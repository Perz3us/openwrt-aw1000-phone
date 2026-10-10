/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2026 perz3us */
#ifndef FXS_PROTO_H
#define FXS_PROTO_H

#define FXS_SI3226X_ID		0xc3
#define FXS_REG_ID		0
#define FXS_REG_MSTRSTAT	3
#define FXS_REG_RAMSTAT		4
#define FXS_REG_RAM_ADDR_HI	5
#define FXS_REG_RAM_D0		6
#define FXS_REG_RAM_D1		7
#define FXS_REG_RAM_D2		8
#define FXS_REG_RAM_D3		9
#define FXS_REG_RAM_ADDR_LO	10
#define FXS_REG_PCMMODE		11
#define FXS_REG_IRQ0		17
#define FXS_REG_IRQ4		21
#define FXS_REG_LINEFEED	30
#define FXS_REG_UAM		126

static inline u8 fxs_ctrl_byte(int read, int reg, u8 ch)
{
	return (read ? 0x40 : 0) | (reg ? 0x20 : 0) | ((ch & 1) << 4);
}

static inline u8 fxs_ram_addr_hi(u16 addr)
{
	return (addr >> 3) & 0xe0;
}

static inline u8 fxs_ram_addr_lo(u16 addr)
{
	return addr & 0xff;
}

static inline void fxs_ram_pack(u32 val, u8 out[4])
{
	u32 v = val << 3;

	out[0] = v & 0xf8;
	out[1] = (v >> 8) & 0xff;
	out[2] = (v >> 16) & 0xff;
	out[3] = (v >> 24) & 0xff;
}

static inline u32 fxs_ram_unpack(const u8 in[4])
{
	u32 v = in[0] | (in[1] << 8) | (in[2] << 16) | ((u32)in[3] << 24);

	return v >> 3;
}

#endif
