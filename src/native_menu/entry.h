/* SPDX-License-Identifier: MIT */
#ifndef XPL_NATIVE_ENTRY_H
#define XPL_NATIVE_ENTRY_H
#include <stdint.h>
#include <stddef.h>
struct xpl_entry_state {
    uint64_t x[31];
    uint64_t sp, daif, spsel, vbar, sctlr, el, midr, mpidr, ctr, sp_el1;
};
_Static_assert(sizeof(struct xpl_entry_state)==328, "assembly capture layout");
extern uint64_t xpl_fault_record[3];
int xpl_guard_read8(uintptr_t address, uint32_t *value);
int xpl_guard_read16(uintptr_t address, uint32_t *value);
int xpl_guard_read32(uintptr_t address, uint32_t *value);
int xpl_guard_write8(uintptr_t address, uint32_t value);
int xpl_guard_write16(uintptr_t address, uint32_t value);
int xpl_guard_write32(uintptr_t address, uint32_t value);
int xpl_guard_counter(uint64_t *value);
int xpl_guard_frequency(uint64_t *value);
void xpl_native_main(const struct xpl_entry_state *state, uintptr_t kernel_base);
#endif
