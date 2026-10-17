/* SPDX-License-Identifier: MIT. E6653 WDT facts reused from measured USB/XNU
 * guards; this earlier lifecycle is a new, separately accepted integration. */
#include "entry_watchdog.h"
#include "service_probe.h"
static int valid(const struct xpl_hw_io *io) {
    return io&&io->read32&&io->write32&&io->fence;
}
static int get(const struct xpl_hw_io *io,uint64_t a,uint32_t *v) {
    return io->read32(io->context,a,v)?0:-2;
}
static int put(const struct xpl_hw_io *io,uint64_t a,uint32_t v,xpl_entry_watchdog *d) {
    if(!(a==XPL_ENTRY_WDT_REASON||a==XPL_ENTRY_WDT_RESET||a==XPL_ENTRY_WDT_ENABLE||
         a==XPL_ENTRY_WDT_BARK||a==XPL_ENTRY_WDT_BITE))return -1;
    d->writes++;
    if(!io->write32(io->context,a,v))return -2;
    io->fence(io->context);return 0;
}
int xpl_entry_watchdog_restore(const struct xpl_hw_io *io,xpl_entry_watchdog *d) {
    if(!valid(io)||!d||d->phase<2)return -1;
    int error=0;uint32_t value;
    if(put(io,XPL_ENTRY_WDT_ENABLE,0,d))error=-2;
    if(put(io,XPL_ENTRY_WDT_BARK,d->old_bark,d))error=-2;
    if(put(io,XPL_ENTRY_WDT_BITE,d->old_bite,d))error=-2;
    if(put(io,XPL_ENTRY_WDT_RESET,1,d))error=-2;
    if(put(io,XPL_ENTRY_WDT_ENABLE,d->old_en,d))error=-2;
    if(put(io,XPL_ENTRY_WDT_REASON,d->old_reason,d))error=-2;
    if(get(io,XPL_ENTRY_WDT_ENABLE,&value)||value!=d->old_en)error=-5;
    if(get(io,XPL_ENTRY_WDT_BARK,&value)||value!=d->old_bark)error=-5;
    if(get(io,XPL_ENTRY_WDT_BITE,&value)||value!=d->old_bite)error=-5;
    if(get(io,XPL_ENTRY_WDT_REASON,&value)||value!=d->old_reason)error=-5;
    d->restore_result=error;d->armed=error?2u:0u;d->phase=error?5u:4u;return error;
}
int xpl_entry_watchdog_arm(const struct xpl_hw_io *io,unsigned seconds,xpl_entry_watchdog *d) {
    if(!d)return -1;
    *d=(xpl_entry_watchdog){.phase=1,.result=-1,.restore_result=-1};
    if(!valid(io)||seconds<6||seconds>30)return -1;
    d->timeout_seconds=seconds;
    if(get(io,XPL_ENTRY_DLOAD0,&d->cookie0)||get(io,XPL_ENTRY_DLOAD1,&d->cookie1))return d->result=-2;
    if(!xpl_service_dload_idle(d->cookie0,d->cookie1))return d->result=-4;
    if(get(io,XPL_ENTRY_WDT_ENABLE,&d->old_en)||get(io,XPL_ENTRY_WDT_BARK,&d->old_bark)||
       get(io,XPL_ENTRY_WDT_BITE,&d->old_bite)||get(io,XPL_ENTRY_WDT_REASON,&d->old_reason))return d->result=-2;
    if(d->old_en&~3u)return d->result=-3;
    d->phase=2;uint32_t value;
    if(put(io,XPL_ENTRY_WDT_REASON,XPL_ENTRY_WDT_FASTBOOT,d)||
       get(io,XPL_ENTRY_WDT_REASON,&value)||value!=XPL_ENTRY_WDT_FASTBOOT||
       put(io,XPL_ENTRY_WDT_ENABLE,0,d)||put(io,XPL_ENTRY_WDT_RESET,1,d)||
       put(io,XPL_ENTRY_WDT_BARK,(seconds-5u)*XPL_ENTRY_WDT_HZ,d)||
       put(io,XPL_ENTRY_WDT_BITE,seconds*XPL_ENTRY_WDT_HZ,d)||
       put(io,XPL_ENTRY_WDT_ENABLE,1,d)||get(io,XPL_ENTRY_WDT_ENABLE,&value)||value!=1u||
       get(io,XPL_ENTRY_WDT_BARK,&value)||value!=(seconds-5u)*XPL_ENTRY_WDT_HZ||
       get(io,XPL_ENTRY_WDT_BITE,&value)||value!=seconds*XPL_ENTRY_WDT_HZ) {
        d->result=-2;(void)xpl_entry_watchdog_restore(io,d);return -2;
    }
    d->phase=3;d->armed=1;return d->result=0;
}
static void le32(volatile uint8_t *out,uint32_t v) {
    for(unsigned i=0;i<4;i++)out[i]=(uint8_t)(v>>(8u*i));
}
int xpl_entry_watchdog_publish(const xpl_entry_watchdog *d,volatile uint8_t *p,size_t size) {
    if(!d||!p||size!=XPL_ENTRY_WDT_RECORD_BYTES)return -1;
    static const uint8_t magic[8]={'X','P','L','W','D','T','0','1'};
    for(unsigned i=0;i<8;i++)p[i]=magic[i];
    const uint32_t words[]={1,64,d->phase,d->armed,d->timeout_seconds,(uint32_t)d->result,
        (uint32_t)d->restore_result,d->writes,d->old_en,d->old_bark,d->old_bite,d->old_reason,d->cookie0,d->cookie1};
    for(unsigned i=0;i<14;i++)le32(p+8u+4u*i,words[i]);return 0;
}
