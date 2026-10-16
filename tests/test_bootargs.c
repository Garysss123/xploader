/* SPDX-License-Identifier: MIT */
#include "../src/native_menu/bootargs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do { checks++; if(!(x)) {fprintf(stderr,"%s:%d %s\n",__FILE__,__LINE__,#x);exit(1);} } while(0)
#define BLANK_CHARGER "                        "
static void example(const char *in,const char *out,unsigned expected) {
    size_t n=strlen(in)+1;uint8_t storage[512];unsigned removed=99;
    memset(storage,0xa5,sizeof storage);memcpy(storage+17,in,n);
    CHECK(xpl_bootargs_interactive(storage+17,n,&removed)==0);
    CHECK(removed==expected&&!strcmp((char *)storage+17,out));
    for(size_t i=0;i<17;i++)CHECK(storage[i]==0xa5);
    for(size_t i=17+n;i<sizeof storage;i++)CHECK(storage[i]==0xa5);
}
static void put32(uint8_t *p,uint32_t v) {
    p[0]=(uint8_t)(v>>24);p[1]=(uint8_t)(v>>16);p[2]=(uint8_t)(v>>8);p[3]=(uint8_t)v;
}
static uint32_t get32(const uint8_t *p) {
    return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];
}
static size_t begin(uint8_t *p,size_t at,const char *name) {
    put32(p+at,1);at+=4;size_t n=strlen(name)+1;memcpy(p+at,name,n);
    return (at+n+3)&~(size_t)3;
}
static size_t prop(uint8_t *p,size_t at,const char *value,size_t bytes,size_t *location) {
    put32(p+at,3);put32(p+at+4,(uint32_t)bytes);put32(p+at+8,0);at+=12;
    *location=at;memcpy(p+at,value,bytes);return (at+bytes+3)&~(size_t)3;
}
static size_t tree(uint8_t *p,const char *a,size_t an,const char *b,size_t bn,
                   size_t *ap,size_t *bp) {
    memset(p,0,2048);size_t at=56;
    at=begin(p,at,"");at=begin(p,at,"chosen");at=prop(p,at,a,an,ap);
    put32(p+at,2);at+=4;at=begin(p,at,"chosen@0");at=prop(p,at,b,bn,bp);
    put32(p+at,2);put32(p+at+4,2);put32(p+at+8,9);at+=12;
    memcpy(p+at,"bootargs",9);size_t total=at+9;
    put32(p,0xd00dfeedu);put32(p+4,(uint32_t)total);put32(p+8,56);
    put32(p+12,(uint32_t)at);put32(p+16,40);put32(p+20,17);put32(p+24,16);
    put32(p+32,9);put32(p+36,(uint32_t)(at-56));return total;
}
static void fdt_cases(void) {
    uint8_t p[2048],copy[2048];size_t a,b,n;xpl_bootargs_stats s;
    const char *first="startup=0x00004004 warmboot=0 androidboot.mode=charger console=tty0";
    const char *second="startup=4005 warmboot=0 androidboot.mode=normal";
    n=tree(p,first,strlen(first)+1,second,strlen(second),&a,&b);
    memcpy(copy,p,sizeof p);
    CHECK(xpl_bootargs_clean_fdt(p,n,&s)==0);
    CHECK(s.properties==2&&s.chosen_nodes==2&&s.startup_masked==2&&s.removed==1);
    CHECK(s.first_startup==0x4004&&s.nonterminated==1);
    CHECK(!memcmp(p+a,"startup=0x00000000",18));
    CHECK(!memcmp(p+b,"startup=0001",12));
    CHECK(!memcmp(p+a+18,copy+a+18,10)); /* warmboot is not fabricated */
    for(size_t i=0;i<sizeof p;i++) {
        if((i<a||i>=a+strlen(first)+1)&&(i<b||i>=b+strlen(second)))CHECK(p[i]==copy[i]);
    }
    memcpy(copy,p,sizeof p);
    CHECK(xpl_bootargs_clean_fdt(p,n,&s)==0&&s.startup_masked==0&&s.removed==0);
    CHECK(!memcmp(copy,p,sizeof p));
    /* Two duplicate properties in one chosen node, plus an alias chosen node. */
    uint8_t duplicate[2048];size_t at=56,c;
    memset(duplicate,0,sizeof duplicate);at=begin(duplicate,at,"");at=begin(duplicate,at,"chosen");
    at=prop(duplicate,at,"startup=+0X4004",15,&a);
    at=prop(duplicate,at,"startup=ffffffffffffffff",25,&b);
    put32(duplicate+at,2);at+=4;at=begin(duplicate,at,"chosen@0");
    at=prop(duplicate,at,"startup=bad! startup=0x0000",27,&c);
    put32(duplicate+at,2);put32(duplicate+at+4,2);put32(duplicate+at+8,9);at+=12;
    memcpy(duplicate+at,"bootargs",9);n=at+9;
    put32(duplicate,0xd00dfeedu);put32(duplicate+4,(uint32_t)n);put32(duplicate+8,56);
    put32(duplicate+12,(uint32_t)at);put32(duplicate+16,40);put32(duplicate+20,17);
    put32(duplicate+24,16);put32(duplicate+32,9);put32(duplicate+36,(uint32_t)(at-56));
    CHECK(xpl_bootargs_clean_fdt(duplicate,n,&s)==0&&s.properties==3&&s.startup_masked==2);
    CHECK(!strcmp((char *)duplicate+a,"startup=+0X0000"));
    CHECK(!strcmp((char *)duplicate+b,"startup=ffffffffffffbffb"));
    CHECK(!strcmp((char *)duplicate+c,"startup=bad! startup=0x0000"));
    /* Validate the ENTIRE tree before changing an earlier charger property. */
    n=tree(p,first,strlen(first)+1,second,strlen(second)+1,&a,&b);
    put32(p+b-12,0xffffffffu);memcpy(copy,p,sizeof p);
    CHECK(xpl_bootargs_clean_fdt(p,n,&s)==XPL_BOOTARGS_ARGUMENT);
    CHECK(!memcmp(copy,p,sizeof p));
    n=tree(p,first,strlen(first)+1,second,strlen(second)+1,&a,&b);
    uint32_t strings=get32(p+12);put32(p+strings-4,2);memcpy(copy,p,sizeof p);
    CHECK(xpl_bootargs_clean_fdt(p,n,&s)==XPL_BOOTARGS_ARGUMENT);
    CHECK(!memcmp(copy,p,sizeof p));
    n=tree(p,first,strlen(first)+1,second,strlen(second)+1,&a,&b);
    put32(p+12,60);memcpy(copy,p,sizeof p);
    CHECK(xpl_bootargs_clean_fdt(p,n,&s)==XPL_BOOTARGS_ARGUMENT);
    CHECK(!memcmp(copy,p,sizeof p));
    CHECK(xpl_bootargs_clean_fdt(0,n,&s)==XPL_BOOTARGS_ARGUMENT);
    CHECK(xpl_bootargs_clean_fdt(p,n,0)==XPL_BOOTARGS_ARGUMENT);
    uint32_t rng=0xf104bf67u;
    for(unsigned run=0;run<12000;run++) {
        n=tree(p,first,strlen(first)+1,second,strlen(second)+1,&a,&b);
        rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;
        size_t where=rng%n;p[where]^=(uint8_t)(1u<<(run%8));memcpy(copy,p,sizeof p);
        int r=xpl_bootargs_clean_fdt(p,n,&s);
        if(r)CHECK(!memcmp(copy,p,sizeof p));
        else {
            CHECK(!memcmp(p+n,copy+n,sizeof p-n));
            memcpy(copy,p,sizeof p);
            CHECK(xpl_bootargs_clean_fdt(p,n,&s)==0&&s.removed==0&&s.startup_masked==0);
            CHECK(!memcmp(copy,p,sizeof p));
        }
    }
}
int main(void) {
    /* Other modes and substrings are intentionally preserved. */
    example("","",0);
    example("androidboot.mode=normal quiet","androidboot.mode=normal quiet",0);
    example("androidboot.mode=charger",BLANK_CHARGER,1);
    example("x=1 androidboot.mode=charger y=2","x=1 " BLANK_CHARGER " y=2",1);
    example("\tandroidboot.mode=charger\n","\t" BLANK_CHARGER "\n",1);
    example("androidboot.mode=charger_noboot",BLANK_CHARGER "       ",1);
    example("androidboot.mode=charger androidboot.mode=charger",BLANK_CHARGER " " BLANK_CHARGER,2);
    example("xandroidboot.mode=charger androidboot.mode=charg","xandroidboot.mode=charger androidboot.mode=charg",0);
    example("foo=androidboot.mode=charger","foo=androidboot.mode=charger",0);
    uint8_t x[128],original[128];unsigned removed=99;
    memset(x,0xa5,sizeof x);memcpy(original,x,sizeof x);
    CHECK(xpl_bootargs_interactive(x,sizeof x,&removed)==XPL_BOOTARGS_UNTERMINATED);
    CHECK(!memcmp(x,original,sizeof x));
    CHECK(xpl_bootargs_interactive(x,0,&removed)==XPL_BOOTARGS_ARGUMENT);
    CHECK(xpl_bootargs_interactive(x,65537,&removed)==XPL_BOOTARGS_ARGUMENT);
    CHECK(xpl_bootargs_interactive(0,1,&removed)==XPL_BOOTARGS_ARGUMENT);
    CHECK(xpl_bootargs_interactive(x,1,0)==XPL_BOOTARGS_ARGUMENT);
    uint32_t rng=0x18b097a3u;
    for(unsigned run=0;run<20000;run++) {
        uint8_t buffer[256],copy[256];unsigned offset=run%17u,n=1u+run%128u;
        memset(buffer,0xa5,sizeof buffer);
        for(unsigned i=0;i<n;i++) {rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;buffer[offset+i]=(uint8_t)rng;}
        if(run&1u)buffer[offset+n-1]=0;
        memcpy(copy,buffer,sizeof buffer);
        int r=xpl_bootargs_interactive(buffer+offset,n,&removed);
        if(r)CHECK(!memcmp(buffer,copy,sizeof buffer));
        else {
            uint8_t *nul=memchr(copy+offset,0,n);CHECK(nul!=0);
            size_t end=(size_t)(nul-copy);
            CHECK(!memcmp(buffer,copy,offset));
            CHECK(!memcmp(buffer+end,copy+end,sizeof buffer-end));
            /* The operation never expands a value or inserts non-space bytes. */
            for(size_t i=offset;i<end;i++)CHECK(buffer[i]==copy[i]||buffer[i]==' ');
            CHECK(xpl_bootargs_interactive(buffer+offset,n,&removed)==0&&removed==0);
        }
    }
    fdt_cases();
    printf("Bootargs/FDT bounds/preservation/idempotence PASS: %u checks\n",checks);
    return 0;
}
