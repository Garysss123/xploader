/* SPDX-License-Identifier: MIT */
#ifndef XPL_FDT_PAYLOAD_H
#define XPL_FDT_PAYLOAD_H
#include <stddef.h>
#include <stdint.h>
enum { XPL_FDT_PAYLOAD_OK=0, XPL_FDT_PAYLOAD_ARGUMENT=1,
       XPL_FDT_PAYLOAD_TREE=2, XPL_FDT_PAYLOAD_CHOSEN=3,
       XPL_FDT_PAYLOAD_ARGS=4, XPL_FDT_PAYLOAD_CAPACITY=5 };
typedef struct {
    size_t total_bytes, old_args_bytes, args_bytes;
    unsigned initrd_cells;
} xpl_fdt_payload_diag;
/* Prepare a disposable, non-overlapping clone. Source is never mutated.
 * Require one chosen node and one existing bootargs/start/end property.
 * Header arguments override matching inherited keys; the caller-supplied
 * mode overrides both. Preserve all other properties and reservations.
 * Failure may modify the clone, which must be discarded. */
int xpl_fdt_payload_prepare(const void *source,size_t source_bytes,
    void *clone,size_t capacity,const char *header_args,size_t header_bytes,
    const char *mode,uint64_t initrd_start,uint64_t initrd_bytes,
    xpl_fdt_payload_diag *diag);
/* First folder-backed Android adapter: select a bundle member matching live
 * qcom MSM/board tuples, transplant its six system-fstab properties into the
 * disposable inherited tree. Keep S1 RAM/reservations/calibration. Source
 * bundle is disposable staging RAM; validation may scrub its charging args.
 * Only equal-size/smaller replacements are supported; no physical writes. */
int xpl_fdt_payload_system_overlay(void *clone,size_t clone_bytes,
    void *bundle,size_t bundle_bytes,unsigned *selected_index);
/* Add the selected SD directory to the disposable chosen tree. */
int xpl_fdt_payload_directory(void *clone,size_t capacity,const char *path,
                              size_t *total_bytes);
#endif
