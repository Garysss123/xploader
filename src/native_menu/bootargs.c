/* SPDX-License-Identifier: MIT */
#include "bootargs.h"
static int space(uint8_t c) {
    return c==' '||c=='\t'||c=='\n'||c=='\r'||c=='\v'||c=='\f';
}
static int scrub(volatile uint8_t *args,size_t bytes,unsigned *removed,
                 xpl_bootargs_stats *stats,int span) {
    static const char charger[]="androidboot.mode=charger";
    size_t end,pos=0;
    if(!args||!removed||!bytes||bytes>65536)return XPL_BOOTARGS_ARGUMENT;
    *removed=0;
    for(end=0;end<bytes&&args[end];end++){}
    if(end==bytes&&!span)return XPL_BOOTARGS_UNTERMINATED;
    if(end==bytes&&stats)stats->nonterminated++;
    while(pos<end) {
        while(pos<end&&space(args[pos]))pos++;
        size_t first=pos;
        while(pos<end&&!space(args[pos]))pos++;
        if(pos-first>=sizeof charger-1) {
            size_t i;
            for(i=0;i<sizeof charger-1&&args[first+i]==(uint8_t)charger[i];i++){}
            if(i==sizeof charger-1) {
                for(i=first;i<pos;i++)args[i]=' ';
                (*removed)++;
            }
        }
        static const char startup[]="startup=";
        if(stats&&pos-first>sizeof startup-1) {
            size_t i;
            for(i=0;i<sizeof startup-1&&args[first+i]==(uint8_t)startup[i];i++){}
            if(i==sizeof startup-1) {
                size_t digit=first+i;
                if(digit<pos&&args[digit]=='+')digit++;
                if(pos-digit>=2&&args[digit]=='0'&&(args[digit+1]=='x'||args[digit+1]=='X'))digit+=2;
                uint64_t value=0;unsigned valid=pos>digit&&pos-digit<=16;
                for(i=digit;i<pos&&valid;i++) {
                    unsigned c=args[i],n;
                    if(c>='0'&&c<='9')n=c-'0';
                    else if(c>='a'&&c<='f')n=c-'a'+10;
                    else if(c>='A'&&c<='F')n=c-'A'+10;
                    else {valid=0;break;}
                    value=(value<<4)|n;
                }
                if(valid) {
                    if(!stats->startup_masked)stats->first_startup=value;
                    if(value&0x4004u) {
                        value&=~UINT64_C(0x4004);
                        static const char hex[]="0123456789abcdef";
                        for(i=pos;i>digit;) {args[--i]=(uint8_t)hex[value&15u];value>>=4;}
                        stats->startup_masked++;
                    }
                }
            }
        }
    }
    return XPL_BOOTARGS_OK;
}
int xpl_bootargs_interactive(volatile uint8_t *args,size_t bytes,unsigned *removed) {
    return scrub(args,bytes,removed,0,0);
}
static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];
}
static int named(const uint8_t *p,size_t n,const char *s) {
    for(size_t i=0;i<n;i++) {if(p[i]!=(uint8_t)s[i])return 0;if(!s[i])return 1;}
    return 0;
}
static int walk(uint8_t *p,size_t size,xpl_bootargs_stats *stats,unsigned apply) {
    if(!p||size<40||be32(p)!=0xd00dfeedu)return XPL_BOOTARGS_ARGUMENT;
    uint32_t total=be32(p+4),so=be32(p+8),st=be32(p+12),sn=be32(p+32),ss=be32(p+36);
    if(total>size||total<40||total>2097152||(so&3u)||so<40||st<40||so>total||ss>total-so||st>total||sn>total-st||
       (so<st+sn&&st<so+ss))
        return XPL_BOOTARGS_ARGUMENT;
    uint32_t pos=so,end=so+ss;unsigned depth=0,chosen=0,root=0;
    while(end-pos>=4) {
        uint32_t token=be32(p+pos);pos+=4;
        if(token==1) {
            uint32_t first=pos;while(pos<end&&p[pos])pos++;
            if(pos==end||depth>=64)return XPL_BOOTARGS_ARGUMENT;
            if(!depth) {if(root||pos!=first)return XPL_BOOTARGS_ARGUMENT;root=1;}
            if(depth==1&&(named(p+first,pos-first+1,"chosen")||named(p+first,pos-first+1,"chosen@0"))) {
                chosen=2;stats->chosen_nodes++;
            }
            depth++;pos=(pos+4u)&~3u;if(pos>end)return XPL_BOOTARGS_ARGUMENT;
        } else if(token==2) {
            if(!depth)return XPL_BOOTARGS_ARGUMENT;
            if(depth==chosen)chosen=0;
            depth--;
        } else if(token==3) {
            if(!depth||end-pos<8)return XPL_BOOTARGS_ARGUMENT;
            uint32_t n=be32(p+pos),name=be32(p+pos+4);pos+=8;
            if(n>end-pos||name>=sn)return XPL_BOOTARGS_ARGUMENT;
            size_t remaining=sn-name,i=0;
            while(i<remaining&&p[st+name+i])i++;
            if(i==remaining)return XPL_BOOTARGS_ARGUMENT;
            if(depth==chosen&&depth==2&&named(p+st+name,remaining,"bootargs")) {
                if(n>65536)return XPL_BOOTARGS_ARGUMENT;
                stats->properties++;
                if(apply&&n) {
                    unsigned removed=0;int r=scrub(p+pos,n,&removed,stats,1);
                    if(r)return r;
                    stats->removed+=removed;
                }
            }
            pos=(pos+n+3u)&~3u;if(pos>end)return XPL_BOOTARGS_ARGUMENT;
        } else if(token==4)continue;
        else if(token==9)return depth||!root?XPL_BOOTARGS_ARGUMENT:XPL_BOOTARGS_OK;
        else return XPL_BOOTARGS_ARGUMENT;
    }
    return XPL_BOOTARGS_ARGUMENT;
}
int xpl_bootargs_clean_fdt(void *fdt,size_t available,xpl_bootargs_stats *stats) {
    if(!stats)return XPL_BOOTARGS_ARGUMENT;
    *stats=(xpl_bootargs_stats){0};
    xpl_bootargs_stats check={0};
    int r=walk(fdt,available,&check,0);
    if(r)return r;
    return walk(fdt,available,stats,1);
}
