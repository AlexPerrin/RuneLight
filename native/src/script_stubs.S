// Entry stubs for inline hooks that only need to look at a function's arguments: save x0-x8 and
// the return address, call `<name>_cb(const uint64_t regs[9], uintptr_t lr)`, restore everything
// and continue in the original function through the trampoline stored in `<name>_orig`.
    .text

    .macro PREHOOK name
    .global \name\()_stub
    .hidden \name\()_stub
    .type \name\()_stub, %function
    .p2align 2
\name\()_stub:
    stp x29, x30, [sp, #-0x60]!
    mov x29, sp
    stp x0, x1, [sp, #0x10]
    stp x2, x3, [sp, #0x20]
    stp x4, x5, [sp, #0x30]
    stp x6, x7, [sp, #0x40]
    str x8, [sp, #0x50]
    add x0, sp, #0x10
    mov x1, x30
    bl \name\()_cb
    ldp x0, x1, [sp, #0x10]
    ldp x2, x3, [sp, #0x20]
    ldp x4, x5, [sp, #0x30]
    ldp x6, x7, [sp, #0x40]
    ldr x8, [sp, #0x50]
    ldp x29, x30, [sp], #0x60
    adrp x16, \name\()_orig
    ldr x16, [x16, :lo12:\name\()_orig]
    br x16
    .size \name\()_stub, . - \name\()_stub
    .endm

    PREHOOK osrsrl_script_lookup
    PREHOOK osrsrl_script_swap

// Full-save entry stub for hooks on leaf functions that callers may treat as clobbering only a few
// registers: saves x0-x18, x30 and q0-q7/q16-q31, calls `<name>_cb(uintptr_t lr)`, restores them and
// continues in the original through `<name>_orig`.
    .macro FULLHOOK name
    .global \name\()_stub
    .hidden \name\()_stub
    .type \name\()_stub, %function
    .p2align 2
\name\()_stub:
    sub sp, sp, #0x230
    stp x0, x1, [sp, #0x00]
    stp x2, x3, [sp, #0x10]
    stp x4, x5, [sp, #0x20]
    stp x6, x7, [sp, #0x30]
    stp x8, x9, [sp, #0x40]
    stp x10, x11, [sp, #0x50]
    stp x12, x13, [sp, #0x60]
    stp x14, x15, [sp, #0x70]
    stp x16, x17, [sp, #0x80]
    stp x18, x30, [sp, #0x90]
    stp x29, xzr, [sp, #0xa0]
    stp q0, q1, [sp, #0xb0]
    stp q2, q3, [sp, #0xd0]
    stp q4, q5, [sp, #0xf0]
    stp q6, q7, [sp, #0x110]
    stp q16, q17, [sp, #0x130]
    stp q18, q19, [sp, #0x150]
    stp q20, q21, [sp, #0x170]
    stp q22, q23, [sp, #0x190]
    stp q24, q25, [sp, #0x1b0]
    stp q26, q27, [sp, #0x1d0]
    stp q28, q29, [sp, #0x1f0]
    stp q30, q31, [sp, #0x210]
    mrs x9, nzcv
    str x9, [sp, #0xa8]
    add x29, sp, #0xa0
    mov x0, x30
    bl \name\()_cb
    ldr x9, [sp, #0xa8]
    msr nzcv, x9
    ldp q0, q1, [sp, #0xb0]
    ldp q2, q3, [sp, #0xd0]
    ldp q4, q5, [sp, #0xf0]
    ldp q6, q7, [sp, #0x110]
    ldp q16, q17, [sp, #0x130]
    ldp q18, q19, [sp, #0x150]
    ldp q20, q21, [sp, #0x170]
    ldp q22, q23, [sp, #0x190]
    ldp q24, q25, [sp, #0x1b0]
    ldp q26, q27, [sp, #0x1d0]
    ldp q28, q29, [sp, #0x1f0]
    ldp q30, q31, [sp, #0x210]
    ldp x0, x1, [sp, #0x00]
    ldp x2, x3, [sp, #0x10]
    ldp x4, x5, [sp, #0x20]
    ldp x6, x7, [sp, #0x30]
    ldp x8, x9, [sp, #0x40]
    ldp x10, x11, [sp, #0x50]
    ldp x12, x13, [sp, #0x60]
    ldp x14, x15, [sp, #0x70]
    ldp x18, x30, [sp, #0x90]
    ldr x29, [sp, #0xa0]
    ldp x16, x17, [sp, #0x80]
    add sp, sp, #0x230
    adrp x16, \name\()_orig
    ldr x16, [x16, :lo12:\name\()_orig]
    br x16
    .size \name\()_stub, . - \name\()_stub
    .endm

    FULLHOOK osrsrl_drag_check

// Post-call stub for leaf functions with up to 4 register arguments and no stack arguments: keeps
// x0-x3 and the return address, runs the original through `<name>_orig`, then saves every register
// as the original left it (x0-x18, q0-q7/q16-q31, nzcv), calls `<name>_cb(const uint64_t args[4],
// uint64_t regs[19], uintptr_t lr)` (it may change regs, e.g. regs[0], the result), restores them
// and returns to the caller.
    .macro POSTHOOK name
    .global \name\()_stub
    .hidden \name\()_stub
    .type \name\()_stub, %function
    .p2align 2
\name\()_stub:
    sub sp, sp, #0x30
    stp x0, x1, [sp, #0x00]
    stp x2, x3, [sp, #0x10]
    stp x29, x30, [sp, #0x20]
    adrp x16, \name\()_orig
    ldr x16, [x16, :lo12:\name\()_orig]
    blr x16
    sub sp, sp, #0x230
    stp x0, x1, [sp, #0x00]
    stp x2, x3, [sp, #0x10]
    stp x4, x5, [sp, #0x20]
    stp x6, x7, [sp, #0x30]
    stp x8, x9, [sp, #0x40]
    stp x10, x11, [sp, #0x50]
    stp x12, x13, [sp, #0x60]
    stp x14, x15, [sp, #0x70]
    stp x16, x17, [sp, #0x80]
    stp x18, xzr, [sp, #0x90]
    stp x29, xzr, [sp, #0xa0]
    stp q0, q1, [sp, #0xb0]
    stp q2, q3, [sp, #0xd0]
    stp q4, q5, [sp, #0xf0]
    stp q6, q7, [sp, #0x110]
    stp q16, q17, [sp, #0x130]
    stp q18, q19, [sp, #0x150]
    stp q20, q21, [sp, #0x170]
    stp q22, q23, [sp, #0x190]
    stp q24, q25, [sp, #0x1b0]
    stp q26, q27, [sp, #0x1d0]
    stp q28, q29, [sp, #0x1f0]
    stp q30, q31, [sp, #0x210]
    mrs x9, nzcv
    str x9, [sp, #0xa8]
    add x29, sp, #0xa0
    add x0, sp, #0x230
    mov x1, sp
    ldr x2, [sp, #0x258]
    bl \name\()_cb
    ldr x9, [sp, #0xa8]
    msr nzcv, x9
    ldp q0, q1, [sp, #0xb0]
    ldp q2, q3, [sp, #0xd0]
    ldp q4, q5, [sp, #0xf0]
    ldp q6, q7, [sp, #0x110]
    ldp q16, q17, [sp, #0x130]
    ldp q18, q19, [sp, #0x150]
    ldp q20, q21, [sp, #0x170]
    ldp q22, q23, [sp, #0x190]
    ldp q24, q25, [sp, #0x1b0]
    ldp q26, q27, [sp, #0x1d0]
    ldp q28, q29, [sp, #0x1f0]
    ldp q30, q31, [sp, #0x210]
    ldp x0, x1, [sp, #0x00]
    ldp x2, x3, [sp, #0x10]
    ldp x4, x5, [sp, #0x20]
    ldp x6, x7, [sp, #0x30]
    ldp x8, x9, [sp, #0x40]
    ldp x10, x11, [sp, #0x50]
    ldp x12, x13, [sp, #0x60]
    ldp x14, x15, [sp, #0x70]
    ldp x16, x17, [sp, #0x80]
    ldr x18, [sp, #0x90]
    add sp, sp, #0x230
    ldp x29, x30, [sp, #0x20]
    add sp, sp, #0x30
    ret
    .size \name\()_stub, . - \name\()_stub
    .endm

    POSTHOOK osrsrl_widget_flags
    POSTHOOK osrsrl_widget_align
