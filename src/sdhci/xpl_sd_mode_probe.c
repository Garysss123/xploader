/* SPDX-License-Identifier: MIT */
#include "xpl_sd_mode_probe.h"
#define CMD UINT64_C(0xfc4cf800)
#define STATUS UINT64_C(0xfc4cf808)
#define WDATA UINT64_C(0xfc4cf810)
#define RDATA UINT64_C(0xfc4cf818)
static int transaction(const xpl_sd_board_io *io,uint32_t command,uint32_t limit,uint32_t *status) {
    if(io->write(io->ctx,CMD,4,command))return XPL_SD_EIO;
    uint64_t start=io->now_us(io->ctx);
    for(uint32_t i=0;i<limit;i++) {
        if(io->read(io->ctx,STATUS,4,status))return XPL_SD_EIO;
        if(*status&14u)return XPL_SD_EHOST;
        if(*status&1u)return 0;
        uint64_t now=io->now_us(io->ctx);
        if(now<start||now-start>=10000)return XPL_SD_ETIMEOUT;
        if(io->relax)io->relax(io->ctx);
    }
    return XPL_SD_ETIMEOUT;
}
static int mode_read(const xpl_sd_board_io *io,uint32_t limit,unsigned *value) {
    uint32_t status,data;int e=transaction(io,0x0814c450u,limit,&status);
    if(e)return e;
    if(io->read(io->ctx,RDATA,4,&data))return XPL_SD_EIO;
    *value=data&255u;return 0;
}
static int mode_write(const xpl_sd_board_io *io,uint32_t limit,unsigned value,
                       xpl_sd_mode_snapshot *s,uint32_t *status) {
    if(value!=0&&value!=128u)return XPL_SD_EINVAL;
    if(io->write(io->ctx,WDATA,4,value))return XPL_SD_EIO;
    s->write_commands++;
    return transaction(io,0x0014c450u,limit,status); /* EXT_WRITEL0, SID1, length1 */
}
int xpl_sd_with_hpm(const xpl_sd_board_io *io,uint32_t limit,
                    int (*operation)(void *),void *context,xpl_sd_mode_snapshot *s) {
    xpl_sd_ldo_state a,b;int e=0;unsigned current;uint32_t status=0;
    if(!io||!io->read||!io->write||!io->now_us||!s||!limit||limit>1000000u)return XPL_SD_EINVAL;
    *s=(xpl_sd_mode_snapshot){0};
    if((e=xpl_sd_board_pmic_snapshot(io,0xfc4cf000u,0x1000,0,0,limit>1000u?1000u:limit,&a,&b)))return e;
    if(a.type!=4||a.subtype!=9||a.range!=3||a.selector!=58||a.enable!=128u||
       (a.mode!=0&&a.mode!=128u))return XPL_SD_EPREP;
    s->before=a.mode;
    e=mode_write(io,limit,128u,s,&s->write_status);
    if(mode_read(io,limit,&s->active)) {e=XPL_SD_EIO;goto restore;}
    s->modified=s->active!=s->before;
    if(!e&&s->active!=128u)e=XPL_SD_EHOST;
    if(!e) {
        uint64_t start=io->now_us(io->ctx),now=start;unsigned settled=0;
        /* SPMI polls and a pure counter spin have different costs. A fast
         * counter callback must not exhaust the bus poll budget before2ms. */
        uint64_t hold_limit=(uint64_t)limit*1000u;
        if(hold_limit>1000000u)hold_limit=1000000u;
        for(uint32_t i=0;i<hold_limit;i++) {
            now=io->now_us(io->ctx);
            if(now<start)break;
            if(now-start>=2000) {settled=1;break;}
            if(io->relax)io->relax(io->ctx);
        }
        s->hold_us=now>=start?now-start:0;
        if(!settled)e=XPL_SD_ETIMEOUT;
        if(!e&&(mode_read(io,limit,&current)||current!=128u))e=XPL_SD_EHOST;
        if(!e&&operation)e=operation(context);
    }
restore:
    if(mode_read(io,limit,&current))s->restore_result=XPL_SD_EIO;
    else if(current!=s->before) {
        /* Do not overwrite an unrelated hardware/remote mode transition. */
        if(current!=128u)s->restore_result=XPL_SD_EPREP;
        else s->restore_result=mode_write(io,limit,s->before,s,&status);
    }
    if(mode_read(io,limit,&s->after))s->restore_result=XPL_SD_EIO;
    if(!s->restore_result&&s->after!=s->before)s->restore_result=XPL_SD_EHOST;
    s->restored=!s->restore_result;
    return e?e:s->restore_result;
}
int xpl_sd_mode_probe(const xpl_sd_board_io *io,uint32_t limit,xpl_sd_mode_snapshot *s) {
    return xpl_sd_with_hpm(io,limit,0,0,s);
}
