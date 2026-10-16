/* SPDX-License-Identifier: MIT */
#ifndef XPL_SD_CORE_PROBE_H
#define XPL_SD_CORE_PROBE_H
#include "xpl_sd_probe.h"
typedef struct {
    xpl_sd_board_snapshot before,active,after;
    uint32_t mode_before,mode_active,mode_after,clock_before,clock_active;
    uint32_t caps,version,power_status,phase,writes,power_before;
    int restore_result;
    unsigned restored;
} xpl_sd_core_snapshot;
#define XPL_SD_BOARD_NATIVE_WARM_POWER 0x100u
/* Temporary known-XO400k GCC root and HC_MODE bit0. Requires the successful
 * exact SDCC2 interface probe, exclusive boot-CPU ownership and live19.2MHz XO.
 * Refuses APPS on, nonzero legacy mode, bus power or output card clock. Does
 * not write HC POWER/CLOCK, pins, PMIC, reset or a card command. Attempts restore
 * on every returning path and checks the results. No complete-ready flag. */
int xpl_sd_core_clock_probe(const xpl_sd_board_io *io,uint32_t prerequisites,
                             uint32_t limit,xpl_sd_core_snapshot *out);
/* Operation runs under the verified active400k/HC_MODE scope and must stop
 * card-clock output before returning. Clock/mode restore follows either result. */
int xpl_sd_with_core_clock(const xpl_sd_board_io *io,uint32_t prerequisites,
                            uint32_t limit,int (*operation)(void *),void *context,
                            xpl_sd_core_snapshot *out);
#endif
