/* SPDX-License-Identifier: MIT */
#include "settings.h"
static uint32_t u32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint64_t u64(const uint8_t *p){return u32(p)|(uint64_t)u32(p+4)<<32;}
static void p32(uint8_t *p,uint32_t x){for(unsigned n=0;n<4;n++)p[n]=(uint8_t)(x>>(8u*n));}
static void p64(uint8_t *p,uint64_t x){p32(p,(uint32_t)x);p32(p+4,(uint32_t)(x>>32));}
static int equal(const uint8_t *a,const uint8_t *b,size_t n){while(n--)if(*a++!=*b++)return 0;return 1;}
static uint32_t crc(const uint8_t *p){
    uint32_t value=0xffffffffu;
    for(unsigned n=0;n<512;n++){
        value^=n>=60&&n<64?0:p[n];
        for(unsigned bit=0;bit<8;bit++)value=(value>>1)^((0u-(value&1u))&0xedb88320u);
    }return ~value;
}
static int path_valid(const char path[256]){
    if(path[0]!='/')return 0;unsigned length=0,component=1;
    while(length<256&&path[length]){
        unsigned c=(uint8_t)path[length];if(c<32||c==127||c=='\\')return 0;
        if(c=='/'&&length){unsigned n=length-component;if((n==1&&path[component]=='.')||(n==2&&path[component]=='.'&&path[component+1]=='.'))return 0;component=length+1;}
        length++;
    }
    if(length==256)return 0;unsigned n=length-component;
    if((n==1&&path[component]=='.')||(n==2&&path[component]=='.'&&path[component+1]=='.'))return 0;
    return 1;
}
static int locale_valid(const char language[16]){
    unsigned n=0;
    while(n<16&&language[n]){
        unsigned c=(uint8_t)language[n];
        if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(n&&c>='0'&&c<='9')||(n&&c=='-')))return 0;
        n++;
    }
    return n<16;
}
void xpl_settings_default(xpl_settings *s){
    *s=(xpl_settings){0};static const char path[]="/lineageos17";
    for(unsigned n=0;n<sizeof path;n++)s->directory[n]=path[n];
}
int xpl_settings_decode(const uint8_t *p,size_t size,xpl_settings *out){
    if(!p||!out||size!=512||!equal(p,(const uint8_t *)"XPLCFG01",8)||u32(p+8)!=1||u32(p+12)!=512||
       u32(p+24)>1||u32(p+28)>2||!u64(p+16)||u64(p+40)!=~u64(p+16)||u32(p+60)!=crc(p))return -1;
    xpl_settings s={.sequence=u64(p+16),.startup=u32(p+24),.target=u32(p+28)};
    for(unsigned n=0;n<256;n++)s.directory[n]=(char)p[64+n];
    if(!path_valid(s.directory))return -1;
    if(u32(p+32)==0x474e414cu&&u32(p+36)==1){
        for(unsigned n=0;n<16;n++)s.language[n]=(char)p[320+n];
        if(!locale_valid(s.language))for(unsigned n=0;n<16;n++)s.language[n]=0;
    }
    *out=s;return 0;
}
int xpl_settings_encode(const xpl_settings *s,uint8_t out[512]){
    if(!s||!out||!s->sequence||s->startup>1||s->target>2||!path_valid(s->directory)||!locale_valid(s->language))return -1;
    for(unsigned n=0;n<512;n++)out[n]=0;
    for(unsigned n=0;n<8;n++)out[n]=(uint8_t)"XPLCFG01"[n];
    p32(out+8,1);p32(out+12,512);p64(out+16,s->sequence);p32(out+24,s->startup);p32(out+28,s->target);p64(out+40,~s->sequence);
    for(unsigned n=0;n<256&&s->directory[n];n++)out[64+n]=(uint8_t)s->directory[n];
    if(s->language[0]){
        p32(out+32,0x474e414cu);p32(out+36,1);
        for(unsigned n=0;n<16&&s->language[n];n++)out[320+n]=(uint8_t)s->language[n];
    }
    p32(out+60,crc(out));return 0;
}
int xpl_settings_choose(const uint8_t p[1024],xpl_settings *out,unsigned *slot){
    if(!p||!out||!slot)return -1;xpl_settings a,b;int ar=xpl_settings_decode(p,512,&a),br=xpl_settings_decode(p+512,512,&b);
    if(ar&&br)return -1;
    if(!ar&&!br&&a.sequence==b.sequence&&!equal(p,p+512,512))return -2;
    *slot=!ar&&(br||a.sequence>=b.sequence)?0u:1u;*out=*slot?b:a;return 0;
}
