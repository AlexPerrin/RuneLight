// Hiding actors from the scene renderer (Entity Hider). The renderer's drawRenderable (Renderer
// vtable slot 2 0x73ba8c, slot 3 0x73c588 for sub-world-views; ELF, build 242) asks the
// Renderable for its model through vtable slot 0 (getModel(this, cycle) -> shared_ptr<Model> via
// x8) and skips the draw (and picking) when the model is null (`cbz` at 0x72dbc0 / 0x72e6a8).
// The slot-0 wrapper returns an empty model for hidden actors when called from those two sites
// (return addresses 0x73bb0c / 0x73c5f4) and calls the original otherwise (hulls, our own reads).
#include "draw_hide.h"

#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <mutex>

#include "got_hook.h"
#include "log.h"

namespace draw_hide {
namespace {

struct ModelSp {
    uintptr_t a, b;
    ~ModelSp() {}  // non-trivial: returned through x8, as the game does
};
using GetModel = ModelSp (*)(uintptr_t, int);

constexpr uintptr_t kDrawRet = 0x73bb0c, kDrawSubRet = 0x73c5f4;
constexpr const char* kGameLib = "liblibs.hal.system.osclient.so";

std::mutex g_mutex;
uintptr_t g_base = 0;
std::vector<uintptr_t> g_hidden;                      // sorted
std::vector<std::pair<uintptr_t, GetModel>> g_origs;  // vtable -> original slot 0

GetModel orig_for(uintptr_t self) {
    uintptr_t vt = *reinterpret_cast<const uintptr_t*>(self);
    for (auto& [v, f] : g_origs)
        if (v == vt) return f;
    return nullptr;
}

ModelSp hook(uintptr_t self, int cycle) {
    uintptr_t ret = reinterpret_cast<uintptr_t>(__builtin_return_address(0)) - g_base;
    GetModel orig;
    bool hide = false;
    {
        std::lock_guard lock(g_mutex);
        orig = orig_for(self);
        if (ret == kDrawRet || ret == kDrawSubRet) hide = std::binary_search(g_hidden.begin(), g_hidden.end(), self);
    }
    if (hide || !orig) return ModelSp{0, 0};
    return orig(self, cycle);
}

void patch(uintptr_t vt) {
    for (auto& p : g_origs)
        if (p.first == vt) return;
    auto** slot = reinterpret_cast<void**>(vt);
    long page = sysconf(_SC_PAGESIZE);
    auto start = reinterpret_cast<void*>(vt & ~(page - 1));
    if (mprotect(start, page * 2, PROT_READ | PROT_WRITE) != 0) {
        LOGE("draw_hide: mprotect failed for vtable %#lx", (unsigned long)vt);
        return;
    }
    g_origs.emplace_back(vt, reinterpret_cast<GetModel>(*slot));
    *slot = reinterpret_cast<void*>(hook);
    mprotect(start, page * 2, PROT_READ);
    LOGI("draw_hide: getModel hooked on vtable elf %#lx", (unsigned long)(vt - g_base));
}

}  // namespace

void set_hidden(const std::vector<uintptr_t>& actors) {
    std::lock_guard lock(g_mutex);
    if (!g_base) g_base = module_base(kGameLib);
    if (!g_base) return;
    for (uintptr_t a : actors)
        if (a) patch(*reinterpret_cast<const uintptr_t*>(a));
    g_hidden = actors;
    std::sort(g_hidden.begin(), g_hidden.end());
}

}  // namespace draw_hide
