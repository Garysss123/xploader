/* SPDX-License-Identifier: MIT */
#include "../src/menu/xpl_menu.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define ALL_ACTIONS ((1u << XPL_MENU_ACTION_COUNT) - 1u)
_Static_assert(XPL_MENU_ACTION_COUNT == 6, "First-release menu has six rows");
_Static_assert(XPL_MENU_SERVICE == 3 && XPL_MENU_RESET == 4 && XPL_MENU_POWEROFF == 5,
               "Six-row semantic action mapping changed");
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x); abort(); } } while(0)
struct actions { unsigned calls; enum xpl_menu_action last; enum xpl_menu_action_result result; };
static enum xpl_menu_action_result action(void *context, enum xpl_menu_action a)
{
    struct actions *log = context;
    ++log->calls; log->last = a; return log->result;
}
static void stable(xpl_menu *m, uint32_t keys, uint64_t *time)
{
    *time += 20; (void)xpl_menu_poll(m, keys, *time);
    *time += 15; (void)xpl_menu_poll(m, keys, *time);
}
static void press(xpl_menu *m, uint32_t key, uint64_t *time)
{
    stable(m,key,time); stable(m,0,time);
}
static void service_folders(void *unused){(void)unused;}
static enum xpl_menu_action_result service_test(void *context,enum xpl_menu_service_action service)
{
    unsigned *calls=context;CHECK(service==XPL_SERVICE_FASTBOOT);(*calls)++;return XPL_MENU_ACTION_RETURNED;
}
static enum xpl_menu_action_result pcsd_test(void *context,enum xpl_menu_service_action service){
    unsigned *calls=context;CHECK(service==XPL_SERVICE_PC_SD);(*calls)++;return XPL_MENU_ACTION_RETURNED;
}
static struct {unsigned calls,mode,target;char directory[256];} settings_log;
static void save_settings_test(void *unused,unsigned mode,unsigned target,const char *path){
    (void)unused;settings_log.calls++;settings_log.mode=mode;settings_log.target=target;
    snprintf(settings_log.directory,sizeof settings_log.directory,"%s",path);
}
static void settings_tests(void){
    xpl_menu m;uint64_t t=0;
    for(unsigned target=0;target<3;target++){
        xpl_menu_init(&m,0,0,0,0,0,0);m.folder_open=service_folders;xpl_menu_set_settings(&m,save_settings_test,0);
        m.page=XPL_MENU_BOOT_SETTINGS_PAGE;m.selected=0;unsigned before=settings_log.calls;
        press(&m,XPL_MENU_KEY_POWER,&t);CHECK(m.page==XPL_MENU_QUICK_TARGET_PAGE&&!m.selected);
        for(unsigned n=0;n<target;n++)press(&m,XPL_MENU_KEY_DOWN,&t);
        stable(&m,XPL_MENU_KEY_POWER,&t);CHECK(settings_log.calls==before);
        stable(&m,0,&t);CHECK(settings_log.calls==before+1&&settings_log.mode==XPL_MENU_QUICK&&settings_log.target==target);
        CHECK(m.page==XPL_MENU_BOOT_SETTINGS_PAGE&&m.startup==XPL_MENU_ALWAYS&&m.notice==XPL_MENU_NOTICE_SETTINGS_SAVE);
        m.selected=1;press(&m,XPL_MENU_KEY_POWER,&t);CHECK(settings_log.mode==XPL_MENU_ALWAYS&&settings_log.calls==before+2);
    }
}
static void service_tests(void)
{
    xpl_menu m;uint64_t t=0;unsigned calls=0;
    xpl_menu_init(&m,0,0,0,0,0,0);m.folder_open=service_folders;
    xpl_menu_set_services(&m,2,service_test,&calls);m.page=XPL_MENU_SERVICE_PAGE;m.selected=2;
    press(&m,XPL_MENU_KEY_POWER,&t);CHECK(!calls&&m.notice==XPL_MENU_NOTICE_PENDING);
    press(&m,XPL_MENU_KEY_DOWN,&t);CHECK(m.selected==3);
    stable(&m,XPL_MENU_KEY_POWER,&t);CHECK(!calls);
    stable(&m,0,&t);CHECK(calls==1);
    (void)xpl_menu_poll(&m,0,t+1000);t+=1000;CHECK(calls==1);
    xpl_menu_show_error(&m,"S1 returned",6,t);
    (void)xpl_menu_poll(&m,0,t+3000);t+=3000;CHECK(m.page==XPL_MENU_SERVICE_PAGE&&m.selected==3);
    press(&m,XPL_MENU_KEY_DOWN,&t);CHECK(m.selected==4);
    press(&m,XPL_MENU_KEY_POWER,&t);CHECK(m.page==XPL_MENU_MAIN&&m.selected==XPL_MENU_SERVICE);
    m.page=XPL_MENU_SERVICE_PAGE;m.selected=0;press(&m,XPL_MENU_KEY_UP,&t);CHECK(m.selected==4);
    xpl_menu_set_services(&m,3,pcsd_test,&calls);m.selected=2;
    stable(&m,XPL_MENU_KEY_POWER,&t);CHECK(calls==1);
    stable(&m,0,&t);CHECK(calls==2);
    (void)xpl_menu_poll(&m,0,t+1000);t+=1000;CHECK(calls==2);
    m.page=XPL_MENU_PC_SD_PAGE;
    press(&m,XPL_MENU_KEY_POWER,&t);press(&m,XPL_MENU_KEY_UP,&t);CHECK(calls==2&&m.page==XPL_MENU_PC_SD_PAGE&&m.selected==2);
}
static void controller_tests(void)
{
    xpl_menu m; struct xpl_menu_config cfg = {0}; uint64_t t = 0;
    struct actions log = {0, XPL_MENU_BOOT_EMMC, XPL_MENU_ACTION_RETURNED};
    xpl_menu_init(&m, 0, UINT32_MAX, action, &log, 0, t);
    CHECK(m.startup == XPL_MENU_ALWAYS && m.selected == 0 && m.page == XPL_MENU_MAIN);
    CHECK(m.available_mask == ALL_ACTIONS);
    xpl_menu_mark_clean(&m);
    CHECK(xpl_menu_poll(&m, 0, UINT64_C(1000000000000)) == 0 && !log.calls);
    CHECK(xpl_menu_action_available(&m,XPL_MENU_SERVICE));
    CHECK(!xpl_menu_action_available(&m,(enum xpl_menu_action)99));
    CHECK(!strcmp(xpl_menu_action_label(XPL_MENU_BOOT_EMMC),"Boot eMMC"));
    CHECK(!strcmp(xpl_menu_action_label((enum xpl_menu_action)99),"Unknown"));
    xpl_menu_init(&m, 0, 1, action, &log, XPL_MENU_KEY_POWER, 0); t=0;
    (void)xpl_menu_poll(&m,XPL_MENU_KEY_POWER,5000);
    CHECK(!log.calls && !m.power_armed);
    t=5000; stable(&m,0,&t);
    CHECK(!log.calls && m.power_armed);
    stable(&m,XPL_MENU_KEY_POWER,&t); CHECK(!log.calls);
    stable(&m,0,&t); CHECK(log.calls == 1 && log.last == XPL_MENU_BOOT_EMMC);
    CHECK(m.notice == XPL_MENU_NOTICE_RETURNED);

    xpl_menu_init(&m,0,ALL_ACTIONS,action,&log,0,0);
    (void)xpl_menu_poll(&m,XPL_MENU_KEY_DOWN,1);
    (void)xpl_menu_poll(&m,0,10);
    (void)xpl_menu_poll(&m,0,30); CHECK(m.selected == 0);
    (void)xpl_menu_poll(&m,XPL_MENU_KEY_DOWN,40);
    (void)xpl_menu_poll(&m,XPL_MENU_KEY_DOWN,54); CHECK(m.selected == 0);
    (void)xpl_menu_poll(&m,XPL_MENU_KEY_DOWN,55); CHECK(m.selected == 1);
    (void)xpl_menu_poll(&m,XPL_MENU_KEY_DOWN,504); CHECK(m.selected == 1);
    (void)xpl_menu_poll(&m,XPL_MENU_KEY_DOWN,505); CHECK(m.selected == 2);
    (void)xpl_menu_poll(&m,XPL_MENU_KEY_DOWN,624); CHECK(m.selected == 2);
    (void)xpl_menu_poll(&m,XPL_MENU_KEY_DOWN,625); CHECK(m.selected == 3);
    (void)xpl_menu_poll(&m,XPL_MENU_KEY_DOWN,50000); CHECK(m.selected == 4);
    (void)xpl_menu_poll(&m,0,60000); CHECK(m.selected == 4);
    (void)xpl_menu_poll(&m,0,60015); CHECK(m.selected == 4 && !m.repeat_key);
    t=60015; stable(&m,XPL_MENU_KEY_UP|XPL_MENU_KEY_DOWN,&t); CHECK(m.selected == 4);
    (void)xpl_menu_poll(&m,3,t+5000); CHECK(m.selected == 4);
    t+=5000; stable(&m,0,&t); press(&m,XPL_MENU_KEY_UP,&t); CHECK(m.selected == 3);
    xpl_menu_init(&m,0,ALL_ACTIONS,action,&log,0,0); t=0;
    press(&m,XPL_MENU_KEY_UP,&t); CHECK(m.selected == XPL_MENU_POWEROFF);
    press(&m,XPL_MENU_KEY_DOWN,&t); CHECK(m.selected == 0);

    log.calls=0; stable(&m,XPL_MENU_KEY_POWER,&t);
    (void)xpl_menu_poll(&m,XPL_MENU_KEY_POWER,t+5000); CHECK(!log.calls);
    t+=5000; stable(&m,0,&t);
    CHECK(!log.calls && m.notice == XPL_MENU_NOTICE_LONG_POWER);
    stable(&m,XPL_MENU_KEY_POWER,&t); stable(&m,XPL_MENU_KEY_POWER|XPL_MENU_KEY_DOWN,&t);
    stable(&m,0,&t); CHECK(!log.calls && m.selected == 0);
    stable(&m,XPL_MENU_KEY_POWER,&t);
    (void)xpl_menu_poll(&m,XPL_MENU_KEY_POWER,1); t=1;
    stable(&m,0,&t); CHECK(!log.calls);
    press(&m,XPL_MENU_KEY_POWER,&t); CHECK(log.calls == 1);

    log.calls=0; xpl_menu_init(&m,0,1,action,&log,0,0); t=0;
    press(&m,XPL_MENU_KEY_DOWN,&t); press(&m,XPL_MENU_KEY_POWER,&t);
    CHECK(!log.calls && m.notice == XPL_MENU_NOTICE_PENDING);
    log.result=XPL_MENU_ACTION_UNSUPPORTED;
    press(&m,XPL_MENU_KEY_UP,&t); press(&m,XPL_MENU_KEY_POWER,&t);
    CHECK(log.calls==1 && !xpl_menu_action_available(&m,XPL_MENU_BOOT_EMMC));
    press(&m,XPL_MENU_KEY_POWER,&t); CHECK(log.calls==1);
    log.result=XPL_MENU_ACTION_FAILED;
    xpl_menu_init(&m,0,1,action,&log,0,0); t=0; press(&m,XPL_MENU_KEY_POWER,&t);
    CHECK(m.notice==XPL_MENU_NOTICE_FAILED);

    log.calls=0; xpl_menu_init(&m,0,0,0,0,0,0); t=0;
    for (unsigned i=0;i<XPL_MENU_SERVICE;++i) press(&m,XPL_MENU_KEY_DOWN,&t);
    CHECK(m.selected==XPL_MENU_SERVICE && xpl_menu_action_available(&m,XPL_MENU_SERVICE));
    press(&m,XPL_MENU_KEY_POWER,&t); CHECK(m.page==XPL_MENU_SERVICE_PAGE);
    press(&m,XPL_MENU_KEY_DOWN,&t); CHECK(m.page==XPL_MENU_SERVICE_PAGE && m.selected==XPL_MENU_SERVICE);
    press(&m,XPL_MENU_KEY_POWER,&t); CHECK(m.page==XPL_MENU_MAIN && !log.calls);

    cfg.startup=XPL_MENU_QUICK; cfg.quick_target=XPL_MENU_BOOT_EMMC;
    log.result=XPL_MENU_ACTION_RETURNED; log.calls=0;
    xpl_menu_init(&m,&cfg,1,action,&log,0,0);
    CHECK(log.calls==1 && m.notice==XPL_MENU_NOTICE_QUICK_RETURNED && m.page==XPL_MENU_MAIN);
    (void)xpl_menu_poll(&m,0,999999); CHECK(log.calls==1);
    log.calls=0; xpl_menu_init(&m,&cfg,1,action,&log,XPL_MENU_KEY_POWER,0); t=0;
    CHECK(!log.calls && m.notice==XPL_MENU_NOTICE_HELD_FOR_MENU);
    stable(&m,0,&t); CHECK(!log.calls);
    xpl_menu_init(&m,&cfg,0,action,&log,0,0); CHECK(!log.calls && m.notice==XPL_MENU_NOTICE_PENDING);
    cfg.quick_target=XPL_MENU_RESET;
    xpl_menu_init(&m,&cfg,ALL_ACTIONS,action,&log,0,0);
    CHECK(m.startup==XPL_MENU_ALWAYS && m.notice==XPL_MENU_NOTICE_INVALID_CONFIG && !log.calls);
    cfg.startup=(enum xpl_menu_startup)99;
    xpl_menu_init(&m,&cfg,ALL_ACTIONS,action,&log,0,0); CHECK(m.startup==XPL_MENU_ALWAYS && !log.calls);
    xpl_menu_init(&m,0,ALL_ACTIONS,0,0,0,0);
    CHECK(!xpl_menu_action_available(&m,XPL_MENU_BOOT_EMMC) && xpl_menu_action_available(&m,XPL_MENU_SERVICE));
    xpl_menu_mark_clean(&m); CHECK(!xpl_menu_poll(&m,0x80000000u,100000));
    CHECK(!xpl_menu_poll(0,0,0));

    {
        uint32_t rng=0x7b4196b3; t=0; xpl_menu_init(&m,0,ALL_ACTIONS,action,&log,0,0);
        for (unsigned i=0;i<50000;++i) {
            rng^=rng<<13; rng^=rng>>17; rng^=rng<<5;
            t+=(rng>>8)&63; (void)xpl_menu_poll(&m,rng&7,t);
            CHECK(m.selected<XPL_MENU_ACTION_COUNT && (unsigned)m.page<=XPL_MENU_ERROR_PAGE);
        }
    }
    /* Every visible row keeps its semantic action after removal of USB rows. */
    {
        static const char *const labels[6]={"Boot eMMC","Boot SD","TWRP","Service","Reset","PowerOff"};
        log.calls=0; log.result=XPL_MENU_ACTION_RETURNED;
        xpl_menu_init(&m,0,ALL_ACTIONS,action,&log,0,0);t=0;
        for(unsigned i=0;i<6;++i) {
            unsigned previous=log.calls;
            CHECK(m.selected==i && !strcmp(xpl_menu_action_label((enum xpl_menu_action)i),labels[i]));
            press(&m,XPL_MENU_KEY_POWER,&t);
            if(i==XPL_MENU_SERVICE) {
                CHECK(m.page==XPL_MENU_SERVICE_PAGE && log.calls==previous);
                press(&m,XPL_MENU_KEY_POWER,&t);CHECK(m.page==XPL_MENU_MAIN);
            } else CHECK(log.calls==previous+1 && log.last==(enum xpl_menu_action)i);
            press(&m,XPL_MENU_KEY_DOWN,&t);
        }
        CHECK(m.selected==XPL_MENU_BOOT_EMMC && log.calls==5);
        CHECK(!xpl_menu_action_available(&m,(enum xpl_menu_action)6));
        CHECK(!strcmp(xpl_menu_action_label((enum xpl_menu_action)6),"Unknown"));
    }
}

