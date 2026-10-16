/* SPDX-License-Identifier: MIT */
#ifndef XPL_BOOTARGS_H
#define XPL_BOOTARGS_H
#include <stdint.h>
#include <stddef.h>
enum { XPL_BOOTARGS_OK=0, XPL_BOOTARGS_ARGUMENT=1, XPL_BOOTARGS_UNTERMINATED=2 };
/* Remove charging-mode arguments from a bounded string. */
int xpl_bootargs_interactive(volatile uint8_t *args,size_t bytes,unsigned *removed);
typedef struct {
    unsigned properties,chosen_nodes,removed,startup_masked,nonterminated;
    uint64_t first_startup;
} xpl_bootargs_stats;
/* Two-pass, bounds-checked DT walk. Handles every /chosen or /chosen@0 bootargs
 * property, including duplicate nodes/properties and length-delimited strings.
 * Removes charger mode and masks startup bits0x4004 which this exact Sony
 * kernel uses to SYNTHESIZE a new charger argument when warmboot is zero.
 * All changes stay within existing argument bytes. No FDT resizing or IMEM/TA
 * writes. A malformed tree is rejected before any tree mutation. */
int xpl_bootargs_clean_fdt(void *fdt,size_t available,xpl_bootargs_stats *stats);
#endif
