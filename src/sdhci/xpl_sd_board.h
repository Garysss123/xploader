/* SPDX-License-Identifier: MIT */
#ifndef XPL_SD_BOARD_H
#define XPL_SD_BOARD_H
#include "xpl_sdhci.h"
typedef struct {
    void *ctx;
    /* Zero means completed ordered access; caller catches synchronous aborts. */
    int (*read)(void *ctx,uint64_t address,unsigned width,uint32_t *value);
    int (*write)(void *ctx,uint64_t address,unsigned width,uint32_t value);
    uint64_t (*now_us)(void *ctx);
    void (*relax)(void *ctx);
} xpl_sd_board_io;
typedef struct {
    uint32_t gcc_apps,gcc_ahb,rcgr_cmd,rcgr_cfg,m,n,d,pads;
} xpl_sd_board_snapshot;

/* Low-level identity/state collected by a separately reviewed SPMI transport.
 * PM8994 is SID0 subtype9; L13/L21 live on SID1 at4c00/5400. */
typedef struct {
    uint8_t pmic_subtype,type,subtype,range,selector,mode,enable;
} xpl_sd_ldo_state;
typedef struct {uint8_t sid,value;uint16_t address;} xpl_sd_pmic_write;

/* Read-only GCC/pad snapshot: no PMIC, core or host-register access. */
int xpl_sd_board_probe(const xpl_sd_board_io *io,xpl_sd_board_snapshot *out);
/* Fixed read-only PMIC snapshot through arbiter-v1 channel0/EE0. Command writes
 * request reads only: SID0:0105 identity and SID1 L13/L21 type/configuration.
 * No generic PMIC access API and no PMIC configuration write transport.
 * Caller serializes against key reads/other SPMI channel users. */
int xpl_sd_board_pmic_snapshot(const xpl_sd_board_io *io,uint64_t core_base,
                              uint32_t core_bytes,uint32_t channel,uint32_t ee,
                              uint32_t poll_limit,xpl_sd_ldo_state *l13,
                              xpl_sd_ldo_state *l21);
/* Executable known clock/pad subset only. Preconditions are established by
 * external board work: exclusive host, safe interconnect, live19.2MHz XO,
 * stable2.95V I/O rails and already-running/safe-to-stop card clock.
 * Success proves only clock/pad register programming. Returns CLOCKS|PINS,
 * never PREP_ALL. It leaves SD card-clock output stopped for later core init.
 * No reset, PMIC, power handshake, card-presence or whole-board readiness claim. */
enum {XPL_SD_BOARD_EXCLUSIVE=1,XPL_SD_BOARD_BUS_SAFE=2,XPL_SD_BOARD_XO_19M2=4,
      XPL_SD_BOARD_RAILS_STABLE=8,XPL_SD_BOARD_HC_ACCESSIBLE=16,
      XPL_SD_BOARD_CLOCK_PAD_PREREQS=31};
int xpl_sd_board_clock_pads(const xpl_sd_board_io *io,uint32_t prerequisites,
                           uint32_t poll_limit,uint32_t *completed);
/* Pure non-executing LDO register plan. Refuses enabled, hardware-controlled,
 * bypass/non-PLDO/unknown regulators. This does NOT establish RPM ownership,
 * analog settling, parent supply readiness or authorize SPMI writes. The plan
 * is useful only after those unresolved platform conditions are established. */
int xpl_sd_ldo_plan_2950(unsigned ldo,const xpl_sd_ldo_state *state,
                        xpl_sd_pmic_write out[4]);
int xpl_sd_ldo_is_2950(const xpl_sd_ldo_state *state);
#endif