struct error_action_context { xpl_menu *menu; unsigned calls; };
static enum xpl_menu_action_result failed_with_error(void *context, enum xpl_menu_action a)
{
    struct error_action_context *log = context;
    ++log->calls; CHECK(a == XPL_MENU_TWRP);
    xpl_menu_show_error(log->menu, "/twrp.img was not found.", 0x1001, log->menu->last_ms);
    return XPL_MENU_ACTION_FAILED;
}

static void error_tests(void)
{
    xpl_menu m; uint64_t t; struct actions log={0,XPL_MENU_BOOT_EMMC,XPL_MENU_ACTION_RETURNED};
    xpl_menu_init(&m,0,ALL_ACTIONS,action,&log,0,0);m.selected=XPL_MENU_TWRP;
    xpl_menu_show_error(&m,"/twrp.img was not found.",0x1001,100);
    CHECK(m.page==XPL_MENU_ERROR_PAGE && m.error_return_page==XPL_MENU_MAIN && m.error_return_selected==XPL_MENU_TWRP);
    CHECK(m.error_code==0x1001 && !strcmp(m.error_message,"/twrp.img was not found."));
    xpl_menu_mark_clean(&m);
    CHECK(!xpl_menu_poll(&m,0,3099) && m.page==XPL_MENU_ERROR_PAGE && !log.calls);
    CHECK(xpl_menu_poll(&m,XPL_MENU_KEY_POWER,3100) && m.page==XPL_MENU_MAIN && m.selected==XPL_MENU_TWRP && !log.calls);
    t=3100;stable(&m,0,&t);CHECK(!log.calls); /* Expiry input cannot select old row. */
    press(&m,XPL_MENU_KEY_POWER,&t);CHECK(log.calls==1 && log.last==XPL_MENU_TWRP);

    log.calls=0;xpl_menu_init(&m,0,ALL_ACTIONS,action,&log,0,0);m.page=XPL_MENU_SERVICE_PAGE;m.selected=XPL_MENU_SERVICE;
    xpl_menu_show_error(&m,"Read failed",3,0);t=0;
    press(&m,XPL_MENU_KEY_POWER,&t);
    CHECK(m.page==XPL_MENU_SERVICE_PAGE && m.selected==XPL_MENU_SERVICE && !log.calls);
    press(&m,XPL_MENU_KEY_POWER,&t);CHECK(m.page==XPL_MENU_MAIN && !log.calls);

    xpl_menu_init(&m,0,ALL_ACTIONS,action,&log,XPL_MENU_KEY_POWER,0);
    xpl_menu_show_error(&m,"Read failed",3,10);t=10;
    stable(&m,0,&t);CHECK(m.page==XPL_MENU_ERROR_PAGE && !log.calls);
    press(&m,XPL_MENU_KEY_POWER,&t);CHECK(m.page==XPL_MENU_MAIN && !log.calls);

    xpl_menu_init(&m,0,ALL_ACTIONS,action,&log,0,0);m.selected=XPL_MENU_BOOT_SD;
    xpl_menu_show_error(&m,"First error",1,0);
    xpl_menu_show_error(&m,"Second error",2,1500);
    CHECK(m.error_return_page==XPL_MENU_MAIN && m.error_return_selected==XPL_MENU_BOOT_SD && m.error_code==2);
    (void)xpl_menu_poll(&m,0,4499);CHECK(m.page==XPL_MENU_ERROR_PAGE);
    (void)xpl_menu_poll(&m,0,4500);CHECK(m.page==XPL_MENU_MAIN && m.selected==XPL_MENU_BOOT_SD && !log.calls);

    xpl_menu_show_error(&m,"Clock regression",4,5000);
    (void)xpl_menu_poll(&m,0,100);CHECK(m.page==XPL_MENU_ERROR_PAGE && m.error_since==100);
    (void)xpl_menu_poll(&m,0,3099);CHECK(m.page==XPL_MENU_ERROR_PAGE);
    (void)xpl_menu_poll(&m,0,3100);CHECK(m.page==XPL_MENU_MAIN && m.selected==XPL_MENU_BOOT_SD && !log.calls);

    xpl_menu_show_error(&m,"Fresh release returns",5,4000);t=4000;
    stable(&m,XPL_MENU_KEY_POWER,&t);t+=1500;stable(&m,0,&t);
    CHECK(m.page==XPL_MENU_MAIN && !log.calls); /* Back is safe even for >1.2s. */

    {
        struct error_action_context errors={&m,0};
        xpl_menu_init(&m,0,XPL_MENU_AVAILABLE(XPL_MENU_TWRP),failed_with_error,&errors,0,0);m.selected=XPL_MENU_TWRP;t=0;
        press(&m,XPL_MENU_KEY_POWER,&t);
        CHECK(errors.calls==1 && m.page==XPL_MENU_ERROR_PAGE && m.notice==XPL_MENU_NOTICE_NONE);
        CHECK(m.error_code==0x1001 && xpl_menu_action_available(&m,XPL_MENU_TWRP));
        press(&m,XPL_MENU_KEY_POWER,&t);CHECK(m.page==XPL_MENU_MAIN && m.selected==XPL_MENU_TWRP && errors.calls==1);
        /* An unimplemented driver never produces a fabricated missing-file error. */
        xpl_menu_init(&m,0,0,failed_with_error,&errors,0,0);m.selected=XPL_MENU_TWRP;t=0;
        press(&m,XPL_MENU_KEY_POWER,&t);
        CHECK(errors.calls==1 && m.page==XPL_MENU_MAIN && m.notice==XPL_MENU_NOTICE_PENDING && !m.error_message[0]);
    }
    xpl_menu_show_error(0,"ignored",1,0);
}

