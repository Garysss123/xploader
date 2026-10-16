/* SPDX-License-Identifier: MIT */
#include "xpl_sd_fast.h"
static int rd(const xpl_sd_board_io *io,uint64_t a,unsigned w,uint32_t *v) {
    return io->read(io->ctx,a,w,v)?XPL_SD_EIO:0;
}
static int wr(const xpl_sd_board_io *io,uint64_t a,unsigned w,uint32_t v) {
    return io->write(io->ctx,a,w,v)?XPL_SD_EIO:0;
}
static int update(const xpl_sd_board_io *io,uint64_t a,uint32_t mask,uint32_t v) {
    uint32_t old;int e=rd(io,a,4,&old);return e?e:wr(io,a,4,(old&~mask)|(v&mask));
}
static int poll(const xpl_sd_board_io *io,uint64_t a,unsigned w,uint32_t mask,uint32_t value,uint32_t limit,uint32_t *last) {
    uint64_t start=io->now_us(io->ctx);
    for(uint32_t i=0;i<limit;i++) {
        int e=rd(io,a,w,last);if(e)return e;
        if((*last&mask)==value)return 0;
        uint64_t now=io->now_us(io->ctx);
        if(now<start||now-start>=100000)return XPL_SD_ETIMEOUT;
        if(io->relax)io->relax(io->ctx);
    }
    return XPL_SD_ETIMEOUT;
}
int xpl_sd_clock_9m6(const xpl_sd_board_io *io,uint32_t limit,uint32_t max,xpl_sd_fast_diag *d) {
    int e;uint32_t v;
    if(!io||!io->read||!io->write||!io->now_us||!d||!limit||limit>1000000u)return XPL_SD_EINVAL;
    *d=(xpl_sd_fast_diag){0};if(max<9600000u)return XPL_SD_EPREP;
    if((e=rd(io,0xf98a4924u,4,&v)))return e;
    if(v&3u)return XPL_SD_EPREP;
    d->phase=1;
    if((e=wr(io,0xf98a492cu,2,3u))||
       (e=wr(io,0xfc400518u,4,0))||(e=wr(io,0xfc40051cu,4,0))||(e=wr(io,0xfc400520u,4,0))||
       (e=update(io,0xfc400514u,0x371fu,3u))||(e=update(io,0xfc400510u,1u,1u))||
       (e=poll(io,0xfc400510u,4,1u,0,limit,&v)))return e;
    d->phase=2;
    if((e=rd(io,0xfc400514u,4,&d->cfg))||(e=rd(io,0xfc400518u,4,&d->m))||
       (e=rd(io,0xfc40051cu,4,&d->n))||(e=rd(io,0xfc400520u,4,&d->d)))return e;
    if((d->cfg&0x371fu)!=3u||d->m||d->n||d->d)return XPL_SD_EHOST;
    if((e=wr(io,0xf98a492cu,2,1u))||(e=poll(io,0xf98a492cu,2,2u,2u,limit,&d->clock))||
       (e=wr(io,0xf98a492cu,2,7u))||(e=rd(io,0xf98a492cu,2,&d->clock)))return e;
    if((d->clock&7u)!=7u)return XPL_SD_EHOST;
    d->phase=3;return 0;
}
