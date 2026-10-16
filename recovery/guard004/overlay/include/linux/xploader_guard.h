/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_XPLOADER_GUARD_H
#define _LINUX_XPLOADER_GUARD_H
struct bio;
#ifdef CONFIG_XPLOADER_RECOVERY_GUARD_E6653
int xpl_mmc_guard_bio(struct bio *bio);
#else
static inline int xpl_mmc_guard_bio(struct bio *bio) { return 0; }
#endif
#endif
