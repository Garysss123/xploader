/* SPDX-License-Identifier: MIT */
#ifndef XPL_SD_PROBE_H
#define XPL_SD_PROBE_H
#include "xpl_sd_board.h"
typedef struct {
    uint32_t ahb_before,ahb_live,ahb_after,apps_before,apps_after;
    uint32_t version,caps,present,host_mode,power,clock,core_mode;
    unsigned enable_attempted,host_reads,restored;
    int restore_result;
} xpl_sd_interface_snapshot;
/* One reversible SDCC2 interface-clock probe. Prerequisites are exclusive
 * boot-time SDCC2 ownership and inherited accessible peripheral interconnect.
 * APPS must already be gated. Only AHB enable bit0 may change, and is restored
 * on every returning path, including an uncertain write or host-read failure.
 * Reads host status/capability/version and core HC_MODE; emits no host writes,
 * reset, card clock, regulator/pad changes, SD command or media operation.
 * Success proves interface reads/restoration only, never PREP_ALL or SD boot. */
int xpl_sd_interface_probe(const xpl_sd_board_io *io,uint32_t prerequisites,
                           uint32_t limit,xpl_sd_interface_snapshot *out);
#endif
