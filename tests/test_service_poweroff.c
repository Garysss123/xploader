/* SPDX-License-Identifier: MIT. Register transaction model; no hardware claim. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/native_menu/service_poweroff.h"
static struct {unsigned calls,fail,ps_writes,pmic_writes,delays,fail_delay;uint32_t command,wdata;uint8_t ctl[2],enable[2];int download,bad_pon,deny_ps;} h;
static int read32(void *unused,uint64_t a,uint32_t *v){
    (void)unused;if(++h.calls==h.fail)return 0;
    if(a==0xfc4cf000u)*v=0x10000000u;
    else if(a==0xfc4cf808u)*v=1;
    else if(a==0xfc4cf818u){unsigned reg=(h.command>>4)&65535u,sid=(h.command>>20)&15u,index=sid?1u:0u;
        *v=reg==0x105?(sid?10u:9u):reg==0x801?4u:reg==0x805?1u:reg==0x85a?h.ctl[index]:h.enable[index];
        if(h.bad_pon&&reg==0x105)*v=0;
    }else if(a==0xfc4ab000u)*v=1;
    else if(a==0xfe87f65cu)*v=0;
    else if(a==0xfe87f000u||a==0xfe87f004u)*v=h.download==2?UINT32_MAX:h.download==3?(a==0xfe87f000u?0xe47b337du:0xce14091au):h.download==4?(a==0xfe87f000u?UINT32_MAX:0u):h.download?1u:0u;
    else if(a==0xfc4003c4u)*v=1;
    else if(a==0xfc4003c8u||a==0xfc4003ccu||a==0xfc4003d0u)*v=0x80000000u;
    else assert(!"unexpected poweroff read");return 1;
}
static int write32(void *unused,uint64_t a,uint32_t v){
    (void)unused;if(++h.calls==h.fail)return 0;
    if(a==0xfc4cf810u){assert(v<=255);h.wdata=v;}
    else if(a==0xfc4cf800u){
        unsigned op=v>>27,reg=(v>>4)&65535u,sid=(v>>20)&15u;assert((sid==0||sid==2)&&(op==0||op==1));h.command=v;
        if(!op){assert(reg==0x85a||reg==0x85b);unsigned index=sid?1u:0u;
            if(reg==0x85a){assert(!(h.enable[index]&128u));h.ctl[index]=(uint8_t)h.wdata;}
            else h.enable[index]=(uint8_t)h.wdata;
            h.pmic_writes++;
        }
    }else{assert(a==0xfc4ab000u&&v==0);if(h.deny_ps)return 0;assert((h.ctl[0]&15u)==4&&(h.ctl[1]&15u)==4&&h.enable[0]==128&&h.enable[1]==128);h.ps_writes++;}
    return 1;
}
static void fence(void *p){(void)p;}
static int delay(void *p,unsigned us){(void)p;assert(us==1||us==500);h.delays++;return h.delays==h.fail_delay?-1:0;}
static void setup(void){memset(&h,0,sizeof h);h.ctl[0]=h.ctl[1]=1;h.enable[0]=h.enable[1]=128;}
int main(void){
    const struct xpl_hw_io io={0,read32,write32,fence};struct xpl_hw_config cfg={.complete=1,.spmi={0xfc4cf000u,0x1000}};xpl_poweroff_diag d;
    setup();assert(xpl_service_poweroff_trigger(&cfg,&io,delay,0,&d)==XPL_POWER_TRIGGERED);assert(d.phase==3&&h.ps_writes==1&&h.pmic_writes==6&&h.delays==3);unsigned calls=h.calls;
    assert(!xpl_service_poweroff_returned(&io,delay,0,&d)&&d.phase==4&&h.ctl[0]==1&&h.ctl[1]==1&&h.enable[0]==128&&h.enable[1]==128);
    for(unsigned n=1;n<=calls;n++){setup();h.fail=n;int e=xpl_service_poweroff_trigger(&cfg,&io,delay,0,&d);assert(e!=XPL_POWER_TRIGGERED&&!h.ps_writes);assert(h.ctl[0]==1&&h.ctl[1]==1&&h.enable[0]==128&&h.enable[1]==128);}
    for(unsigned n=1;n<=3;n++){setup();h.fail_delay=n;assert(xpl_service_poweroff_trigger(&cfg,&io,delay,0,&d)<0&&!h.ps_writes);assert(h.ctl[0]==1&&h.ctl[1]==1&&h.enable[0]==128&&h.enable[1]==128);}
    setup();h.bad_pon=1;assert(xpl_service_poweroff_trigger(&cfg,&io,delay,0,&d)==XPL_POWER_PON&&!h.pmic_writes);
    setup();h.download=1;assert(xpl_service_poweroff_trigger(&cfg,&io,delay,0,&d)==XPL_POWER_CHANGED&&!h.pmic_writes);
    for(int mode=3;mode<=4;mode++){setup();h.download=mode;assert(xpl_service_poweroff_trigger(&cfg,&io,delay,0,&d)==XPL_POWER_CHANGED&&!h.pmic_writes&&!h.ps_writes);}
    setup();h.download=2;assert(xpl_service_poweroff_trigger(&cfg,&io,delay,0,&d)==XPL_POWER_TRIGGERED&&h.ps_writes==1&&d.dload0==UINT32_MAX&&d.dload1==UINT32_MAX);unsigned erased_calls=h.calls;
    for(unsigned n=1;n<=erased_calls;n++){setup();h.download=2;h.fail=n;assert(xpl_service_poweroff_trigger(&cfg,&io,delay,0,&d)!=XPL_POWER_TRIGGERED&&!h.ps_writes);assert(h.ctl[0]==1&&h.ctl[1]==1&&h.enable[0]==128&&h.enable[1]==128);}
    setup();h.deny_ps=1;assert(xpl_service_poweroff_trigger(&cfg,&io,delay,0,&d)<0&&h.ctl[0]==1&&h.ctl[1]==1);
    puts("PASS: SID0/2 disable500us/type04/enable/readback, PS_HOLD last, every I/O fault and delay failure rollback; host model only");
}
