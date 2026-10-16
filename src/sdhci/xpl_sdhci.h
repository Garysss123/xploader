/* SPDX-License-Identifier: MIT
 * SDv2/SDHC/SDXC PIO protocol over a PREPARED Sony SDHC2.
 * No GPIO, regulator, GCC, vendor/core-mode or DMA implementation.
 */
#ifndef XPL_SDHCI_H
#define XPL_SDHCI_H
#include <stddef.h>
#include <stdint.h>

#define XPL_SDHC2_BASE UINT64_C(0xf98a4900)
#define XPL_SDHC2_SPAN 0x11cu
#define XPL_SD_MAX_READ_BLOCKS 128u

enum {
    XPL_SD_OK=0, XPL_SD_EINVAL=-1, XPL_SD_EIO=-2,
    XPL_SD_ETIMEOUT=-3, XPL_SD_EPREP=-4, XPL_SD_EHOST=-5,
    XPL_SD_ECARD=-6, XPL_SD_EUNSUPPORTED=-7, XPL_SD_EBOUNDS=-8
};

/* Byte offsets, widths 1/2/4. Return zero only on a completed ordered access.
 * Board callbacks must confine accesses to this validated SDHC2 window and
 * catch synchronous data aborts; no raw pointer dereference occurs here.
 * now_us is monotonic. Even a frozen timer is bounded by poll_limit. */
typedef struct {
    void *ctx;
    int (*read)(void *ctx,uint32_t offset,unsigned width,uint32_t *value);
    int (*write)(void *ctx,uint32_t offset,unsigned width,uint32_t value);
    uint64_t (*now_us)(void *ctx);
    void (*relax)(void *ctx);
} xpl_sdhci_io;

enum {
    XPL_SD_PREP_CLOCKS=1u<<0,       /* AHB/core/interconnect clocks known live */
    XPL_SD_PREP_POWER_2950=1u<<1,  /* VDD L21 and I/O L13 stable near 2.95V */
    XPL_SD_PREP_PINS=1u<<2,        /* active SDC2 pads, CMD/DATA pull-ups */
    XPL_SD_PREP_HC_MODE=1u<<3,     /* Qcom core HC_MODE and reset prerequisites */
    XPL_SD_PREP_PWR_HANDSHAKE=1u<<4,/* Qcom BUS_ON / IO_HIGH acknowledged */
    XPL_SD_PREP_LEGACY=1u<<5,      /* 1-bit SDR12, no 1.8V/DLL/DDR assumptions */
    XPL_SD_PREP_CARD_PRESENT=1u<<6,/* board detector; SDHCI CD bit is broken */
    XPL_SD_PREP_EXCLUSIVE=1u<<7,   /* no S1/IRQ/DMA/other owner using this host */
    XPL_SD_PREP_ALL=0xffu
};
typedef struct {
    uint32_t verified;
    uint32_t card_clock_hz; /* measured/established by board HAL, 100k..400k */
} xpl_sd_prepared;

typedef struct {
    uint32_t present_state,capabilities,capabilities1;
    uint16_t version,clock_control,host_control2;
    uint8_t host_control,power_control,software_reset;
} xpl_sdhci_snapshot;

typedef struct {
    xpl_sdhci_io io;
    uint32_t poll_limit,last_interrupt,last_response;
    uint64_t operation_start,operation_us,blocks512;
    uint16_t rca;
    uint32_t cid[4]; /* canonical136-bit response after the SDHCI CRC-byte shift */
    uint32_t max_clock_hz;
    uint32_t data_commands,multi_commands,blocks_read,blocks_written;
    uint8_t bound,card_ready;
    int last_error;
} xpl_sd_host;

int xpl_sdhci_bind(xpl_sd_host *host,const xpl_sdhci_io *io,
                  uint64_t physical_base,uint32_t span,uint32_t poll_limit);
/* Read-only snapshot. It does NOT establish board readiness/card identity. */
int xpl_sdhci_probe(xpl_sd_host *host,xpl_sdhci_snapshot *out);
/* Performs volatile SD protocol initialization only AFTER board preparation.
 * No standard POWER/CLOCK or Qcom vendor writes are made. Card reset CMD0,
 * CMD8, ACMD41, CID/RCA/CSD/select/status; SDv1/SDSC/MMC are unsupported. */
int xpl_sd_initialize(xpl_sd_host *host,const xpl_sd_prepared *prepared);
/* API matches xpl_exfat_read_fn. Whole read succeeds with zero, else caller
 * must discard partial output. No card changes/removal during a mounted FS.
 * Only single-block CMD17 PIO; count bounded, no DMA/multiblock/write commands. */
int xpl_sd_read512(void *host,uint64_t lba512,uint32_t count512,void *out);
/* Explicit experimental read-only CMD18 PIO + automatic CMD12. Same ranges
 * and timeout bounds; one-block reads retain CMD17. No DMA or bus-width change.
 * On any error the caller discards the entire partial output and reinitializes.
 */
int xpl_sd_read512_multiblock(void *host,uint64_t lba512,uint32_t count512,void *out);
/* Explicit synchronous CMD24 only, for an exclusively owned USB SD session.
 * Each sector completes DATA_END, releases DAT0 and reports TRAN/READY via
 * CMD13 before success. Errors invalidate card_ready; do not retry in place.
 * This API cannot bind to eMMC: xpl_sdhci_bind accepts SDHC2 only. */
int xpl_sd_write512(void *host,uint64_t lba512,uint32_t count512,const void *data);

#endif
