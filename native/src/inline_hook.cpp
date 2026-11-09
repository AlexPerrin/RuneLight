#include "inline_hook.h"

#include <sys/mman.h>
#include <unistd.h>

#include <cstring>
#include <mutex>

#include "log.h"

namespace {

bool pc_relative(uint32_t op) {
    return (op & 0x1f000000) == 0x10000000     // adr, adrp
        || (op & 0x7c000000) == 0x14000000     // b, bl
        || (op & 0xff000010) == 0x54000000     // b.cond
        || (op & 0x7e000000) == 0x34000000     // cbz, cbnz
        || (op & 0x7e000000) == 0x36000000     // tbz, tbnz
        || (op & 0x3b000000) == 0x18000000;    // ldr (literal), ldrsw (literal), prfm (literal)
}

std::mutex g_mutex;
uint8_t* g_page = nullptr;
size_t g_used = 0;
size_t g_page_size = 0;

uint32_t* alloc_trampoline(size_t bytes) {
    if (!g_page || g_used + bytes > g_page_size) {
        g_page_size = static_cast<size_t>(sysconf(_SC_PAGESIZE));
        void* p = mmap(nullptr, g_page_size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (p == MAP_FAILED) return nullptr;
        g_page = static_cast<uint8_t*>(p);
        g_used = 0;
    }
    auto* t = reinterpret_cast<uint32_t*>(g_page + g_used);
    g_used += (bytes + 15) & ~size_t(15);
    return t;
}

void write_jump(uint32_t* at, uintptr_t dest) {
    at[0] = 0x58000050;  // ldr x16, #8
    at[1] = 0xd61f0200;  // br x16
    memcpy(at + 2, &dest, 8);
}

}  // namespace

void* inline_hook(uintptr_t target, const uint32_t (&expect)[4], void* replacement) {
    std::lock_guard lock(g_mutex);
    auto* code = reinterpret_cast<uint32_t*>(target);
    int conds = 0;
    for (int i = 0; i < 4; i++) {
        bool cond = (code[i] & 0xff000010) == 0x54000000;  // b.cond: relocated below
        conds += cond;
        if (code[i] != expect[i] || (pc_relative(code[i]) && !cond)) {
            LOGE("inline_hook: %#lx instruction %d is %08x, expected %08x; not hooking", (unsigned long)target, i,
                 code[i], expect[i]);
            return nullptr;
        }
    }
    // Trampoline: the 4 instructions, a jump to target + 16, then per b.cond an absolute jump to its
    // destination, which the copied b.cond is re-pointed at.
    uint32_t* tramp = alloc_trampoline(32 + 16 * conds);
    if (!tramp) {
        LOGE("inline_hook: trampoline mmap failed");
        return nullptr;
    }
    memcpy(tramp, code, 16);
    write_jump(tramp + 4, target + 16);
    for (int i = 0, k = 0; i < 4; i++) {
        if ((code[i] & 0xff000010) != 0x54000000) continue;
        int64_t imm = static_cast<int32_t>(code[i] << 8) >> 13;  // imm19 (bits 5-23), sign-extended
        uint32_t* far = tramp + 8 + 4 * k++;
        write_jump(far, target + 4 * i + imm * 4);
        tramp[i] = 0x54000000 | ((static_cast<uint32_t>(far - (tramp + i)) & 0x7ffff) << 5) | (code[i] & 0xf);
    }
    __builtin___clear_cache(reinterpret_cast<char*>(tramp), reinterpret_cast<char*>(tramp + 8 + 4 * conds));

    long page = sysconf(_SC_PAGESIZE);
    uintptr_t start = target & ~(uintptr_t)(page - 1);
    size_t len = ((target + 16 + page - 1) & ~(uintptr_t)(page - 1)) - start;
    if (mprotect(reinterpret_cast<void*>(start), len, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        LOGE("inline_hook: mprotect(%#lx) failed", (unsigned long)target);
        return nullptr;
    }
    uint32_t patch[4];
    write_jump(patch, reinterpret_cast<uintptr_t>(replacement));
    memcpy(code, patch, 16);
    mprotect(reinterpret_cast<void*>(start), len, PROT_READ | PROT_EXEC);
    __builtin___clear_cache(reinterpret_cast<char*>(code), reinterpret_cast<char*>(code + 4));
    return tramp;
}
