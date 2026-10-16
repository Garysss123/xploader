/* SPDX-License-Identifier: MIT */
#ifndef XPL_DSI_WINDOW_H
#define XPL_DSI_WINDOW_H
#include <stdint.h>
typedef struct {
    void *ctx;
    int (*read)(void *,uint32_t offset,uint32_t *value);
    int (*write)(void *,uint32_t offset,uint32_t value);
    uint64_t (*now_us)(void *);
    void (*fence)(void *);
    void (*relax)(void *);
} xpl_dsi_window_io;
typedef struct {uint32_t revision,dma_control,trigger_control,tpg_control;unsigned commands;} xpl_dsi_window_diag;
enum {XPL_DSI_WINDOW_OK=0,XPL_DSI_WINDOW_ARGUMENT=1,XPL_DSI_WINDOW_UNSUPPORTED=2,
      XPL_DSI_WINDOW_IO=3,XPL_DSI_WINDOW_TIMEOUT=4};
/* Fixed Sumire DSI0 command-mode 1080x1920 window. Uses the MSM8994 1.3
 * command FIFO supported by the installed kernel, not external-memory DMA.
 * Sends only DCS SET_COLUMN_ADDRESS and SET_PAGE_ADDRESS for the full panel.
 * Restores IRQ masks and disables/resets the temporary FIFO on exit. No PHY,
 * power, pixel format, controller reset or non-display command is exposed. */
int xpl_dsi_window_full(const xpl_dsi_window_io *io,uint32_t poll_limit,
                        xpl_dsi_window_diag *diag);
#endif
