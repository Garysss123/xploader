/* SPDX-License-Identifier: MIT. Key-driven extended service interactions. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/menu/xpl_menu.h"
#include "../src/menu/xpl_locale.h"
static uint64_t tick;static unsigned boots,lang_saves,defaults,s1;static enum xpl_menu_action last_boot;static char lang[16];
static void key(xpl_menu *m,unsigned k){tick+=20;xpl_menu_poll(m,k,tick);tick+=16;xpl_menu_poll(m,k,tick);tick+=30;xpl_menu_poll(m,0,tick);tick+=16;xpl_menu_poll(m,0,tick);}
static enum xpl_menu_action_result boot(void *p,enum xpl_menu_action a){(void)p;boots++;last_boot=a;return XPL_MENU_ACTION_RETURNED;}
static enum xpl_menu_action_result service(void *p,enum xpl_menu_service_action a){(void)p;if(a==XPL_SERVICE_FASTBOOT)s1++;return XPL_MENU_ACTION_RETURNED;}
static void folder(void *p){(void)p;}
static void saved(void *p,unsigned a,unsigned b,const char *c){(void)p;(void)a;(void)b;(void)c;assert(0);}
static void language(void *p,const char *s){(void)p;lang_saves++;strcpy(lang,s);}
static void reset(void *p){(void)p;defaults++;}
static void tools(void *p,enum xpl_menu_tool tool){static const char lines[2][96]={"Actual diagnostic data","No boot action"};xpl_menu *m=p;assert(tool==XPL_TOOL_ABOUT);xpl_menu_show_info(m,"About",lines,2,6);}
int main(void){
    xpl_menu m;struct xpl_menu_config config={.startup=XPL_MENU_ALWAYS,.quick_target=XPL_MENU_BOOT_EMMC,.language="en"};
    xpl_menu_init(&m,&config,(1u<<XPL_MENU_ACTION_COUNT)-1u,boot,0,0,0);m.folder_open=folder;
    xpl_menu_set_services(&m,3,service,0);xpl_menu_set_settings(&m,saved,0);xpl_menu_set_extended(&m,language,reset,tools,&m);
    m.selected=XPL_MENU_SERVICE;key(&m,XPL_MENU_KEY_POWER);assert(m.page==XPL_MENU_SERVICE_PAGE);
    key(&m,XPL_MENU_KEY_DOWN);key(&m,XPL_MENU_KEY_POWER);assert(m.page==XPL_MENU_LANGUAGE_PAGE);
    key(&m,XPL_MENU_KEY_DOWN);key(&m,XPL_MENU_KEY_POWER);assert(lang_saves==1&&!strcmp(lang,"zh-TW")&&!strcmp(m.language,"en"));
    m.selected=2;key(&m,XPL_MENU_KEY_POWER);assert(m.page==XPL_MENU_SERVICE_PAGE&&m.selected==1);
    m.selected=4;key(&m,XPL_MENU_KEY_POWER);assert(m.page==XPL_MENU_TOOLS_PAGE);
    key(&m,XPL_MENU_KEY_POWER);assert(m.page==XPL_MENU_ONCE_PAGE);m.selected=1;key(&m,XPL_MENU_KEY_POWER);
    assert(boots==1&&last_boot==XPL_MENU_BOOT_SD&&m.startup==XPL_MENU_ALWAYS&&m.quick_target==XPL_MENU_BOOT_EMMC);
    m.selected=3;key(&m,XPL_MENU_KEY_POWER);m.selected=6;key(&m,XPL_MENU_KEY_POWER);assert(m.page==XPL_MENU_INFO_PAGE&&m.info_count==2);
    key(&m,XPL_MENU_KEY_POWER);assert(m.page==XPL_MENU_TOOLS_PAGE&&m.selected==6);
    xpl_menu_show_error(&m,"Returning diagnostic",3,tick);tick+=3000;xpl_menu_poll(&m,0,tick);assert(m.page==XPL_MENU_TOOLS_PAGE&&m.selected==6);
    m.selected=5;key(&m,XPL_MENU_KEY_POWER);assert(m.page==XPL_MENU_DEFAULTS_PAGE&&m.selected==1&&!defaults);
    key(&m,XPL_MENU_KEY_POWER);assert(m.page==XPL_MENU_TOOLS_PAGE&&!defaults);key(&m,XPL_MENU_KEY_POWER);m.selected=0;key(&m,XPL_MENU_KEY_POWER);assert(defaults==1);
    m.page=XPL_MENU_TOOLS_PAGE;m.selected=4;key(&m,XPL_MENU_KEY_POWER);assert(m.page==XPL_MENU_RESCUE_PAGE);
    key(&m,XPL_MENU_KEY_POWER);assert(boots==2&&last_boot==XPL_MENU_TWRP);m.selected=1;m.service_available=0;key(&m,XPL_MENU_KEY_POWER);assert(!s1&&m.notice==XPL_MENU_NOTICE_PENDING);
    m.service_available=2;key(&m,XPL_MENU_KEY_POWER);assert(s1==1);
    assert(!strcmp(xpl_locale_message("zh-TW","language"),"語言"));assert(!strcmp(xpl_locale_message("xx","language"),"Language"));
    const char valid[]={ (char)0xe9,(char)0x96,(char)0x8b,0 };size_t used=0;assert(xpl_utf8_next(valid,3,&used)==0x958b&&used==3);
    assert(xpl_utf8_next(valid,1,&used)=='?'&&used==1);assert(xpl_locale_glyph(0x958b));
    puts("PASS: language commit request, BootOnce preserves policy, information/back/error return, explicit-only defaults, rescue availability, UTF8/fallback");
}
