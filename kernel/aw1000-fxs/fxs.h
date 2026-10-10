/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2026 perz3us */
#ifndef FXS_H
#define FXS_H

#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/timer.h>
#include <linux/gpio/consumer.h>
#include "fxs_uapi.h"

struct platform_device;

struct fxs_dev {
	void __iomem *gcc;
	void __iomem *adss_glb;
	void __iomem *adss_cc;
	void __iomem *pcm;
	void __iomem *tlmm;

	struct platform_device *pdev;
	struct gpio_desc *rst, *irq_gpio, *cs, *sclk, *miso, *mosi;
	int irq;

	struct mutex lock;
	wait_queue_head_t evq;
	atomic_t irq_count;
	atomic_t irq_masked;
	unsigned int irq_seen;
	bool open;

	struct timer_list ring_wd;

	struct task_struct *br_task;
	u32 br_moved, br_resync, br_ticks, br_status, br_peak_a, br_peak_b;
	u8 br_ai, br_ao, br_bi, br_bo, br_words;

	u32 save[32];
	bool clk_on, pll_on, adss_on, pcm_on, pins_on, spi_on, spi_drv_set;
};

int fxs_clk_enable(struct fxs_dev *d);
void fxs_clk_disable(struct fxs_dev *d);
int fxs_pcm_start(struct fxs_dev *d);
void fxs_pcm_stop(struct fxs_dev *d);
int fxs_pcm_rd(struct fxs_dev *d, u32 off, u32 *val);
int fxs_pcm_wr(struct fxs_dev *d, u32 off, u32 val);
int fxs_pcm_capture(struct fxs_dev *d, u32 bitmap, u32 ms, u16 *w, u32 *n);
int fxs_pcm_bridge(struct fxs_dev *d, u32 arg);
int fxs_spi_init(struct fxs_dev *d);
void fxs_spi_exit(struct fxs_dev *d);
void fxs_slic_reset(struct fxs_dev *d, bool assert);
void fxs_irq_ack(struct fxs_dev *d);
int fxs_reg_read(struct fxs_dev *d, u8 ch, u8 addr, u8 *val);
int fxs_reg_write(struct fxs_dev *d, u8 ch, u8 addr, u8 val);
int fxs_ram_read(struct fxs_dev *d, u8 ch, u16 addr, u32 *val);
int fxs_ram_write(struct fxs_dev *d, u8 ch, u16 addr, u32 val);

#endif
