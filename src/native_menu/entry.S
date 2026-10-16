/* SPDX-License-Identifier: MIT
 * Native pre-Linux menu. Linux boot argument registers and original stack are
 * restored on return; x9 is scratch at this hook, as in the accepted probe.
 * MMIO and counter primitives return -1 after a synchronous access exception.
 */
#include "native_menu_config.h"
.section .text.entry,"ax"
.balign 16
.global _start
_start:
    adr x9, xpl_saved
    stp x0, x1, [x9, #0]
    stp x2, x3, [x9, #16]
    stp x4, x5, [x9, #32]
    stp x6, x7, [x9, #48]
    str x8, [x9, #64]
    str xzr, [x9, #72]
    stp x10, x11, [x9, #80]
    stp x12, x13, [x9, #96]
    stp x14, x15, [x9, #112]
    stp x16, x17, [x9, #128]
    stp x18, x19, [x9, #144]
    stp x20, x21, [x9, #160]
    stp x22, x23, [x9, #176]
    stp x24, x25, [x9, #192]
    stp x26, x27, [x9, #208]
    stp x28, x29, [x9, #224]
    str x30, [x9, #240]
    mov x10, sp
    str x10, [x9, #248]
    mrs x10, CurrentEL
    str x10, [x9, #288]
    cmp x10, #4
    b.ne xpl_restore_registers
    mrs x10, sctlr_el1
    str x10, [x9, #280]
    tbnz x10, #0, xpl_restore_registers
    tbnz x10, #2, xpl_restore_registers
    mrs x10, daif
    str x10, [x9, #256]
    msr daifset, #15
    mrs x10, SPSel
    str x10, [x9, #264]
    mrs x10, vbar_el1
    str x10, [x9, #272]
    msr SPSel, #1
    mov x10, sp
    str x10, [x9, #320]
    adr x10, xpl_stack_top
    mov sp, x10
    adr x10, xpl_vectors
    msr vbar_el1, x10
    isb
    mrs x10, midr_el1
    str x10, [x9, #296]
    mrs x10, mpidr_el1
    str x10, [x9, #304]
    mrs x10, ctr_el0
    str x10, [x9, #312]
    adr x10, __bss_start
    adr x11, __bss_end
1:  cmp x10, x11
    b.hs 2f
    strb wzr, [x10], #1
    b 1b
2:  adr x10, _start
    ldr x11, .Llink_entry
    sub x10, x10, x11
    adr x12, __rela_start
    adr x13, __rela_end
3:  cmp x12, x13
    b.hs 4f
    ldp x14, x15, [x12]
    cmp x15, #1027
    b.ne xpl_restore_state
    ldr x16, [x12, #16]
    add x14, x14, x10
    add x16, x16, x10
    str x16, [x14]
    add x12, x12, #24
    b 3b
4:  mov x0, x9
    adr x1, _start
    ldr x10, .Lstub_offset
    sub x1, x1, x10
    bl xpl_native_main
    b xpl_restore_state

.global xpl_restore_state
xpl_restore_state:
    adr x9, xpl_saved
    ldr x10, [x9, #272]
    msr vbar_el1, x10
    isb
    ldr x10, [x9, #320]
    mov sp, x10
    ldr x10, [x9, #264]
    msr SPSel, x10
    ldr x10, [x9, #248]
    mov sp, x10
    ldr x10, [x9, #256]
    msr daif, x10
xpl_restore_registers:
    adr x9, xpl_saved
    ldp x0, x1, [x9, #0]
    ldp x2, x3, [x9, #16]
    ldp x4, x5, [x9, #32]
    ldp x6, x7, [x9, #48]
    ldr x8, [x9, #64]
    ldp x10, x11, [x9, #80]
    ldp x12, x13, [x9, #96]
    ldp x14, x15, [x9, #112]
    ldp x16, x17, [x9, #128]
    ldp x18, x19, [x9, #144]
    ldp x20, x21, [x9, #160]
    ldp x22, x23, [x9, #176]
    ldp x24, x25, [x9, #192]
    ldp x26, x27, [x9, #208]
    ldp x28, x29, [x9, #224]
    ldr x30, [x9, #240]
    .inst XPL_REPLAY_INSN
    adr x9, _start
    ldr x4, .Lresume_delta
    add x9, x9, x4
    /* x4 is not an ARM64 Linux boot argument. Restore it before branching. */
    adr x4, xpl_saved
    ldr x4, [x4, #32]
    br x9
.balign 8
.Lstub_offset: .quad XPL_STUB_OFFSET
.Lresume_delta: .quad XPL_ORIGINAL_ENTRY - XPL_STUB_OFFSET
.Llink_entry: .quad XPL_LINK_ENTRY

.section .text.vectors,"ax"
.balign 2048
.global xpl_vectors
xpl_vectors:
.rept 16
    b xpl_exception
    .space 124
.endr
xpl_exception:
    stp x0, x1, [sp, #-48]!
    stp x2, x3, [sp, #16]
    stp x4, x5, [sp, #32]
    adr x0, xpl_fault_record
    mrs x1, esr_el1
    mrs x2, far_el1
    mrs x3, elr_el1
    stp x1, x2, [x0]
    str x3, [x0, #16]
    adr x0, xpl_guard_resume
    ldr x4, [x0]
    cbz x4, .Lunexpected
    lsr x5, x1, #26
    cmp x5, #0x25
    b.eq .Lhandled
    cmp x5, #0x18
    b.ne .Lunexpected
.Lhandled:
    msr elr_el1, x4
    str xzr, [x0]
    ldp x4, x5, [sp, #32]
    ldp x2, x3, [sp, #16]
    ldp x0, x1, [sp], #48
    eret
.Lunexpected:
    /* Discard the C frames; the immutable original entry is the fallback.
     * No hardware/menu success is attributed to this recovery path. */
    mov x0, #0
    str x0, [sp]
    b xpl_restore_state

.macro GUARDED_READ name, insn, dest
.global \name
\name:
    adr x2, 8f
    adr x3, xpl_guard_resume
    str x2, [x3]
    dsb sy
    \insn \dest, [x0]
    str w2, [x1]
    str xzr, [x3]
    mov w0, #0
    ret
8:  mov w0, #-1
    ret
.endm
GUARDED_READ xpl_guard_read8, ldrb, w2
GUARDED_READ xpl_guard_read16, ldrh, w2
GUARDED_READ xpl_guard_read32, ldr, w2

.macro GUARDED_WRITE name, insn, source
.global \name
\name:
    adr x2, 8f
    adr x3, xpl_guard_resume
    str x2, [x3]
    dsb sy
    \insn \source, [x0]
    dsb sy
    str xzr, [x3]
    mov w0, #0
    ret
8:  mov w0, #-1
    ret
.endm
GUARDED_WRITE xpl_guard_write8, strb, w1
GUARDED_WRITE xpl_guard_write16, strh, w1
GUARDED_WRITE xpl_guard_write32, str, w1

.global xpl_guard_counter
xpl_guard_counter:
    adr x2, 8f
    adr x3, xpl_guard_resume
    str x2, [x3]
    isb
    mrs x2, cntpct_el0
    str x2, [x0]
    str xzr, [x3]
    mov w0, #0
    ret
8:  mov w0, #-1
    ret
.global xpl_guard_frequency
xpl_guard_frequency:
    adr x2, 8f
    adr x3, xpl_guard_resume
    str x2, [x3]
    mrs x2, cntfrq_el0
    str x2, [x0]
    str xzr, [x3]
    mov w0, #0
    ret
8:  mov w0, #-1
    ret

.section .data,"aw"
.balign 16
.global xpl_saved
xpl_saved: .space 328
.global xpl_guard_resume
xpl_guard_resume: .quad 0
.global xpl_fault_record
xpl_fault_record: .space 24
.balign 16
xpl_stack_bottom: .space 65536
xpl_stack_top: .quad 0x4d454e5558504c31
.section .note.GNU-stack,"",%progbits
