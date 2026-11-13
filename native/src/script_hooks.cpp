#include "script_hooks.h"

#include <sys/system_properties.h>

#include <atomic>
#include <string>
#include <vector>

#include "client_thread.h"
#include "game.h"
#include "got_hook.h"
#include "inline_hook.h"
#include "log.h"

// Filled in by install(); read by the stubs in script_stubs.S.
extern "C" {
__attribute__((visibility("hidden"))) void* osrsrl_script_lookup_orig = nullptr;
__attribute__((visibility("hidden"))) void* osrsrl_script_swap_orig = nullptr;
void osrsrl_script_lookup_stub();
void osrsrl_script_swap_stub();
void osrsrl_script_lookup_cb(const uint64_t* regs, uintptr_t lr);
void osrsrl_script_swap_cb(const uint64_t* regs, uintptr_t lr);
}

namespace script_hooks {
namespace {

// ELF vaddrs, build 242 (docs/offsets.md "Script VM").
constexpr uintptr_t kRunScriptEvent = 0x588988;
constexpr uintptr_t kScriptLookup = 0x3faa9c;
constexpr uintptr_t kSwapLocals = 0x5891a0;
constexpr uintptr_t kGosubLookupRet = 0x586d80;  // bl kScriptLookup in case 0x28 (GOSUB)
constexpr uintptr_t kReturnSwapRet = 0x587590;   // bl kSwapLocals in case 0x15 (RETURN)
constexpr uint32_t kRunScriptEventCode[4] = {0xd10343ff, 0xa9077bfd, 0xa9086ffc, 0xa90967fa};
constexpr uint32_t kScriptLookupCode[4] = {0xd10243ff, 0xa9047bfd, 0xf9002bf9, 0xa9065ff8};
constexpr uint32_t kSwapLocalsCode[4] = {0xd101c3ff, 0xa9017bfd, 0xf90013fb, 0xa90367fa};
constexpr uintptr_t kOpDispatch = 0x589b20;  // opcodes >= 100: (vm, opcode, ...)
constexpr uint32_t kOpDispatchCode[4] = {0xd10583ff, 0xa9107bfd, 0xa9116ffc, 0xa91267fa};

using RunScriptEventFn = void (*)(uintptr_t vm, uintptr_t ev, int max_ops);
RunScriptEventFn g_orig_run = nullptr;
uintptr_t g_base = 0;
Listener g_listener = nullptr;
std::atomic<bool> g_enabled{false};
bool g_log = false;

// Script ids of the running root scripts and procs, innermost last. Only the game thread runs
// scripts; a root script that errors out leaves its procs here until the root returns.
constexpr int kMaxDepth = 1024;
int g_ids[kMaxDepth];
int g_depth = 0;
int g_root_base = 0;  // g_depth index of the innermost root script
uintptr_t g_event = 0;
uintptr_t g_return_frame = 0;

// Watched (script, opcode) pairs; written by the game thread's JNI calls only.
int32_t g_watch[kMaxOpWatches * 2];
std::string g_watch_prefix[kMaxOpWatches];
std::atomic<int> g_watch_count{0};
using OpDispatchFn = __uint128_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
OpDispatchFn g_orig_op = nullptr;

std::atomic<uint64_t> g_roots{0}, g_gosubs{0}, g_returns{0}, g_delivered{0};

void deliver(int phase, int id) {
    if (!g_enabled.load(std::memory_order_relaxed) || !g_listener || !client_thread::on_client_thread()) return;
    g_delivered.fetch_add(1, std::memory_order_relaxed);
    g_listener(phase, id);
}

// Arg 0 of the event (a 0x28-byte variant, u32 type at +0x20, 1 = int) is the script id when
// the event looks the script up by id; -1 otherwise.
int event_script_id(uintptr_t ev) {
    int argc = *reinterpret_cast<int32_t*>(ev + 0x08);
    uintptr_t argv = *reinterpret_cast<uintptr_t*>(ev + 0x10);
    if (argc <= 0 || !argv || *reinterpret_cast<uint32_t*>(argv + 0x20) != 1) return -1;
    return *reinterpret_cast<int32_t*>(argv);
}

void hook_run_script_event(uintptr_t vm, uintptr_t ev, int max_ops) {
    g_roots.fetch_add(1, std::memory_order_relaxed);
    int id = ev ? event_script_id(ev) : -1;
    if ((id & 0xffff0000) == kJavaListener) {  // no such script: the host's Java listener
        uintptr_t outer = g_event;
        g_event = ev;
        if (g_listener && client_thread::on_client_thread()) g_listener(JavaListener, id & 0xffff);
        g_event = outer;
        return;
    }
    int base = g_depth, outer_root = g_root_base;
    uintptr_t outer_event = g_event;
    g_root_base = base;
    if (g_depth < kMaxDepth) g_ids[g_depth] = id;
    g_depth++;
    g_event = ev;
    if (g_log) LOGI("script: root %d (depth %d)", id, base);
    if (id >= 0) deliver(RootPre, id);
    g_orig_run(vm, ev, max_ops);
    if (id >= 0) deliver(Post, id);
    g_depth = base;
    g_root_base = outer_root;
    g_event = outer_event;
}

// The wrapper keeps x0-x7 and the x0:x1 result; the handler takes (vm, opcode, ...) in x0-x3.
__uint128_t hook_op_dispatch(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5,
                             uint64_t a6, uint64_t a7) {
    int n = g_watch_count.load(std::memory_order_relaxed);
    int match = -1;
    if (n > 0 && g_depth > 0 && g_depth <= kMaxDepth && g_listener) {
        int op = static_cast<int32_t>(a1), id = g_ids[g_depth - 1];
        for (int i = 0; i < n && match < 0; i++) {
            if (g_watch[2 * i + 1] != op) continue;
            if (g_watch[2 * i] == id) match = (op << 16) | (id & 0xffff);
            else if (g_watch[2 * i] == -1 && client_thread::on_client_thread()
                     && (g_watch_prefix[i].empty() || game::script_top_string_starts_with(g_watch_prefix[i])))
                match = (op << 16) | 0xffff;
        }
        if (match >= 0 && !client_thread::on_client_thread()) match = -1;
    }
    if (match >= 0) g_listener(OpPre, match);
    __uint128_t r = g_orig_op(a0, a1, a2, a3, a4, a5, a6, a7);
    if (match >= 0) g_listener(OpPost, match);
    return r;
}

}  // namespace

void set_op_watches(const int32_t* pairs, const std::string* prefixes, int count) {
    if (count < 0) count = 0;
    if (count > kMaxOpWatches) count = kMaxOpWatches;
    g_watch_count.store(0, std::memory_order_relaxed);
    for (int i = 0; i < count * 2; i++) g_watch[i] = pairs[i];
    for (int i = 0; i < count; i++) g_watch_prefix[i] = prefixes ? prefixes[i] : std::string();
    g_watch_count.store(count, std::memory_order_relaxed);
}

bool install() {
    if (!game::available()) return false;
    g_base = module_base("liblibs.hal.system.osclient.so");
    if (!g_base) return false;
    char value[PROP_VALUE_MAX] = "";
    __system_property_get("debug.osrsrl.scripts", value);
    // Verified under arm64 translation (libnb) 2026-10-16. setprop debug.osrsrl.scripts 0 = off, 2 = log
    // every root/gosub/return (very verbose).
    if (value[0] == '0') {
        LOGI("script hooks off (debug.osrsrl.scripts=0)");
        return false;
    }
    g_log = value[0] == '2';
    // Hook the two stubs' targets first: runScriptEvent is the only way scripts start, so it
    // goes last (a partial install never sees procs without their root).
    osrsrl_script_lookup_orig = inline_hook(g_base + kScriptLookup, kScriptLookupCode,
                                            reinterpret_cast<void*>(osrsrl_script_lookup_stub));
    if (!osrsrl_script_lookup_orig) return false;
    osrsrl_script_swap_orig = inline_hook(g_base + kSwapLocals, kSwapLocalsCode,
                                          reinterpret_cast<void*>(osrsrl_script_swap_stub));
    if (!osrsrl_script_swap_orig) return false;
    // Opcode wrapper: optional (opcode callbacks only); a failure leaves the rest working.
    g_orig_op = reinterpret_cast<OpDispatchFn>(
        inline_hook(g_base + kOpDispatch, kOpDispatchCode, reinterpret_cast<void*>(hook_op_dispatch)));
    if (!g_orig_op) LOGI("script hooks: opcode dispatch hook not installed");
    g_orig_run = reinterpret_cast<RunScriptEventFn>(
        inline_hook(g_base + kRunScriptEvent, kRunScriptEventCode, reinterpret_cast<void*>(hook_run_script_event)));
    if (!g_orig_run) return false;
    LOGI("script hooks installed");
    return true;
}

bool installed() { return g_orig_run != nullptr; }
void set_listener(Listener listener) { g_listener = listener; }
void set_enabled(bool enabled) { g_enabled.store(enabled, std::memory_order_relaxed); }
uintptr_t current_event() { return g_event; }
uintptr_t return_frame() { return g_return_frame; }
int current_script() { return g_depth > 0 && g_depth <= kMaxDepth ? g_ids[g_depth - 1] : -1; }

// Script (decoder 0x3f5964, static RE client-api-host-gaps-reach): +0x18 u64 id, +0x28 u64 instruction count,
// +0x30 int32 opcodes, +0x48 int32 int operands (one per instruction; u8 operands widened). The lookup
// (unhooked original) returns the cached {ctrl, Script*}, loading it on a miss. Game thread only.
bool script_code(int id, std::vector<int32_t>& ops, std::vector<int32_t>& ints) {
    ops.clear();
    ints.clear();
    if (!osrsrl_script_lookup_orig || id < 0 || id > 0xffff) return false;
    auto sp = reinterpret_cast<const uintptr_t* (*)(int)>(osrsrl_script_lookup_orig)(id);
    uintptr_t s = sp ? sp[1] : 0;
    if (!s || *reinterpret_cast<const uint64_t*>(s + 0x18) != static_cast<uint64_t>(id)) return false;
    uint64_t n = *reinterpret_cast<const uint64_t*>(s + 0x28);
    auto op = *reinterpret_cast<const int32_t* const*>(s + 0x30);
    auto in = *reinterpret_cast<const int32_t* const*>(s + 0x48);
    if (!op || !in || n == 0 || n > 1000000) return false;
    ops.assign(op, op + n);
    ints.assign(in, in + n);
    return true;
}

Stats stats() {
    return {g_roots.load(), g_gosubs.load(), g_returns.load(), g_delivered.load()};
}

}  // namespace script_hooks

