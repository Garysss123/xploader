/* SPDX-License-Identifier: MIT */
#ifndef XPL_DSI_FRAME_H
#define XPL_DSI_FRAME_H
#include "dsi_window.h"
typedef struct {
    uint32_t initial_dcs,current_dcs,saved_masks,last_irq,last_status;
    uint64_t max_wait_us;
    unsigned frames,active;
} xpl_dsi_frame_diag;
/* One full RGB888 Sumire frame. Clear only the owned MDP-done status, preserve
 * existing IRQ masks, trigger through the separate validated MDP presenter,
 * then wait for the new CMD_MDP_DONE before another CPU redraw or handoff.
 * The legacy S1 WRITE_MEMORY_CONTINUE start form is normalized to the Sony
 * donor's WRITE_MEMORY_START2c/CONTINUE3c for whole-frame transfers only.
 * No address/stride/PHY/power changes or pixel command-DMA are exposed. */
int xpl_dsi_frame_begin(const xpl_dsi_window_io *io,xpl_dsi_frame_diag *diag);
int xpl_dsi_frame_wait(const xpl_dsi_window_io *io,uint32_t limit,xpl_dsi_frame_diag *diag);
#endif
