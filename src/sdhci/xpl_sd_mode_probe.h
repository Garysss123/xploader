/* SPDX-License-Identifier: MIT */
#ifndef XPL_SD_MODE_PROBE_H
#define XPL_SD_MODE_PROBE_H
#include "xpl_sd_board.h"
typedef struct {
    uint32_t write_status;
    unsigned before,active,after,write_commands,modified,restored;
    uint64_t hold_us;
    int restore_result;
} xpl_sd_mode_snapshot;
/* Fixed L13 mode-bit probe under the owned boot-time serialized SPMI-v1
 * channel. Requires an enabled known2.95V PLDO with no hardware mode control.
 * Tries only SID1:4c45 bit7 HPM, waits2ms, observes and restores its original
 * value. No voltage, enable, bypass, pin-control, other regulator or SD access.
 * A DENIED status is evidence of unavailable APPS write permission; this code
 * neither bypasses arbitration nor claims RPM ownership from a successful read. */
int xpl_sd_mode_probe(const xpl_sd_board_io *io,uint32_t limit,xpl_sd_mode_snapshot *out);
int xpl_sd_with_hpm(const xpl_sd_board_io *io,uint32_t limit,
                    int (*operation)(void *),void *context,xpl_sd_mode_snapshot *out);
#endif
