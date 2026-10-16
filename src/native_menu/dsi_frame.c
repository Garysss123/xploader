/* SPDX-License-Identifier: MIT */
#include "dsi_frame.h"
/* Pinned downstream mdss_dsi.h: paired W1C status/IRQ mask bits. Preserve all
 * named masks including deskew and dynamic refresh; never clear their status. */
#define MASKS 0xa2220202u
#define DONE 0x100u
#define DONE_MASK 0x200u
static int valid(const xpl_dsi_window_io *io) {
    return io&&io->read&&io->write&&io->now_us&&io->fence;
}
static int rd(const xpl_dsi_window_io *io,uint32_t offset,uint32_t *v) {
    return io->read(io->ctx,offset,v)?XPL_DSI_WINDOW_IO:0;
}
static int wr(const xpl_dsi_window_io *io,uint32_t offset,uint32_t v) {
    return io->write(io->ctx,offset,v)?XPL_DSI_WINDOW_IO:0;
}
int xpl_dsi_frame_begin(const xpl_dsi_window_io *io,xpl_dsi_frame_diag *d) {
    uint32_t rev,ctrl,format,stream,total,dcs,irq;int e;
    if(!valid(io)||!d||d->active)return XPL_DSI_WINDOW_ARGUMENT;
    if((e=rd(io,0,&rev))||(e=rd(io,4,&ctrl))||(e=rd(io,0x40,&format))||
       (e=rd(io,0x44,&dcs))||(e=rd(io,0x58,&stream))||
       (e=rd(io,0x5c,&total))||(e=rd(io,0x110,&irq)))return e;
    if(!d->frames)d->initial_dcs=dcs;
    d->current_dcs=dcs;d->last_irq=irq;
    if(rev!=0x10030000u||(ctrl&7u)!=5u||format!=8u||
       stream!=0x0ca90039u||total!=0x07800438u||
       (dcs!=0x13c2cu&&dcs!=0x13c3cu))return XPL_DSI_WINDOW_UNSUPPORTED;
    if(dcs!=0x13c2cu) {
        if((e=wr(io,0x44,0x13c2cu)))return e;
        d->current_dcs=0x13c2cu;
    }
    d->saved_masks=irq&MASKS;
    /* Native DAIF remains masked. Enabling only the owned status mask makes
     * completion observable even on firmware that gates its status latch. */
    if((e=wr(io,0x110,d->saved_masks|DONE_MASK|DONE)))return e;
    io->fence(io->ctx);d->active=1;return 0;
}
int xpl_dsi_frame_wait(const xpl_dsi_window_io *io,uint32_t limit,xpl_dsi_frame_diag *d) {
    int e=0;uint64_t start,now;unsigned completed=0;
    if(!valid(io)||!d||!d->active||!limit||limit>1000000u)return XPL_DSI_WINDOW_ARGUMENT;
    start=now=io->now_us(io->ctx);
    for(uint32_t i=0;i<limit;i++) {
        if((e=rd(io,0x110,&d->last_irq)))break;
        now=io->now_us(io->ctx);
        if(now<start) {e=XPL_DSI_WINDOW_TIMEOUT;break;}
        if(d->last_irq&DONE) {completed=1;break;}
        if(now-start>=250000) {e=XPL_DSI_WINDOW_TIMEOUT;break;}
        if(io->relax)io->relax(io->ctx);
    }
    if(!e&&!completed)e=XPL_DSI_WINDOW_TIMEOUT;
    if(!e&&rd(io,8,&d->last_status))e=XPL_DSI_WINDOW_IO;
    int cleanup=wr(io,0x110,d->saved_masks|DONE);
    io->fence(io->ctx);d->active=0;
    if(now>=start&&now-start>d->max_wait_us)d->max_wait_us=now-start;
    if(!e&&!cleanup)d->frames++;
    return e?e:cleanup;
}
