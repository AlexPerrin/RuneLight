// Opt-in NPC_INFO probe (setprop debug.osrsrl.npcprobe 1, then restart the game): an inline hook
// on the client's NPC info reader (ELF 0x46f4c8, build 242) logs every packet's bit payload as
// hex, plus the world view's high-res NPC count before and after. Off unless the property is set
// at startup, so normal builds run the reader unhooked.
#include "npc_probe.h"

#include <sys/system_properties.h>

#include <cstdio>
#include <string>

#include "game.h"
#include "got_hook.h"
#include "inline_hook.h"
#include "log.h"

namespace {

constexpr uintptr_t kNpcInfoReader = 0x46f4c8;
constexpr uint32_t kNpcInfoReaderCode[4] = {0xfc190fe8, 0xa9017bfd, 0xa9026ffc, 0xa90367fa};

// reader(client, world view, large-distance flag, packet buffer)
using ReaderFn = void (*)(uintptr_t, int32_t*, uint32_t, uintptr_t);
ReaderFn g_orig = nullptr;

void hook_reader(uintptr_t client, int32_t* wv, uint32_t large, uintptr_t buf) {
    // Buffer: +0x08 length (bytes), +0x10 data, +0x18 position (bytes).
    int len = *reinterpret_cast<int32_t*>(buf + 8);
    int pos = *reinterpret_cast<int32_t*>(buf + 0x18);
    auto data = *reinterpret_cast<const uint8_t* const*>(buf + 0x10);
    int before = wv ? wv[0x36] : -1;
    std::string hex;
    if (data && len > pos && len - pos <= 4096) {
        char b[3];
        for (int i = pos; i < len; i++) {
            snprintf(b, sizeof b, "%02x", data[i]);
            hex += b;
        }
    }
    g_orig(client, wv, large, buf);
    int after = wv ? wv[0x36] : -1;
    int end = *reinterpret_cast<int32_t*>(buf + 0x18);
    // NPCs in the high-res list (wv +0xd0 indices, +0xd8 count) still unplaced after the reader:
    // fine x/y +0x1fc/+0x200 (placed by the per-cycle movement), +0x268/+0x26c as set by the add, path x/y +0x3f0/+0x418, NpcType* +0x730, spawn clock +0x73c,
    // removal flag +0x768.
    auto list = wv ? *reinterpret_cast<const int32_t* const*>(reinterpret_cast<uintptr_t>(wv) + 0xd0) : nullptr;
    for (int i = 0; list && i < after && i < 1024; i++) {
        uintptr_t n = game::live_npc(list[i]);
        if (!n) {
            LOGI("npcprobe slot %d index %d: no live Npc", i, list[i]);
            continue;
        }
        auto rd = [n](uint32_t o) { return *reinterpret_cast<const int32_t*>(n + o); };
        if (rd(0x1fc) == 0 && rd(0x200) == 0)
            LOGI("npcprobe slot %d index %d unplaced: +268 %d,%d path %d,%d type %p clock %d removed %d", i, list[i],
                 rd(0x268), rd(0x26c), rd(0x3f0), rd(0x418), *reinterpret_cast<void* const*>(n + 0x730), rd(0x73c), *reinterpret_cast<const uint8_t*>(n + 0x768));
    }
    // Logcat lines are capped near 4 KB; long payloads are split.
    for (size_t o = 0; o < hex.size() || o == 0; o += 3000) {
        LOGI("npcprobe large %u pos %d len %d end %d hires %d->%d part %zu %s", large & 1, pos, len, end, before,
             after, o / 3000, hex.substr(o, 3000).c_str());
        if (hex.empty()) break;
    }
}

}  // namespace

void npc_probe_install(const char* module) {
    char value[PROP_VALUE_MAX] = "";
    if (__system_property_get("debug.osrsrl.npcprobe", value) <= 0 || value[0] != '1') return;
    uintptr_t base = module_base(module);
    if (!base) return;
    g_orig = reinterpret_cast<ReaderFn>(
        inline_hook(base + kNpcInfoReader, kNpcInfoReaderCode, reinterpret_cast<void*>(hook_reader)));
    LOGI("npcprobe: %s", g_orig ? "installed" : "failed");
}
