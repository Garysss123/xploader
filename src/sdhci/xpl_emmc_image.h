/* SPDX-License-Identifier: MIT */
#ifndef XPL_EMMC_IMAGE_H
#define XPL_EMMC_IMAGE_H
#include "xpl_emmc_read.h"
#include "../payload/xpl_payload.h"
typedef struct {
    uint64_t bytes,fingerprint,elapsed_us;
    struct xpl_android_v0_layout layout;
} xpl_emmc_image_diag;
/* Read one Androidv0 image from a bounded sector range into caller-owned RAM.
 * Header addresses are never followed. No write, hash registration or ROM
 * model allowlist. Whole-image fingerprint is retrospective evidence only.
 * On any failure all output is invalid; no kernel handoff is performed. */
int xpl_emmc_load_android_v0(xpl_emmc_reader *h,uint64_t first,uint32_t sectors,
    void *destination,size_t capacity,xpl_emmc_image_diag *d);
#endif
