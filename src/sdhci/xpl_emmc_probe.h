/* SPDX-License-Identifier: MIT */
#ifndef XPL_EMMC_PROBE_H
#define XPL_EMMC_PROBE_H
#include "xpl_sd_board.h"
typedef struct {
    uint32_t apps_before,apps_after,ahb_before,ahb_live,ahb_after;
    uint32_t rcgr_cmd,rcgr_cfg,m,n,d;
    uint32_t version,caps,caps1,present,host,power,clock,host2,core_mode;
    unsigned reads,writes,enable_attempted,restored;
    int restore_result;
} xpl_emmc_snapshot;
/* Resource-bound SDCC1 observation, before Linux starts. Exclusive boot-CPU
 * ownership and a live peripheral interconnect must be established externally.
 * Only AHB bit0 can change, and is restored on every returning path. No core
 * reset, power/pad changes, card command, DMA or media operation is emitted.
 * An active APPS clock is observed without stopping or reconfiguring it. */
int xpl_emmc_interface_probe(const xpl_sd_board_io *io,uint32_t prerequisites,
                             uint32_t poll_limit,xpl_emmc_snapshot *out);
#endif
