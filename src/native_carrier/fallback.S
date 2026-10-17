/* SPDX-License-Identifier: MIT. Stack-free E6653/S1 abort continuation.
 * Uses only the measured restart-reason + APPS WDT bank. No PMIC, DLOAD,
 * storage, USB or clock writes. Unsupported state is recorded, not cleared.
 * Link at0, place at Image+0x1000; an owned64-byte record is Image+0x800.
 */
#ifndef XPL_FALLBACK_SECONDS
#define XPL_FALLBACK_SECONDS 20
#endif
.section .text,"ax"
.global xpl_carrier_fallback
xpl_carrier_fallback:
    adr x10, xpl_carrier_fallback
    sub x10, x10, #0x800
    ldr x11, =0x314c4c41464c5058
    str x11, [x10]
    mov w11, #1
    str w11, [x10, #8]
    mov w11, #XPL_FALLBACK_SECONDS
    str w11, [x10, #12]
    mrs x0, CurrentEL
    cmp x0, #4
    b.ne bad_cpu
    msr daifset, #15
    mrs x0, sctlr_el1
    tst x0, #1
    b.ne bad_cpu
    tst x0, #4
    b.ne bad_cpu
    ldr x1, =0xfe87f000
    ldr w2, [x1]
    ldr w3, [x1, #4]
    stp w2, w3, [x10, #16]
    orr w4, w2, w3
    cbz w4, idle
    mvn w2, w2
    mvn w3, w3
    orr w4, w2, w3
    cbnz w4, bad_cookie
idle:
    ldr x1, =0xf9017000
    ldr w2, [x1, #8]
    str w2, [x10, #24]
    tst w2, #0xfffffffc
    b.ne bad_enable
    mov w11, #2
    str w11, [x10, #8]
    ldr x5, =0xfe87f65c
    ldr w6, [x5]
    str w6, [x10, #32]
    ldr w6, =0x77665500
    str w6, [x5]
    dsb sy
    ldr w7, [x5]
    cmp w6, w7
    b.ne bad_readback
    str wzr, [x1, #8]
    mov w2, #1
    str w2, [x1, #4]
    ldr w3, =((XPL_FALLBACK_SECONDS-5)*32765)
    ldr w4, =(XPL_FALLBACK_SECONDS*32765)
    str w3, [x1, #0x10]
    str w4, [x1, #0x14]
    dsb sy
    str w2, [x1, #8]
    dsb sy
    ldr w5, [x1, #8]
    str w5, [x10, #28]
    cmp w5, #1
    b.ne bad_readback
    ldr w5, [x1, #0x10]
    cmp w5, w3
    b.ne bad_readback
    ldr w5, [x1, #0x14]
    cmp w5, w4
    b.ne bad_readback
    stp w3, w4, [x10, #36]
    mov w11, #3
    str w11, [x10, #8]
    dsb sy
wait_reset:
    wfe
    b wait_reset
bad_cpu:
    mov w11, #0xe0
    b failed
bad_cookie:
    mov w11, #0xe1
    b failed
bad_enable:
    mov w11, #0xe2
    b failed
bad_readback:
    mov w11, #0xe3
failed:
    str w11, [x10, #8]
    dsb sy
    b wait_reset
.balign 8
.ltorg
.section .note.GNU-stack,"",%progbits
