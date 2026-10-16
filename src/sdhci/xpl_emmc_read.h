/* SPDX-License-Identifier: MIT */
#ifndef XPL_EMMC_READ_H
#define XPL_EMMC_READ_H
#include "xpl_emmc_probe.h"
typedef struct {
    unsigned phase,commands,blocks,restored;
    uint32_t last_command,last_irq,last_response,capacity_sectors;
    uint32_t saved_enable,saved_signal,saved_arg;
    uint32_t saved_timeout,saved_size,saved_count,saved_transfer;
    uint32_t saved_clock,clock_active,reset_after,vendor,dll_config,dll_status,core_power,core_version;
    uint32_t partition_config,bus_width,hs_timing,ext_revision,data_sector_size;
    uint32_t core_before,core_active,core_after;
    uint32_t pll_vote_before,pll_vote_active,pll_vote_after,pll_mode_before,pll_mode_active,pll_mode_after,bcr;
    uint32_t saved_host,saved_power,saved_host2,saved_vendor,saved_dll;
    uint32_t saved_cfg,saved_m,saved_n,saved_d,init_cfg,init_power_irq,init_core_reset;
    uint32_t ocr,cid[4],csd[4],op_cond_tries,card_reinitialized;
    uint32_t max_clock_hz,fast_cfg,fast_clock;
    int restore_result;
} xpl_emmc_read_diag;
typedef struct {
    xpl_sd_board_io io;
    xpl_emmc_read_diag *diag;
    uint32_t limit;
    unsigned started,ready,core_started,pll_started,fresh_started;
} xpl_emmc_reader;
/* Initial read-only adapter: inherited, selected RCA1, 8-bit HS200 card.
 * Rejects unearned clock/tuning/power modes. It neither initializes the card
 * nor sends SWITCH, write, erase, reset-to-idle or partition-switch commands.
 * Status + EXT_CSD must establish TRAN/READY, 512-byte sectors and user area
 * before block reads are permitted. Returning paths restore static host
 * interrupt/timeout/block/argument configuration. IRQ completion latches are
 * acknowledged; command/response registers are intentionally not replayed.
 * Caller owns the host and must always call close after an attempted begin. */
int xpl_emmc_reader_begin(xpl_emmc_reader *h,const xpl_sd_board_io *io,
                          const xpl_emmc_snapshot *in,uint32_t limit,xpl_emmc_read_diag *d);
/* Same read-only protocol while borrowing only HC_MODE bit0.
 * No full core reset or card reinitialization. Close restores the exact word. */
int xpl_emmc_reader_begin_hc(xpl_emmc_reader *h,const xpl_sd_board_io *io,
                             const xpl_emmc_snapshot *in,uint32_t limit,xpl_emmc_read_diag *d);
/* Adds only the firmware-configured GPLL4 vote if it is unlocked but already
 * in FSM mode. Refuses an asserted SDCC1 reset or another clock parent. No
 * PLL rate/configuration is changed, and close restores only our vote bit. */
int xpl_emmc_reader_begin_hc_pll(xpl_emmc_reader *h,const xpl_sd_board_io *io,
                                 const xpl_emmc_snapshot *in,uint32_t limit,xpl_emmc_read_diag *d);
/* Earn SDHCI startup by reasserting the inherited clock and resetting only
 * command/data host lines. Does not reset the card or change clock frequency. */
int xpl_emmc_reader_begin_primed(xpl_emmc_reader *h,const xpl_sd_board_io *io,
                                const xpl_emmc_snapshot *in,uint32_t limit,xpl_emmc_read_diag *d);
/* Reset/reinitialize the controller and MMC card at 400kHz, 1-bit legacy.
 * Only CMD0/1/2/3/7/9/13/8/17 are emitted, with no media writes, SWITCH,
 * erase, partition selection or permanent configuration. The board must
 * retain the S1-established power rails. Close restores static host/clock
 * settings, NOT the previous card protocol state; Linux must reinitialize. */
int xpl_emmc_reader_begin_fresh(xpl_emmc_reader *h,const xpl_sd_board_io *io,
                               const xpl_emmc_snapshot *in,uint32_t limit,xpl_emmc_read_diag *d);
/* Legacy 9.6MHz from the live 19.2MHz XO. Requires a successfully selected
 * fresh MMC card and CSD TRAN_SPEED >=9.6MHz. No card SWITCH or PLL mutation. */
int xpl_emmc_reader_clock_9m6(xpl_emmc_reader *h);
int xpl_emmc_read512(void *ctx,uint64_t first,uint32_t count,void *out);
int xpl_emmc_reader_close(xpl_emmc_reader *h);
#endif
