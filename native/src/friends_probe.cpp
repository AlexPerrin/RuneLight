// Opt-in friends-list probe (setprop debug.osrsrl.friendprobe 1, then restart the game), for the
// mobile friends tab stuck at "Loading friends list" after login (state 1, FRIENDLIST_LOADED,
// never followed by state 2 from UPDATE_FRIENDLIST although the server sends both in order).
// Inline hooks (ELF vaddrs, build 242):
//   0x4487d4 server packet dispatcher (client, ...): logs every change of the state across a call;
//   0x401174 UPDATE_FRIENDLIST reader (FriendSystem*, buf, len, ...) (case 0x19): entry/exit;
//   0x40151c friends clear (FriendSystem*, ...) (writes state 0): entry, with its caller.
// FriendSystem = *(Client +0x46c0); state +0x150 (0 none, 1 loaded, 2 list received).
// Off unless the property is set at startup, so normal builds run these unhooked.
#include "friends_probe.h"

#include <sys/system_properties.h>

#include "game.h"
#include "got_hook.h"
#include "inline_hook.h"
#include "log.h"

namespace {

constexpr uintptr_t kDispatch = 0x4487d4;
constexpr uint32_t kDispatchCode[4] = {0xfc190fe8, 0xa9017bfd, 0xa9026ffc, 0xa90367fa};
constexpr uintptr_t kUpdateFriends = 0x401174;
constexpr uintptr_t kClearFriends = 0x40151c;
constexpr uint32_t kSmallPrologue[4] = {0xa9be7bfd, 0xf9000bf3, 0x910003fd, 0xaa0003f3};
constexpr uint32_t kFriendSystem = 0x4700;
constexpr uint32_t kState = 0x150;

using Fn = __uint128_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
Fn g_dispatch = nullptr, g_update = nullptr, g_clear = nullptr;
uintptr_t g_base = 0;
int g_last_state = -100;
unsigned long g_calls = 0;

int state_of_fs(uintptr_t fs) { return fs ? *reinterpret_cast<const int32_t*>(fs + kState) : -1; }
int state_of_client(uintptr_t client) {
    return client ? state_of_fs(*reinterpret_cast<const uintptr_t*>(client + kFriendSystem)) : -1;
}

__uint128_t hook_dispatch(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    int before = state_of_client(a0);
    __uint128_t r = g_dispatch(a0, a1, a2, a3, a4, a5, a6, a7);
    int after = state_of_client(a0);
    g_calls++;
    if (after != before || after != g_last_state)
        LOGI("friendprobe dispatch #%lu cycle %d: state %d -> %d (last seen %d) ret %llu", g_calls, game::cycle(),
             before, after, g_last_state, static_cast<unsigned long long>(static_cast<uint64_t>(r)));
    g_last_state = after;
    return r;
}

__uint128_t hook_update(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    int before = state_of_fs(a0);
    __uint128_t r = g_update(a0, a1, a2, a3, a4, a5, a6, a7);
    LOGI("friendprobe UPDATE_FRIENDLIST len %u cycle %d: state %d -> %d", static_cast<unsigned>(a2), game::cycle(), before,
         state_of_fs(a0));
    return r;
}

__uint128_t hook_clear(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    uintptr_t caller = reinterpret_cast<uintptr_t>(__builtin_return_address(0)) - g_base;
    int before = state_of_fs(a0);
    __uint128_t r = g_clear(a0, a1, a2, a3, a4, a5, a6, a7);
    LOGI("friendprobe clear from 0x%lx cycle %d: state %d -> %d", static_cast<unsigned long>(caller), game::cycle(), before,
         state_of_fs(a0));
    return r;
}

}  // namespace

void friends_probe_install(const char* module) {
    char value[PROP_VALUE_MAX] = "";
    if (__system_property_get("debug.osrsrl.friendprobe", value) <= 0 || value[0] != '1') return;
    g_base = module_base(module);
    if (!g_base) return;
    g_update = reinterpret_cast<Fn>(inline_hook(g_base + kUpdateFriends, kSmallPrologue, reinterpret_cast<void*>(hook_update)));
    g_clear = reinterpret_cast<Fn>(inline_hook(g_base + kClearFriends, kSmallPrologue, reinterpret_cast<void*>(hook_clear)));
    g_dispatch = reinterpret_cast<Fn>(inline_hook(g_base + kDispatch, kDispatchCode, reinterpret_cast<void*>(hook_dispatch)));
    LOGI("friendprobe: update %s, clear %s, dispatch %s", g_update ? "hooked" : "failed", g_clear ? "hooked" : "failed",
         g_dispatch ? "hooked" : "failed");
}
