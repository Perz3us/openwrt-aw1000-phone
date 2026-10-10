/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/* Copyright (C) 2026 perz3us */
#ifndef FXS_UAPI_H
#define FXS_UAPI_H

#include <linux/types.h>
#include <linux/ioctl.h>

struct fxs_reg {
	__u8 ch;
	__u8 addr;
	__u8 val;
};

struct fxs_ram {
	__u8 ch;
	__u16 addr;
	__u32 val;
};

struct fxs_pcmreg {
	__u32 off;
	__u32 val;
};

#define FXS_CAP_MAX	320

struct fxs_pcmcap {
	__u32 bitmap;
	__u32 ms;
	__u32 n;
	__u16 w[FXS_CAP_MAX];
};

struct fxs_pcmstat {
	__u32 moved;
	__u32 resync;
	__u32 ticks;
	__u32 status;
	__u32 peak_a;
	__u32 peak_b;
};

struct fxs_event {
	__u32 irq_count;
};

#define FXS_IOC_MAGIC	'F'
#define FXS_RD_REG	_IOWR(FXS_IOC_MAGIC, 1, struct fxs_reg)
#define FXS_WR_REG	_IOW(FXS_IOC_MAGIC, 2, struct fxs_reg)
#define FXS_RD_RAM	_IOWR(FXS_IOC_MAGIC, 3, struct fxs_ram)
#define FXS_WR_RAM	_IOW(FXS_IOC_MAGIC, 4, struct fxs_ram)
#define FXS_RESET	_IOW(FXS_IOC_MAGIC, 5, __u32)
#define FXS_RING_ARM	_IOW(FXS_IOC_MAGIC, 6, __u32)
#define FXS_PCM_ENABLE	_IOW(FXS_IOC_MAGIC, 7, __u32)
#define FXS_IRQ_ACK	_IO(FXS_IOC_MAGIC, 8)
#define FXS_PCM_RD	_IOWR(FXS_IOC_MAGIC, 9, struct fxs_pcmreg)
#define FXS_PCM_WR	_IOW(FXS_IOC_MAGIC, 10, struct fxs_pcmreg)
#define FXS_PCM_CAPTURE	_IOWR(FXS_IOC_MAGIC, 11, struct fxs_pcmcap)
#define FXS_PCM_BRIDGE	_IOW(FXS_IOC_MAGIC, 12, __u32)
#define FXS_PCM_STAT	_IOR(FXS_IOC_MAGIC, 13, struct fxs_pcmstat)

#endif
