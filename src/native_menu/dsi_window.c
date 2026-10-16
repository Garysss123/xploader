/* SPDX-License-Identifier: MIT */
#include "dsi_window.h"
static int rd(const xpl_dsi_window_io *io,uint32_t o,uint32_t *v) {
    switch(o) {case 0:case 4:case 8:case 0x3c:case 0x58:case 0x5c:case 0x84:case 0x110:case 0x15c:break;
    default:return XPL_DSI_WINDOW_ARGUMENT;}
    return io->read(io->ctx,o,v)?XPL_DSI_WINDOW_IO:0;
}
static int wr(const xpl_dsi_window_io *io,uint32_t o,uint32_t v) {
    switch(o) {case 0x4c:case 0x90:case 0x110:case 0x15c:case 0x17c:case 0x1ec:break;
    default:return XPL_DSI_WINDOW_ARGUMENT;}
    return io->write(io->ctx,o,v)?XPL_DSI_WINDOW_IO:0;
}
static int poll(const xpl_dsi_window_io *io,uint32_t o,uint32_t mask,uint32_t expected,uint32_t limit) {
    uint64_t start=io->now_us(io->ctx),now;uint32_t value;int e;
    for(uint32_t i=0;i<limit;i++) {
        if((e=rd(io,o,&value)))return e;
        if((value&mask)==expected)return 0;
        now=io->now_us(io->ctx);
        if(now<start||now-start>=100000)return XPL_DSI_WINDOW_TIMEOUT;
        if(io->relax)io->relax(io->ctx);
    }
    return XPL_DSI_WINDOW_TIMEOUT;
}
int xpl_dsi_window_full(const xpl_dsi_window_io *io,uint32_t limit,xpl_dsi_window_diag *d) {
    uint32_t ctrl,stream,total,tpg,irq;int e=0,cleanup=0;unsigned started=0;
    if(!io||!io->read||!io->write||!io->now_us||!io->fence||!d||!limit||limit>1000000)
        return XPL_DSI_WINDOW_ARGUMENT;
    d->commands=0;d->revision=d->dma_control=d->trigger_control=d->tpg_control=0;
    if((e=rd(io,0,&d->revision))||(e=rd(io,4,&ctrl))||
       (e=rd(io,0x3c,&d->dma_control))||(e=rd(io,0x84,&d->trigger_control))||
       (e=rd(io,0x58,&stream))||(e=rd(io,0x5c,&total))||
       (e=rd(io,0x15c,&tpg))||(e=rd(io,0x110,&irq)))return e;
    d->tpg_control=tpg;
    if(d->revision!=0x10030000u||(ctrl&7u)!=5u||d->dma_control!=0x14000000u||
       (d->trigger_control&7u)!=4u||stream!=0x0ca90039u||total!=0x07800438u||(tpg&~0x30004u))
        return XPL_DSI_WINDOW_UNSUPPORTED;
    if((e=poll(io,8,3u,0,limit)))return e;
    for(unsigned command=0;command<2;command++) {
        /* DCS long-write length5: command, start0, inclusive full-panel end.
         * Four FIFO dwords satisfy the driver's even-dword rule; DMA length12
         * transmits the header+five bytes+padding, not the final zero dword. */
        const uint32_t words[4]={0xc0390005u,command?0x0700002bu:0x0400002au,
                               command?0xffffff7fu:0xffffff37u,0};
        started=1;
        if((e=wr(io,0x15c,0x30006u)))break;
        for(unsigned i=0;i<4;i++)if((e=wr(io,0x17c,words[i])))break;
        if(e)break;
        io->fence(io->ctx);
        if((e=wr(io,0x110,(irq&0x02020202u)|3u))||(e=wr(io,0x4c,12u)))break;
        io->fence(io->ctx);
        if((e=wr(io,0x90,1u))||(e=poll(io,0x110,1u,1u,limit)))break;
        d->commands++;
        if((e=wr(io,0x110,(irq&0x02020202u)|1u))||
           (e=wr(io,0x1ec,1u))||(e=wr(io,0x1ec,0u)))break;
    }
    if(started) {
        /* Same FIFO cleanup on completion or timeout; masks never enable a
         * new unrelated interrupt, and the native caller has DAIF masked. */
        int a=wr(io,0x1ec,1u),b=wr(io,0x1ec,0u),c=wr(io,0x15c,tpg);
        int mask=wr(io,0x110,irq&0x02020202u);
        if(a||b||c||mask)cleanup=XPL_DSI_WINDOW_IO;
        io->fence(io->ctx);
    }
    return e?e:cleanup;
}