static uint32_t component(uint8_t c,uint32_t mask)
{
    unsigned shift=0;
    if (!mask) return 0;
    while (!(mask&(1u<<shift))) ++shift;
    return (uint32_t)(((uint64_t)c*(mask>>shift)+127)/255)<<shift;
}
static uint32_t expected_bg(const xpl_fb *f)
{
    return component(0x10,f->red_mask)|component(0x18,f->green_mask)|component(0x20,f->blue_mask)|f->alpha_mask;
}

/* Linked from immutable native-menu-005, whose RGB888 writer is scalar. */
int xpl_menu_render_005(const xpl_menu *menu, const xpl_fb *surface);
struct render_pair {
    uint8_t *actual, *reference;
    size_t prefix, visible_bytes, total;
    xpl_fb surface, oracle;
};
static struct render_pair render_pair_new(uint32_t width, uint32_t height,
    uint8_t bpp, uint32_t padding, size_t prefix,
    uint32_t r, uint32_t g, uint32_t b, uint32_t a)
{
    struct render_pair p = {0};
    uint32_t stride = width * bpp + padding;
    p.prefix = prefix;
    /* Exclude last-row padding from buffer_bytes: no write may rely on it. */
    p.visible_bytes = (size_t)stride * (height - 1u) + (size_t)width * bpp;
    p.total = prefix + p.visible_bytes + padding + 37u;
    p.actual = malloc(p.total); p.reference = malloc(p.total);
    CHECK(p.actual && p.reference);
    memset(p.actual, 0xa5, p.total); memset(p.reference, 0xa5, p.total);
    p.surface = (xpl_fb){p.actual + prefix, p.visible_bytes, width, height, stride, bpp,r,g,b,a};
    p.oracle = p.surface; p.oracle.pixels = p.reference + prefix;
    return p;
}
static void render_pair_free(struct render_pair *p)
{
    free(p->actual); free(p->reference);
}
static void render_pair_check(struct render_pair *p, const xpl_menu *m)
{
    CHECK(xpl_menu_render_005(m, &p->oracle) == XPL_FB_OK);
    if (memcmp(p->actual, p->reference, p->total)) {
        size_t at = 0;
        while (at < p->total && p->actual[at] == p->reference[at]) ++at;
        fprintf(stderr, "Renderer mismatch: %ux%u bpp=%u stride=%u prefix=%zu page=%u row=%u offset=%zu actual=%02x reference=%02x\n",
            p->surface.width, p->surface.height, p->surface.bytes_per_pixel,
            p->surface.stride_bytes, p->prefix, (unsigned)m->page, m->selected,
            at, p->actual[at], p->reference[at]);
        CHECK(0);
    }
    ++checks;
    for (size_t i = 0; i < p->prefix; ++i) CHECK(p->actual[i] == 0xa5);
    for (size_t i = p->prefix + p->visible_bytes; i < p->total; ++i) CHECK(p->actual[i] == 0xa5);
    for (uint32_t y = 0; y + 1u < p->surface.height; ++y)
        for (uint32_t x = p->surface.width * p->surface.bytes_per_pixel; x < p->surface.stride_bytes; ++x)
            CHECK(p->actual[p->prefix + (size_t)y * p->surface.stride_bytes + x] == 0xa5);
}
static void update_check(struct render_pair *p, xpl_menu *m, xpl_menu_render_cache *cache,
    enum xpl_menu_render_kind kind, uint32_t rows, unsigned notice)
{
    CHECK(xpl_menu_render_update(m, &p->surface, cache) == XPL_FB_OK);
    CHECK(cache->valid && cache->kind == kind && cache->row_mask == rows && cache->notice_updated == notice);
    render_pair_check(p, m);
}
static void frozen_renderer_tests(void)
{
    static const uint32_t masks[][4] = {
        {0xff,0xff00,0xff0000,0}, {0xff0000,0xff00,0xff,0},
        {0x3f0000,0xfc00,0x3ff,0xc00000}
    };
    xpl_menu m; struct actions log = {0,XPL_MENU_BOOT_EMMC,XPL_MENU_ACTION_RETURNED};
    xpl_menu_init(&m,0,ALL_ACTIONS,action,&log,0,0);
    /* Every pointer and stride residue, with full-fill and glyph-width tails. */
    for (unsigned mask = 0; mask < sizeof masks / sizeof masks[0]; ++mask)
        for (unsigned alignment = 0; alignment < 4; ++alignment)
            for (uint32_t padding = 0; padding < 4; ++padding) {
                uint32_t width = 216u + alignment;
                struct render_pair p = render_pair_new(width, 280, 3, padding,
                    32u + alignment, masks[mask][0],masks[mask][1],masks[mask][2],masks[mask][3]);
                for (unsigned page = 0; page < 3; ++page) {
                    m.page = (enum xpl_menu_page)page;
                    m.selected = (uint8_t)((alignment + padding) % XPL_MENU_ACTION_COUNT);
                    m.available_mask = (alignment & 1u) ? ALL_ACTIONS : 0;
                    m.notice = (enum xpl_menu_notice)(padding + 1u);
                    m.error_code = UINT32_C(0xabcdef01);
                    memset(m.error_message,'E',sizeof m.error_message);
                    CHECK(xpl_menu_render(&m,&p.surface) == XPL_FB_OK);
                    render_pair_check(&p,&m);
                }
                render_pair_free(&p);
            }
    printf("Frozen005 renderer equivalence PASS: 144 RGB888 frames; all pointer/stride residues and masks\n");
}
static void render_update_case(uint8_t bpp, uint32_t r, uint32_t g, uint32_t b, uint32_t a,
    uint32_t width, uint32_t height, uint32_t padding, size_t prefix)
{
    struct render_pair p = render_pair_new(width,height,bpp,padding,prefix,r,g,b,a);
    struct actions log = {0,XPL_MENU_BOOT_EMMC,XPL_MENU_ACTION_RETURNED};
    xpl_menu m; xpl_menu_render_cache cache = {0}, before;
    uint32_t rng = UINT32_C(0x748629ab);
    xpl_menu_init(&m,0,ALL_ACTIONS,action,&log,0,0);
    update_check(&p,&m,&cache,XPL_MENU_RENDER_FULL,ALL_ACTIONS,1);
    update_check(&p,&m,&cache,XPL_MENU_RENDER_NONE,0,0);
    /* NONE must not store even when the destination was externally disturbed. */
    p.actual[prefix] ^= 0xff;
    CHECK(xpl_menu_render_update(&m,&p.surface,&cache) == XPL_FB_OK && cache.kind == XPL_MENU_RENDER_NONE);
    CHECK(p.actual[prefix] == (uint8_t)(p.reference[prefix] ^ 0xffu));
    p.actual[prefix] ^= 0xff;
    for (unsigned row = 1; row <= XPL_MENU_ACTION_COUNT; ++row) {
        unsigned old = m.selected;
        m.selected = (uint8_t)(row % XPL_MENU_ACTION_COUNT);
        update_check(&p,&m,&cache,XPL_MENU_RENDER_PARTIAL,
            XPL_MENU_AVAILABLE(old)|XPL_MENU_AVAILABLE(m.selected),0);
    }
    for (unsigned notice = 1; notice <= XPL_MENU_NOTICE_LONG_POWER + 1u; ++notice) {
        m.notice = (enum xpl_menu_notice)(notice % (XPL_MENU_NOTICE_LONG_POWER + 1u));
        update_check(&p,&m,&cache,XPL_MENU_RENDER_PARTIAL,0,1);
    }
    m.available_mask = 0;
    update_check(&p,&m,&cache,XPL_MENU_RENDER_PARTIAL,ALL_ACTIONS ^ XPL_MENU_AVAILABLE(XPL_MENU_SERVICE),0);
    m.available_mask = ALL_ACTIONS;
    update_check(&p,&m,&cache,XPL_MENU_RENDER_PARTIAL,ALL_ACTIONS ^ XPL_MENU_AVAILABLE(XPL_MENU_SERVICE),0);
    m.action = 0;
    update_check(&p,&m,&cache,XPL_MENU_RENDER_PARTIAL,ALL_ACTIONS ^ XPL_MENU_AVAILABLE(XPL_MENU_SERVICE),0);
    m.action = action;
    update_check(&p,&m,&cache,XPL_MENU_RENDER_PARTIAL,ALL_ACTIONS ^ XPL_MENU_AVAILABLE(XPL_MENU_SERVICE),0);
    /* Combined state changes also have to match an independent full draw. */
    for (unsigned i = 0; i < (width == 1080 ? 8u : 48u); ++i) {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        m.selected = (uint8_t)(rng % XPL_MENU_ACTION_COUNT);
        m.available_mask = (rng >> 8) & ALL_ACTIONS;
        m.notice = (enum xpl_menu_notice)((rng >> 16) % (XPL_MENU_NOTICE_LONG_POWER + 1u));
        CHECK(xpl_menu_render_update(&m,&p.surface,&cache) == XPL_FB_OK);
        render_pair_check(&p,&m);
    }
    m.page = XPL_MENU_SERVICE_PAGE;
    update_check(&p,&m,&cache,XPL_MENU_RENDER_FULL,0,0);
    update_check(&p,&m,&cache,XPL_MENU_RENDER_NONE,0,0);
    memset(m.version,'V',sizeof m.version);
    update_check(&p,&m,&cache,XPL_MENU_RENDER_FULL,0,0);
    memset(m.sd_directory,'S',sizeof m.sd_directory);
    update_check(&p,&m,&cache,XPL_MENU_RENDER_FULL,0,0);
    memset(m.language,'L',sizeof m.language);
    update_check(&p,&m,&cache,XPL_MENU_RENDER_FULL,0,0);
    m.startup = XPL_MENU_QUICK;
    update_check(&p,&m,&cache,XPL_MENU_RENDER_FULL,0,0);
    xpl_menu_show_error(&m,"First failure",0x1001,0);
    update_check(&p,&m,&cache,XPL_MENU_RENDER_FULL,0,0);
    update_check(&p,&m,&cache,XPL_MENU_RENDER_NONE,0,0);
    ++m.error_code;
    update_check(&p,&m,&cache,XPL_MENU_RENDER_FULL,0,0);
    memset(m.error_message,'E',sizeof m.error_message);
    update_check(&p,&m,&cache,XPL_MENU_RENDER_FULL,0,0);
    (void)xpl_menu_poll(&m,0,XPL_MENU_ERROR_BACK_MS);
    CHECK(m.page == XPL_MENU_SERVICE_PAGE);
    update_check(&p,&m,&cache,XPL_MENU_RENDER_FULL,0,0);
    m.page = XPL_MENU_MAIN;
    update_check(&p,&m,&cache,XPL_MENU_RENDER_FULL,ALL_ACTIONS,1);
    xpl_menu_render_invalidate(&cache);
    CHECK(!cache.valid && cache.kind == XPL_MENU_RENDER_NONE && !cache.row_mask && !cache.notice_updated);
    update_check(&p,&m,&cache,XPL_MENU_RENDER_FULL,ALL_ACTIONS,1);
    /* A failed update may not commit or alter the cache. */
    memcpy(&before,&cache,sizeof cache);
    --p.surface.buffer_bytes;
    CHECK(xpl_menu_render_update(&m,&p.surface,&cache) == XPL_FB_BOUNDS);
    CHECK(!memcmp(&before,&cache,sizeof cache));
    ++p.surface.buffer_bytes;
    CHECK(xpl_menu_render_update(0,&p.surface,&cache) == XPL_FB_ARGUMENT);
    CHECK(xpl_menu_render_update(&m,&p.surface,0) == XPL_FB_ARGUMENT);
    CHECK(!memcmp(&before,&cache,sizeof cache));
    /* Pointer, mask, dimensions and stride changes require a full repaint. */
    {
        struct render_pair replacement = render_pair_new(width,height,bpp,padding,prefix,r,g,b,a);
        update_check(&replacement,&m,&cache,XPL_MENU_RENDER_FULL,ALL_ACTIONS,1);
        replacement.surface.red_mask = b; replacement.surface.blue_mask = r;
        replacement.oracle.red_mask = b; replacement.oracle.blue_mask = r;
        update_check(&replacement,&m,&cache,XPL_MENU_RENDER_FULL,ALL_ACTIONS,1);
        --replacement.surface.width; --replacement.oracle.width;
        replacement.visible_bytes -= bpp;
        replacement.surface.buffer_bytes = replacement.oracle.buffer_bytes = replacement.visible_bytes;
        memset(replacement.actual,0xa5,replacement.total); memset(replacement.reference,0xa5,replacement.total);
        update_check(&replacement,&m,&cache,XPL_MENU_RENDER_FULL,ALL_ACTIONS,1);
        --replacement.surface.stride_bytes; --replacement.oracle.stride_bytes;
        replacement.visible_bytes -= height - 1u;
        replacement.surface.buffer_bytes = replacement.oracle.buffer_bytes = replacement.visible_bytes;
        memset(replacement.actual,0xa5,replacement.total); memset(replacement.reference,0xa5,replacement.total);
        update_check(&replacement,&m,&cache,XPL_MENU_RENDER_FULL,ALL_ACTIONS,1);
        render_pair_free(&replacement);
    }
    render_pair_free(&p);
}
static void render_update_tests(void)
{
    render_update_case(2,0xf800,0x7e0,0x1f,0,240,320,7,17);
    render_update_case(4,0xff0000,0xff00,0xff,0xff000000,432,560,3,18);
    render_update_case(4,0x3ff00000,0xffc00,0x3ff,0xc0000000,219,281,1,19);
    render_update_case(3,0xff,0xff00,0xff0000,0,648,840,3,33);
    /* Measured E6653 RGB888 viewport begins after two 3240-byte rows. */
    render_update_case(3,0xff,0xff00,0xff0000,0,1080,1918,0,6480);
    render_update_case(3,0xff,0xff00,0xff0000,0,1080,1918,7,6480);
    xpl_menu_render_invalidate(0);
    printf("Dirty/full/frozen005 equivalence PASS: 16/24/32-bit; measured 1080x1918 stride3240 offset6480; prefix/padding/suffix guards\n");
}
static void render_guard_test(uint8_t bpp,uint32_t r,uint32_t g,uint32_t b,uint32_t a)
{
    xpl_menu m; struct xpl_menu_config cfg={.version="six-row-menu",.language="zh-TW"};
    const uint32_t width=240,height=320,stride=width*bpp+7;
    const size_t bytes=(size_t)stride*height,guard=17;
    uint8_t *allocation=malloc(bytes+guard*2); xpl_fb fb; uint32_t pixel;
    CHECK(allocation!=0); memset(allocation,0xa5,bytes+guard*2);
    fb=(xpl_fb){allocation+guard,bytes,width,height,stride,bpp,r,g,b,a};
    xpl_menu_init(&m,&cfg,1,0,0,0,0);
    CHECK(xpl_fb_validate(&fb)==0 && xpl_menu_render(&m,&fb)==0);
    pixel=expected_bg(&fb);
    for(unsigned byte=0;byte<bpp;++byte) CHECK(allocation[guard+byte]==(uint8_t)(pixel>>(byte*8)));
    for(size_t i=0;i<guard;++i) CHECK(allocation[i]==0xa5 && allocation[guard+bytes+i]==0xa5);
    for(unsigned y=0;y<height;++y) for(unsigned x=width*bpp;x<stride;++x) CHECK(allocation[guard+(size_t)y*stride+x]==0xa5);
    /* Boot-page stores must preserve the same scanout/padding boundaries.
     * Invalid phases are rejected without modifying the accepted page. */
    CHECK(xpl_boot_render(&fb,XPL_BOOT_ACCEPTED)==XPL_FB_OK);
    uint8_t *accepted=malloc(bytes);CHECK(accepted!=0);
    memcpy(accepted,allocation+guard,bytes);
    CHECK(xpl_boot_render(&fb,(enum xpl_boot_phase)99)==XPL_FB_ARGUMENT);
    CHECK(!memcmp(accepted,allocation+guard,bytes));
    for(unsigned step=XPL_BOOT_COUNT_3;step<=XPL_BOOT_COUNT_1;step++) {
        CHECK(xpl_boot_render(&fb,(enum xpl_boot_phase)step)==XPL_FB_OK);
        CHECK(memcmp(accepted,allocation+guard,bytes)!=0);
    }
    CHECK(xpl_boot_render(&fb,XPL_BOOT_HANDOFF)==XPL_FB_OK);
    CHECK(memcmp(accepted,allocation+guard,bytes)!=0);
    /* HANDOFF updates only the GO GO GO band; it cannot repaint the title. */
    unsigned scale=width/216u;if(scale>height/280u)scale=height/280u;
    for(unsigned y=0;y<height;++y) {
        if(y<89u*scale||y>=103u*scale)
            CHECK(!memcmp(accepted+(size_t)y*stride,allocation+guard+(size_t)y*stride,width*bpp));
        for(unsigned x=width*bpp;x<stride;++x) CHECK(allocation[guard+(size_t)y*stride+x]==0xa5);
    }
    for(size_t i=0;i<guard;++i)CHECK(allocation[i]==0xa5&&allocation[guard+bytes+i]==0xa5);
    free(accepted);
    m.page=XPL_MENU_SERVICE_PAGE; memset(m.sd_directory,'a',sizeof m.sd_directory);
    memset(m.version,'v',sizeof m.version); memset(m.language,'z',sizeof m.language);
    CHECK(xpl_menu_render(&m,&fb)==0);
    for(size_t i=0;i<guard;++i) CHECK(allocation[i]==0xa5 && allocation[guard+bytes+i]==0xa5);
    xpl_menu_show_error(&m,"/twrp.img was not found.",0x1001,1000);
    memset(m.error_message,'e',sizeof m.error_message);
    CHECK(xpl_menu_render(&m,&fb)==0);
    for(size_t i=0;i<guard;++i) CHECK(allocation[i]==0xa5 && allocation[guard+bytes+i]==0xa5);
    for(unsigned y=0;y<height;++y) for(unsigned x=width*bpp;x<stride;++x) CHECK(allocation[guard+(size_t)y*stride+x]==0xa5);
    fb.buffer_bytes=(uint64_t)stride*(height-1)+width*bpp-1;
    CHECK(xpl_menu_render(&m,&fb)==XPL_FB_BOUNDS);
    fb.buffer_bytes=bytes; fb.stride_bytes=width*bpp-1; CHECK(xpl_fb_validate(&fb)==XPL_FB_BOUNDS);
    fb.stride_bytes=stride; fb.red_mask=fb.green_mask; CHECK(xpl_fb_validate(&fb)==XPL_FB_FORMAT);
    fb.red_mask=r; fb.blue_mask=0; CHECK(xpl_fb_validate(&fb)==XPL_FB_FORMAT);
    fb.blue_mask=b; fb.red_mask=5; CHECK(xpl_fb_validate(&fb)==XPL_FB_FORMAT);
    fb.red_mask=r; fb.bytes_per_pixel=1; CHECK(xpl_fb_validate(&fb)==XPL_FB_FORMAT);
    fb.bytes_per_pixel=bpp; fb.height=279; CHECK(xpl_menu_render(&m,&fb)==XPL_FB_TOO_SMALL);
    fb.height=320; fb.pixels=(volatile uint8_t *)(UINTPTR_MAX-1); CHECK(xpl_fb_validate(&fb)==XPL_FB_BOUNDS);
    free(allocation);
}
static void screenshot(const char *directory)
{
    xpl_menu m; struct actions log={0,XPL_MENU_BOOT_EMMC,XPL_MENU_ACTION_RETURNED};
    struct xpl_menu_config cfg={.version="six-row-menu",.sd_directory="/lineageos17",.language="zh-TW"};
    uint32_t *pixels=malloc(1080u*1920u*4u); char path[1024]; FILE *file;
    xpl_fb fb={(volatile uint8_t *)pixels,1080u*1920u*4u,1080,1920,1080*4,4,0xff0000,0xff00,0xff,0xff000000};
    CHECK(pixels!=0);
    xpl_menu_init(&m,&cfg,XPL_MENU_AVAILABLE(XPL_MENU_BOOT_EMMC)|XPL_MENU_AVAILABLE(XPL_MENU_RESET)|XPL_MENU_AVAILABLE(XPL_MENU_POWEROFF),action,&log,0,0);
    for(unsigned view=0;view<9;++view) {
        static const char *const names[]={"main","service","error","boot","go","count","pcsd","settings","quick"};
        m.page=view==1?XPL_MENU_SERVICE_PAGE:XPL_MENU_MAIN;
        if(view==1){m.folder_open=service_folders;xpl_menu_set_services(&m,3,service_test,0);}
        if(view==6)m.page=XPL_MENU_PC_SD_PAGE;
        if(view>=7){xpl_menu_set_settings(&m,save_settings_test,0);m.page=view==7?XPL_MENU_BOOT_SETTINGS_PAGE:XPL_MENU_QUICK_TARGET_PAGE;}
        if(view==2) {m.selected=XPL_MENU_TWRP;xpl_menu_show_error(&m,"/twrp.img was not found.",0x1001,1000);}
        CHECK(xpl_menu_render(&m,&fb)==0);
        if(view>=3&&view<=5)CHECK(xpl_boot_render(&fb,XPL_BOOT_ACCEPTED)==XPL_FB_OK);
        if(view==4)CHECK(xpl_boot_render(&fb,XPL_BOOT_HANDOFF)==XPL_FB_OK);
        if(view==5)CHECK(xpl_boot_render(&fb,XPL_BOOT_COUNT_3)==XPL_FB_OK);
        CHECK(snprintf(path,sizeof path,"%s/menu-%s.ppm",directory,names[view])>0);
        file=fopen(path,"wb"); CHECK(file!=0);
        CHECK(fprintf(file,"P6\n1080 1920\n255\n")>0);
        for(unsigned i=0;i<1080u*1920u;++i) {
            uint8_t rgb[3]={(uint8_t)(pixels[i]>>16),(uint8_t)(pixels[i]>>8),(uint8_t)pixels[i]};
            CHECK(fwrite(rgb,1,3,file)==3);
        }
        CHECK(fclose(file)==0);
    }
    free(pixels);
}
int main(int argc,char **argv)
{
    controller_tests();error_tests();service_tests();settings_tests();
    render_guard_test(4,0xff0000,0xff00,0xff,0xff000000);
    render_guard_test(4,0xff,0xff00,0xff0000,0xff000000);
    render_guard_test(2,0xf800,0x7e0,0x1f,0);
    render_guard_test(3,0xff,0xff00,0xff0000,0);
    render_guard_test(4,0x3ff00000,0xffc00,0x3ff,0xc0000000);
    frozen_renderer_tests();render_update_tests();
    if(argc==2) screenshot(argv[1]); else CHECK(argc==1);
    printf("Menu/controller/framebuffer PASS: %u checks; state=%zu framebuffer=%zu bytes\n",checks,sizeof(xpl_menu),sizeof(xpl_fb));
    return 0;
}
