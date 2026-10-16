/* SPDX-License-Identifier: MIT. SPMI controller writes request PMIC reads only. */
#include "xpl_hw_internal.h"

static int readable_register(uint16_t addr)
{
    if(addr==0x0810)return 1;
    if((addr&0xff00)==0xc100||(addr&0xff00)==0xc200){
        uint16_t off=addr&255;
        return off==1||off==4||off==8||off==0x40||off==0x46;
    }
    return 0;
}
static enum xpl_hw_status read8(const struct xpl_hw_config *c,const struct xpl_hw_io *io,
    struct xpl_hw_keys *keys,uint16_t addr,uint8_t *value,struct xpl_hw_diag *diag)
{
    uint32_t status,data;uint64_t bank=c->spmi.base+0x800;
    if(!readable_register(addr)||!keys->poll_limit||keys->poll_limit>1000||!io->fence)return hw_fail(diag,XPL_HW_ARGUMENT,addr);
    /* v1 EXT_READL opcode1, SID0, literal 16-bit PMIC address, one byte. */
    if(!hw_write(io,bank,(1u<<27)|((uint32_t)addr<<4),diag))return XPL_HW_MMIO_FAULT;
    io->fence(io->context);
    for(uint32_t i=0;i<keys->poll_limit;i++){
        if(!hw_read(io,bank+8,&status,diag))return XPL_HW_MMIO_FAULT;
        if(status&14u)return hw_fail(diag,XPL_HW_SPMI_ERROR,addr);
        if(status&1u){
            if(!hw_read(io,bank+0x18,&data,diag))return XPL_HW_MMIO_FAULT;
            *value=(uint8_t)data;return XPL_HW_OK;
        }
    }
    return hw_fail(diag,XPL_HW_SPMI_TIMEOUT,addr);
}

enum xpl_hw_status xpl_hw_keys_init(const struct xpl_hw_config *c,const struct xpl_hw_io *io,
    uint32_t poll_limit,struct xpl_hw_keys *keys,struct xpl_hw_diag *diag)
{
    enum xpl_hw_status status;
    hw_diag_init(diag,XPL_HW_PHASE_SPMI);if(keys)hw_zero(keys,sizeof(*keys));
    if(!keys||!hw_resources_valid(c)||!io||!io->read32||!io->write32||!io->fence||!poll_limit||poll_limit>1000)return hw_fail(diag,XPL_HW_ARGUMENT,0);
    keys->poll_limit=poll_limit;
    if(!hw_read(io,c->spmi.base,&keys->arbiter_version,diag))return XPL_HW_MMIO_FAULT;
    if(!keys->arbiter_version||keys->arbiter_version>=0x20010000u)return hw_fail(diag,XPL_HW_SPMI_UNSUPPORTED,keys->arbiter_version);
    if(diag)diag->phase=XPL_HW_PHASE_KEYS;
    for(unsigned i=0;i<2;i++){
        uint16_t base=i?0xc100:0xc200;uint8_t revision,type,mode,enable;
        status=read8(c,io,keys,base+1,&revision,diag);if(status)return status;
        status=read8(c,io,keys,base+4,&type,diag);if(status)return status;
        status=read8(c,io,keys,base+0x40,&mode,diag);if(status)return status;
        status=read8(c,io,keys,base+0x46,&enable,diag);if(status)return status;
        if(type!=0x10||(mode&0x70)||!(enable&0x80))return hw_fail(diag,XPL_HW_KEYS_UNCONFIGURED,base);
        keys->gpio_enable_mask[i]=revision?0x80:2;
    }
    keys->ready=1;return XPL_HW_OK;
}

enum xpl_hw_status xpl_hw_keys_poll(const struct xpl_hw_config *c,const struct xpl_hw_io *io,
    struct xpl_hw_keys *keys,unsigned *key_bits,struct xpl_hw_diag *diag)
{
    enum xpl_hw_status status;uint8_t up,down,power;
    hw_diag_init(diag,XPL_HW_PHASE_KEYS);if(key_bits)*key_bits=0;
    if(!hw_resources_valid(c)||!io||!io->read32||!io->write32||!io->fence||!keys||!keys->ready||!key_bits)return hw_fail(diag,XPL_HW_ARGUMENT,0);
    if(!keys->arbiter_version||keys->arbiter_version>=0x20010000u)return hw_fail(diag,XPL_HW_SPMI_UNSUPPORTED,keys->arbiter_version);
    status=read8(c,io,keys,0xc208,&up,diag);if(status)return status;keys->last_up=up;
    status=read8(c,io,keys,0xc108,&down,diag);if(status)return status;keys->last_down=down;
    status=read8(c,io,keys,0x0810,&power,diag);if(status)return status;keys->last_power=power;
    if(!(up&keys->gpio_enable_mask[0])||!(down&keys->gpio_enable_mask[1]))return hw_fail(diag,XPL_HW_KEYS_UNCONFIGURED,0);
    *key_bits=(!(up&1)?XPL_HW_KEY_UP:0)|(!(down&1)?XPL_HW_KEY_DOWN:0)|((power&1)?XPL_HW_KEY_POWER:0);
    return XPL_HW_OK;
}
