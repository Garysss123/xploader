/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/native_menu/service_reset.h"
static struct {unsigned calls,fail,ps_writes;uint32_t cmd,reason;int bad_pon,download,deny_ps,ignore_reason;} h;
static int read32(void *unused,uint64_t a,uint32_t *v){
    (void)unused;if(++h.calls==h.fail)return 0;
    if(a==0xfc4cf000u)*v=0x10000000;
    else if(a==0xfc4cf808u)*v=1;
    else if(a==0xfc4cf818u){
        unsigned reg=(h.cmd>>4)&65535,sid=(h.cmd>>20)&15;
        *v=reg==0x105?(sid?10u:9u):reg==0x801?4u:reg==0x805?1u:reg==0x85a?(h.bad_pon?7u:1u):128u;
    }else if(a==0xfe87f65cu)*v=h.reason;
    else if(a==0xfc4ab000u)*v=1;
    else if(a==0xfe87f000u||a==0xfe87f004u)*v=h.download==2?UINT32_MAX:h.download==3?(a==0xfe87f000u?0xe47b337du:0xce14091au):h.download==4?(a==0xfe87f000u?UINT32_MAX:0u):h.download?0xe47b337du:0;
    else if(a==0xfc4003c4u)*v=1;
    else if(a==0xfc4003c8u||a==0xfc4003ccu||a==0xfc4003d0u)*v=0x80000000u;
    else assert(!"unexpected reset read");return 1;
}
static int write32(void *unused,uint64_t a,uint32_t v){
    (void)unused;if(++h.calls==h.fail)return 0;
    if(a==0xfc4cf800u){assert((v>>27)==1);h.cmd=v;}
    else if(a==0xfe87f65cu){assert(v==0x77665500u||v==0x77665501u||v==0xabadbabeu);if(!h.ignore_reason)h.reason=v;}
    else {assert(a==0xfc4ab000u&&v==0);if(h.deny_ps)return 0;h.ps_writes++;}return 1;
}
static void fence(void *unused){(void)unused;}
static void reset(void){memset(&h,0,sizeof h);h.reason=0xabadbabeu;}
int main(void){
    const struct xpl_hw_io io={0,read32,write32,fence};
    struct xpl_hw_config cfg={.complete=1,.spmi={0xfc4cf000u,0x1000}};
    xpl_service_snapshot s;xpl_service_reset_diag d;
    reset();assert(xpl_service_fastboot_trigger(&cfg,&io,&s,&d)==XPL_S1_TRIGGERED);
    assert(h.ps_writes==1&&h.reason==0x77665500u&&d.phase==4&&d.writes==2);
    unsigned calls=h.calls;
    assert(xpl_service_reset_returned(&io,&d)==XPL_S1_RETURNED&&h.reason==0xabadbabeu&&d.phase==5);
    for(unsigned f=1;f<=calls;f++){
        reset();h.fail=f;int e=xpl_service_fastboot_trigger(&cfg,&io,&s,&d);
        assert(e!=XPL_S1_TRIGGERED&&!h.ps_writes);
    }
    reset();h.bad_pon=1;assert(xpl_service_fastboot_trigger(&cfg,&io,&s,&d)==XPL_S1_PON&&!d.writes);
    reset();h.download=1;assert(xpl_service_fastboot_trigger(&cfg,&io,&s,&d)==XPL_S1_DLOAD&&!d.writes);
    for(int mode=3;mode<=4;mode++){reset();h.download=mode;assert(xpl_service_restart_trigger(&cfg,&io,&s,&d)==XPL_S1_DLOAD&&!d.writes&&!h.ps_writes);}
    reset();h.download=2;assert(xpl_service_restart_trigger(&cfg,&io,&s,&d)==XPL_S1_TRIGGERED&&h.ps_writes==1&&d.writes==2&&d.dload0==UINT32_MAX&&d.dload1==UINT32_MAX);
    unsigned erased_calls=h.calls;
    for(unsigned f=1;f<=erased_calls;f++){reset();h.download=2;h.fail=f;assert(xpl_service_restart_trigger(&cfg,&io,&s,&d)!=XPL_S1_TRIGGERED&&!h.ps_writes);}
    reset();h.download=2;assert(xpl_service_fastboot_trigger(&cfg,&io,&s,&d)==XPL_S1_TRIGGERED&&h.reason==0x77665500u&&d.writes==2);
    reset();h.ignore_reason=1;assert(xpl_service_fastboot_trigger(&cfg,&io,&s,&d)==XPL_S1_CHANGED&&!h.ps_writes);
    reset();h.deny_ps=1;assert(xpl_service_fastboot_trigger(&cfg,&io,&s,&d)==XPL_S1_IO&&h.reason==0xabadbabeu&&!h.ps_writes);
    reset();assert(xpl_service_restart_trigger(&cfg,&io,&s,&d)==XPL_S1_TRIGGERED&&h.ps_writes==1&&h.reason==0x77665501u);
    assert(xpl_service_restart_returned(&io,&d)==XPL_S1_RETURNED&&h.reason==0xabadbabeu);
    puts("PASS: exact warm-reset write allowlist, every I/O fault, warm-PON gate, DLOAD refusal, reason readback/restore");
}
