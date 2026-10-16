/* SPDX-License-Identifier: MIT */
#include "xpl_emmc_image.h"
int xpl_emmc_load_android_v0(xpl_emmc_reader *h,uint64_t first,uint32_t sectors,
    void *destination,size_t capacity,xpl_emmc_image_diag *d) {
    uint8_t *p=destination;uint64_t start,now;int e;
    struct xpl_payload_error error;
    if(!h||!h->ready||!h->diag||!d||!destination||capacity<4096u||sectors<8u)return XPL_SD_EINVAL;
    *d=(xpl_emmc_image_diag){0};
    if(first>=h->diag->capacity_sectors||sectors>h->diag->capacity_sectors-first)return XPL_SD_EBOUNDS;
    start=h->io.now_us(h->io.ctx);if(start==UINT64_MAX)return XPL_SD_ETIMEOUT;
    if((e=xpl_emmc_read512(h,first,8u,p)))return e;
    if(xpl_payload_parse_android_v0(p,4096u,(uint64_t)sectors*512u,&d->layout,&error)||d->layout.second.bytes)return XPL_SD_EUNSUPPORTED;
    uint64_t bytes=d->layout.padded_payload_end;
    if(bytes<4096u||bytes>capacity||bytes%512u)return XPL_SD_EBOUNDS;
    for(uint64_t position=4096u;position<bytes;) {
        now=h->io.now_us(h->io.ctx);
        if(now<start||now==UINT64_MAX||now-start>=120000000u)return XPL_SD_ETIMEOUT;
        uint32_t n=(uint32_t)((bytes-position)/512u);if(n>128u)n=128u;
        if((e=xpl_emmc_read512(h,first+position/512u,n,p+(size_t)position)))return e;
        position+=(uint64_t)n*512u;
    }
    uint64_t hash=UINT64_C(14695981039346656037);
    for(size_t i=0;i<(size_t)bytes;i++) {
        hash=(hash^p[i])*UINT64_C(1099511628211);
        if(!(i&65535u)) {
            now=h->io.now_us(h->io.ctx);
            if(now<start||now==UINT64_MAX||now-start>=120000000u)return XPL_SD_ETIMEOUT;
        }
    }
    now=h->io.now_us(h->io.ctx);if(now<start||now==UINT64_MAX)return XPL_SD_ETIMEOUT;
    d->bytes=bytes;d->fingerprint=hash;d->elapsed_us=now-start;return 0;
}
