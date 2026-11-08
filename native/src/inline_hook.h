#pragma once
#include <cstdint>

// Minimal arm64 inline hook: overwrites the first 16 bytes of `target` (4 instructions, which
// must equal `expect` and must not be PC-relative, but a b.cond, which the trampoline relocates) with `ldr x16, #8; br x16; .quad replacement`.
// Returns a trampoline that runs the 4 displaced instructions and continues at target + 16, or
// nullptr (nothing patched) on any mismatch or failure.
//
// Works under an emulator's arm64 translation (libnb) as long as the target hasn't run yet, so
// install these hooks from JNI_OnLoad.
void* inline_hook(uintptr_t target, const uint32_t (&expect)[4], void* replacement);
