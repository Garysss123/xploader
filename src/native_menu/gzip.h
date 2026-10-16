/* SPDX-License-Identifier: MIT */
#ifndef XPL_GZIP_H
#define XPL_GZIP_H
#include <stddef.h>
enum {XPL_GZIP_OK=0,XPL_GZIP_ARGUMENT=1,XPL_GZIP_FORMAT=2,XPL_GZIP_CAPACITY=3,XPL_GZIP_CORRUPT=4,XPL_GZIP_STOPPED=5};
/* One bounded gzip member; appended DTB bytes are returned via member_bytes.
 * CRC/ISIZE are compressed-format integrity, not a ROM identity/whitelist.
 * No allocation. Caller discards the entire output on any failure. */
int xpl_gzip_decode(const void *source,size_t bytes,void *output,size_t capacity,
                     size_t *written,size_t *member_bytes,int (*progress)(void *),void *context);
#endif
