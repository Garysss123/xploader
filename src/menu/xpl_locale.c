/* SPDX-License-Identifier: MIT */
#include "xpl_locale.h"
#include "xpl_locale_data.h"
static int same(const char *a,const char *b){if(!a||!b)return 0;while(*a&&*a==*b){a++;b++;}return *a==*b;}
int xpl_locale_valid(const char *s){return same(s,"en")||same(s,"zh-TW");}
const char *xpl_locale_canonical(const char *s){return same(s,"zh-TW")?"zh-TW":"en";}
const char *xpl_locale_message(const char *locale,const char *id){
    for(size_t i=0;i<sizeof xpl_locale_entries/sizeof *xpl_locale_entries;i++)
        if(same(id,xpl_locale_entries[i].id))return same(locale,"zh-TW")?xpl_locale_entries[i].zh:xpl_locale_entries[i].en;
    return "Unknown";
}
const char *xpl_locale_translate(const char *locale,const char *english){
    if(!same(locale,"zh-TW")||!english)return english;
    for(size_t i=0;i<sizeof xpl_locale_entries/sizeof *xpl_locale_entries;i++)
        if(same(english,xpl_locale_entries[i].en))return xpl_locale_entries[i].zh;
    return english;
}
const xpl_cjk_glyph *xpl_locale_glyph(uint32_t cp){
    size_t lo=0,hi=sizeof xpl_cjk_glyphs/sizeof *xpl_cjk_glyphs;
    while(lo<hi){size_t mid=lo+(hi-lo)/2;if(xpl_cjk_glyphs[mid].codepoint<cp)lo=mid+1;else hi=mid;}
    return lo<sizeof xpl_cjk_glyphs/sizeof *xpl_cjk_glyphs&&xpl_cjk_glyphs[lo].codepoint==cp?&xpl_cjk_glyphs[lo]:0;
}
uint32_t xpl_utf8_next(const char *s,size_t size,size_t *used){
    if(!used)return '?';
    *used=0;if(!s||!size||!s[0])return 0;
    uint32_t cp=(uint8_t)s[0];*used=1;if(cp<128)return cp;
    unsigned need;uint32_t minimum;
    if(cp>=0xc2&&cp<=0xdf){need=2;cp&=31;minimum=0x80;}
    else if(cp>=0xe0&&cp<=0xef){need=3;cp&=15;minimum=0x800;}
    else if(cp>=0xf0&&cp<=0xf4){need=4;cp&=7;minimum=0x10000;}
    else return '?';
    if(size<need)return '?';
    for(unsigned i=1;i<need;i++){uint32_t c=(uint8_t)s[i];if((c&0xc0)!=0x80)return '?';cp=(cp<<6)|(c&63);}
    if(cp<minimum||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff))return '?';
    *used=need;return cp;
}
