/* SPDX-License-Identifier: MIT. Native warm reset; references g8-source-001. */
#include "service_reset.h"
#define REASON 0xfe87f65cu
#define PSHOLD 0xfc4ab000u
#define BOOTLOADER 0x77665500u
#define NORMAL_RESTART 0x77665501u
static int get(const struct xpl_hw_io *io,uint64_t a,uint32_t *v){return io->read32(io->context,a,v)?0:XPL_S1_IO;}
static int put(const struct xpl_hw_io *io,uint64_t a,uint32_t v,xpl_service_reset_diag *d){
    if(!((a==REASON)||(a==PSHOLD&&v==0)))return XPL_S1_ARGUMENT;
    if(!io->write32(io->context,a,v))return XPL_S1_IO;
    d->writes++;io->fence(io->context);return 0;
}
static int trigger(const struct xpl_hw_config *c,const struct xpl_hw_io *io,
    xpl_service_snapshot *s,xpl_service_reset_diag *d,uint32_t reason) {
    uint32_t observed;
    if(!d||!s)return XPL_S1_ARGUMENT;
    *d=(xpl_service_reset_diag){0};
    if(!io||!io->read32||!io->write32||!io->fence)return XPL_S1_ARGUMENT;
    int e=xpl_service_probe(c,io,1000,s);if(e)return e;
    d->phase=1;
    for(unsigned i=0;i<2;i++) {
        uint64_t subtype=s->pon[i]&255u,ctl=(s->pon[i]>>24)&255u,enabled=(s->pon[i]>>32)&255u;
        if(s->pon_result[i]||subtype!=(i?10u:9u)||(ctl&15u)!=1u||!(enabled&128u))return XPL_S1_PON;
    }
    if(s->pshold!=1)return XPL_S1_CHANGED;
    if(get(io,0xfe87f000u,&d->dload0)||get(io,0xfe87f004u,&d->dload1))return XPL_S1_IO;
    if(!xpl_service_dload_idle(d->dload0,d->dload1))return XPL_S1_DLOAD;
    if(get(io,REASON,&d->old_reason))return XPL_S1_IO;
    d->phase=2;
    if(put(io,REASON,reason,d))return XPL_S1_IO;
    d->phase=3;
    if(get(io,REASON,&observed)||observed!=reason) {
        (void)put(io,REASON,d->old_reason,d);return XPL_S1_CHANGED;
    }
    if(put(io,PSHOLD,0,d)) {
        (void)put(io,REASON,d->old_reason,d);return XPL_S1_IO;
    }
    d->phase=4;
    return XPL_S1_TRIGGERED;
}
static int returned(const struct xpl_hw_io *io,xpl_service_reset_diag *d,uint32_t expected) {
    uint32_t reason;
    if(!io||!io->read32||!io->write32||!io->fence||!d||d->phase!=4)return XPL_S1_ARGUMENT;
    if(get(io,REASON,&reason))return XPL_S1_IO;
    if(reason!=expected)return XPL_S1_CHANGED;
    if(put(io,REASON,d->old_reason,d))return XPL_S1_IO;
    d->phase=5;return XPL_S1_RETURNED;
}
int xpl_service_fastboot_trigger(const struct xpl_hw_config *c,const struct xpl_hw_io *io,xpl_service_snapshot *s,xpl_service_reset_diag *d){return trigger(c,io,s,d,BOOTLOADER);}
int xpl_service_restart_trigger(const struct xpl_hw_config *c,const struct xpl_hw_io *io,xpl_service_snapshot *s,xpl_service_reset_diag *d){return trigger(c,io,s,d,NORMAL_RESTART);}
int xpl_service_reset_returned(const struct xpl_hw_io *io,xpl_service_reset_diag *d){return returned(io,d,BOOTLOADER);}
int xpl_service_restart_returned(const struct xpl_hw_io *io,xpl_service_reset_diag *d){return returned(io,d,NORMAL_RESTART);}