using namespace script_hooks;

extern "C" void osrsrl_script_lookup_cb(const uint64_t* regs, uintptr_t lr) {
    if (lr != g_base + kGosubLookupRet) return;
    int id = static_cast<int32_t>(regs[0]);
    g_gosubs.fetch_add(1, std::memory_order_relaxed);
    if (g_depth < kMaxDepth) g_ids[g_depth] = id;
    g_depth++;
    if (g_log) LOGI("script: gosub %d (depth %d)", id, g_depth - 1);
    deliver(Pre, id);
}

extern "C" void osrsrl_script_swap_cb(const uint64_t* regs, uintptr_t lr) {
    if (lr != g_base + kReturnSwapRet) return;
    g_returns.fetch_add(1, std::memory_order_relaxed);
    // The root's own entry is never popped here: RETURN with no call frames leaves the
    // interpreter without swapping.
    if (g_depth <= g_root_base + 1) return;
    g_depth--;
    int id = g_depth < kMaxDepth ? g_ids[g_depth] : -1;
    if (g_log) LOGI("script: return %d (depth %d)", id, g_depth);
    // swap(frame, vm+8, vm+0x20, vm+0x38) has not run yet: x0 is the caller's saved frame.
    g_return_frame = regs ? static_cast<uintptr_t>(regs[0]) : 0;
    if (id >= 0) deliver(Post, id);
    g_return_frame = 0;
}
