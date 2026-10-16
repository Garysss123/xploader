/* SPDX-License-Identifier: MIT */
#ifndef XPL_SD_POWERED_H
#define XPL_SD_POWERED_H
#include "xpl_sd_board.h"
typedef struct {
    uint32_t phase,verified,request,ack,vendor,pads_before,pads_after;
    uint32_t mask_before,control_before,r7,last_irq,power_before,power_after_reset,warm_reused;
    unsigned pads_changed,started;
    int cleanup_result;
} xpl_sd_powered_diag;
/* Called ONLY inside earned live400k/HPM scopes. Resets the separate SDCC2
 * core, establishes one-bit high-voltage/pads and the actual BUS_ON handshake,
 * then obtains card presence from CMD8. No media command, voltage or regulator
 * enable write. The caller must cleanup even on preparation failure. */
int xpl_sd_prepare_legacy(const xpl_sd_board_io *io,uint32_t limit,
                           const xpl_sd_ldo_state *l13,const xpl_sd_ldo_state *l21,
                           xpl_sd_prepared *out,xpl_sd_powered_diag *diag);
/* Same native bootCPU epoch only: revalidate rails/pads/IO-high and issue fresh
 * CMD0/CMD8. Retained bus power gets no invented BUS_ON interrupt. */
int xpl_sd_prepare_legacy_owned(const xpl_sd_board_io *io,uint32_t limit,
    const xpl_sd_ldo_state *l13,const xpl_sd_ldo_state *l21,unsigned warm_owned,
    xpl_sd_prepared *out,xpl_sd_powered_diag *diag);
int xpl_sd_cleanup_legacy(const xpl_sd_board_io *io,uint32_t limit,xpl_sd_powered_diag *diag);
#endif
