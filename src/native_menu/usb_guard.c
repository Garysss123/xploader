/* SPDX-License-Identifier: MIT. Watchdog register facts: g8-source-004. */
#include "usb_guard.h"
static int get(const struct xpl_hw_io *io,uint64_t a,uint32_t *v){return io->read32(io->context,a,v)?0:-2;}
static int put(const struct xpl_hw_io *io,uint64_t a,uint32_t v,xpl_usb_guard *d){
    if(!(a==0xfe87f65cu||a==0xf9017004u||a==0xf9017008u||a==0xf9017010u||a==0xf9017014u))return -1;
    d->writes++;if(!io->write32(io->context,a,v))return -2;io->fence(io->context);return 0;
}
int xpl_usb_guard_disarm(const struct xpl_hw_io *io,xpl_usb_guard *d){
    if(!io||!io->read32||!io->write32||!io->fence||!d||d->phase<2)return -1;
    int e=0;
    if(put(io,0xf9017008u,0,d))e=-2;
    if(put(io,0xf9017010u,d->old_bark,d))e=-2;
    if(put(io,0xf9017014u,d->old_bite,d))e=-2;
    if(put(io,0xf9017004u,1,d))e=-2;
    if(put(io,0xf9017008u,d->old_en,d))e=-2;
    if(put(io,0xfe87f65cu,d->old_reason,d))e=-2;
    uint32_t value;
    if(get(io,0xf9017008u,&value)||value!=d->old_en)e=-5;
    if(get(io,0xf9017010u,&value)||value!=d->old_bark)e=-5;
    if(get(io,0xf9017014u,&value)||value!=d->old_bite)e=-5;
    if(get(io,0xfe87f65cu,&value)||value!=d->old_reason)e=-5;
    d->armed=0;d->restore_result=e;d->phase=4;return e;
}
int xpl_usb_guard_arm(const struct xpl_hw_config *c,const struct xpl_hw_io *io,xpl_usb_guard *d){
    xpl_service_snapshot s;uint32_t cookie0,cookie1,value;
    if(!d)return -1;*d=(xpl_usb_guard){.result=-1};
    if(!c||c->watchdog.base!=0xf9017000u||c->watchdog.bytes!=0x1000u||
       !io||!io->read32||!io->write32||!io->fence)return -1;
    if(xpl_service_probe(c,io,1000,&s))return d->result=-2;
    for(unsigned i=0;i<2;i++)if(s.pon_result[i]||(s.pon[i]&255u)!=(i?10u:9u)||
       ((s.pon[i]>>24)&15u)!=1u||!((s.pon[i]>>32)&128u))return d->result=-3;
    if(get(io,0xfe87f000u,&cookie0)||get(io,0xfe87f004u,&cookie1)||cookie0||cookie1)return d->result=-4;
    if(get(io,0xf9017008u,&d->old_en)||get(io,0xf9017010u,&d->old_bark)||
       get(io,0xf9017014u,&d->old_bite)||get(io,0xfe87f65cu,&d->old_reason))return d->result=-2;
    if(d->old_en&~3u)return d->result=-3;
    d->phase=2;
    if(put(io,0xfe87f65cu,0x77665500u,d)||get(io,0xfe87f65cu,&value)||value!=0x77665500u||
       put(io,0xf9017008u,0,d)||put(io,0xf9017004u,1,d)||
       put(io,0xf9017010u,15u*32765u,d)||put(io,0xf9017014u,20u*32765u,d)||
       put(io,0xf9017008u,1,d)||get(io,0xf9017008u,&value)||!(value&1u)) {
        d->result=-2;(void)xpl_usb_guard_disarm(io,d);return -2;
    }
    d->phase=3;d->armed=1;return d->result=0;
}
