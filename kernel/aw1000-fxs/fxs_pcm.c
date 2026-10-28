// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 perz3us
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/kthread.h>
#include <linux/moduleparam.h>
#include <linux/sched.h>
#include "fxs.h"
#include "fxs_regs.h"

enum { P_TLMM = 16, P_BITMAP = 20, P_CTRL, P_OFFSET, P_DIV, P_INTEN, P_TH };

#define PCM_CTRL_BASE	((1 << 24) | (2 << 0))
#define BR_PRIME	24
#define BR_HIGH		192
#define BR_HIGH_WB	288
#define BR_ERR		0x6

static uint pcm_offset;
module_param(pcm_offset, uint, 0644);
static uint pcm_ctrl_or;
module_param(pcm_ctrl_or, uint, 0644);

static void pcm_hw_setup(struct fxs_dev *d, u32 ctrl, u32 bitmap)
{
	u32 cnt;
	int i;

	writel(0, d->pcm + PCM_START);
	usleep_range(250, 500);
	writel(1, d->pcm + PCM_DIVIDER);
	writel(ctrl | (pcm_ctrl_or & 0x0e000010), d->pcm + PCM_CTRL);
	writel(pcm_offset, d->pcm + PCM_OFFSET);
	writel(bitmap, d->pcm + PCM_BITMAP);
	for (i = 0; i < 2 * FXS_CAP_MAX; i++) {
		cnt = readl(d->pcm + PCM_FIFO_CNT);
		if (!((cnt >> 9) & 0x1ff))
			break;
		readl(d->pcm + PCM_RX16);
	}
	if (readl(d->pcm + PCM_FIFO_CNT) & 1)
		writel(0, d->pcm + PCM_TX16);
}

int fxs_pcm_start(struct fxs_dev *d)
{
	u32 *s = d->save;
	int i;

	if (d->pcm_on)
		return 0;
	for (i = 0; i < 4; i++)
		s[P_TLMM + i] = readl(d->tlmm + TLMM_CFG(33 + i));
	s[P_BITMAP] = readl(d->pcm + PCM_BITMAP);
	s[P_CTRL] = readl(d->pcm + PCM_CTRL);
	s[P_OFFSET] = readl(d->pcm + PCM_OFFSET);
	s[P_DIV] = readl(d->pcm + PCM_DIVIDER);
	s[P_INTEN] = readl(d->pcm + PCM_INT_ENABLE);
	s[P_TH] = readl(d->pcm + PCM_TH);

	/* 16 slots x 16 bit, TX on falling edge, RX on rising edge */
	writel(1, d->pcm + PCM_DIVIDER);
	writel(PCM_CTRL_BASE, d->pcm + PCM_CTRL);
	writel(pcm_offset, d->pcm + PCM_OFFSET);
	writel(BIT(0), d->pcm + PCM_BITMAP);

	/* gpio33-36: function 1 (pcm), 8 mA, pull-up */
	for (i = 0; i < 4; i++)
		writel((1 << 2) | TLMM_DRV_8MA | 3, d->tlmm + TLMM_CFG(33 + i));
	d->pins_on = true;

	writel(1, d->pcm + PCM_START);
	d->pcm_on = true;
	return 0;
}

void fxs_pcm_stop(struct fxs_dev *d)
{
	u32 *s = d->save;
	int i;

	fxs_pcm_bridge(d, 0);
	if (d->pcm_on) {
		writel(0, d->pcm + PCM_START);
		writel(s[P_BITMAP], d->pcm + PCM_BITMAP);
		writel(s[P_CTRL], d->pcm + PCM_CTRL);
		writel(s[P_OFFSET], d->pcm + PCM_OFFSET);
		writel(s[P_DIV], d->pcm + PCM_DIVIDER);
		writel(s[P_INTEN], d->pcm + PCM_INT_ENABLE);
		writel(s[P_TH], d->pcm + PCM_TH);
		d->pcm_on = false;
	}
	if (d->pins_on) {
		for (i = 0; i < 4; i++)
			writel(s[P_TLMM + i], d->tlmm + TLMM_CFG(33 + i));
		d->pins_on = false;
	}
}

static bool pcm_reg_ok(struct fxs_dev *d, u32 off)
{
	return d->pcm_on && off <= PCM_REG_LAST && !(off & 3);
}

int fxs_pcm_rd(struct fxs_dev *d, u32 off, u32 *val)
{
	if (!pcm_reg_ok(d, off))
		return -EINVAL;
	*val = readl(d->pcm + off);
	return 0;
}

int fxs_pcm_wr(struct fxs_dev *d, u32 off, u32 val)
{
	if (!pcm_reg_ok(d, off) || off == PCM_DIVIDER || d->br_task)
		return -EINVAL;
	writel(val, d->pcm + off);
	return 0;
}

static void pcm_restore_idle(struct fxs_dev *d)
{
	pcm_hw_setup(d, PCM_CTRL_BASE, BIT(0));
	writel(1, d->pcm + PCM_START);
}

int fxs_pcm_capture(struct fxs_dev *d, u32 bitmap, u32 ms, u16 *w, u32 *n)
{
	u32 cnt, i;

	if (!d->pcm_on || !bitmap || bitmap > 0xffff || ms < 1 || ms > 40)
		return -EINVAL;
	fxs_pcm_bridge(d, 0);
	pcm_hw_setup(d, PCM_CTRL_BASE | PCM_CTRL_CPU, bitmap);
	writel(1, d->pcm + PCM_START);
	msleep(ms);
	writel(0, d->pcm + PCM_START);
	cnt = (readl(d->pcm + PCM_FIFO_CNT) >> 9) & 0x1ff;
	cnt = min_t(u32, cnt, FXS_CAP_MAX);
	for (i = 0; i < cnt; i++)
		w[i] = readl(d->pcm + PCM_RX16) & 0xffff;
	*n = cnt;
	pcm_restore_idle(d);
	return 0;
}

static void br_sync(struct fxs_dev *d)
{
	u32 m = BIT(d->br_words) - 1;
	int i, h, j;

	pcm_hw_setup(d, PCM_CTRL_BASE | PCM_CTRL_CPU, d->br_wb ? m | (m << 8) : m);
	for (i = 0; i < BR_PRIME; i++)
		for (h = 0; h <= d->br_wb; h++)
			for (j = 0; j < d->br_words; j++)
				writel(((j + 8 * h) << 16) | 0xffff, d->pcm + PCM_TX16);
	writel(1, d->pcm + PCM_START);
	writel(BR_ERR, d->pcm + PCM_INT_STATUS);
}

static u16 br_get(u64 f, unsigned int bits, unsigned int pos)
{
	return (f >> (bits - pos - 16)) & 0xffff;
}

static u64 br_put(u64 f, unsigned int bits, unsigned int pos, u16 v)
{
	unsigned int sh = bits - pos - 16;

	return (f & ~(0xffffULL << sh)) | ((u64)v << sh);
}

#define AU_MASK		(FXS_AU_RING - 1)

u32 fxs_au_period(struct fxs_dev *d)
{
	return 160 << d->br_wb;
}

u32 fxs_au_avail(struct fxs_dev *d)
{
	u32 n;

	spin_lock(&d->au_lock);
	n = d->au_rx_head - d->au_rx_tail;
	spin_unlock(&d->au_lock);
	return n;
}

u32 fxs_au_read(struct fxs_dev *d, u32 *buf, u32 max)
{
	u32 n = 0;

	spin_lock(&d->au_lock);
	while (n < max && d->au_rx_tail != d->au_rx_head)
		buf[n++] = d->au_rx[d->au_rx_tail++ & AU_MASK];
	spin_unlock(&d->au_lock);
	return n;
}

void fxs_au_write(struct fxs_dev *d, const u32 *buf, u32 n)
{
	u32 i;

	spin_lock(&d->au_lock);
	for (i = 0; i < n; i++) {
		if (d->au_tx_head - d->au_tx_tail == FXS_AU_RING) {
			d->au_drop += n - i;
			break;
		}
		d->au_tx[d->au_tx_head++ & AU_MASK] = buf[i];
	}
	spin_unlock(&d->au_lock);
}

static u32 au_swap(struct fxs_dev *d, u16 a, u16 b, u32 *ready)
{
	u32 o = 0;

	spin_lock(&d->au_lock);
	if (d->au_rx_head - d->au_rx_tail == FXS_AU_RING) {
		d->au_rx_tail++;
		d->au_over++;
	}
	d->au_rx[d->au_rx_head++ & AU_MASK] = a | (u32)b << 16;
	if (d->au_tx_tail != d->au_tx_head)
		o = d->au_tx[d->au_tx_tail++ & AU_MASK];
	else
		d->au_under++;
	*ready = d->au_rx_head - d->au_rx_tail;
	spin_unlock(&d->au_lock);
	return o;
}

static void au_reset(struct fxs_dev *d)
{
	spin_lock(&d->au_lock);
	d->au_rx_head = d->au_rx_tail = d->au_tx_head = d->au_tx_tail = 0;
	d->au_under = d->au_over = d->au_drop = 0;
	spin_unlock(&d->au_lock);
}

static int br_thread(void *arg)
{
	struct fxs_dev *d = arg;
	unsigned int k = d->br_words, bits = 16 * k;
	u32 high = d->br_wb ? BR_HIGH_WB : BR_HIGH;
	u32 period = fxs_au_period(d);
	u16 cur[4] = { 0 };
	u32 have = 0;

	sched_set_fifo_low(current);
	while (!kthread_should_stop()) {
		u32 cnt, rx, tx, st, i, j;

		usleep_range(900, 1100);
		d->br_ticks++;
		st = readl(d->pcm + PCM_INT_STATUS);
		d->br_status = st;
		if (st & BR_ERR) {
			d->br_resync++;
			writel(st & BR_ERR, d->pcm + PCM_INT_STATUS);
		}
		cnt = readl(d->pcm + PCM_FIFO_CNT);
		rx = min_t(u32, (cnt >> 9) & 0x1ff, high);
		tx = cnt & 0x1ff;
		for (i = 0; i < rx; i++) {
			u32 w = readl(d->pcm + PCM_RX16);
			u32 t = (w >> 16) & 0x1f, hb = 0;
			u64 in = 0, out = GENMASK_ULL(bits - 1, 0);
			u16 a, b;

			if (d->br_wb && t >= 8) {
				hb = 8;
				t -= 8;
			}
			if (t >= k)
				continue;
			if (t == 0)
				have = 0;
			cur[t] = w & 0xffff;
			have |= BIT(t);
			if (t != k - 1 || have != BIT(k) - 1)
				continue;
			have = 0;
			if (tx + k > high)
				continue;
			for (j = 0; j < k; j++)
				in = (in << 16) | cur[j];
			a = br_get(in, bits, d->br_ai);
			b = br_get(in, bits, d->br_bi);
			d->br_peak_a = max_t(u32, d->br_peak_a, abs((s16)a));
			d->br_peak_b = max_t(u32, d->br_peak_b, abs((s16)b));
			if (d->br_user) {
				u32 ready, o = au_swap(d, a, b, &ready);

				if (ready >= period)
					wake_up_interruptible(&d->auq);
				a = o >> 16;
				b = o & 0xffff;
			}
			out = br_put(out, bits, d->br_ao, b);
			out = br_put(out, bits, d->br_bo, a);
			for (j = 0; j < k; j++)
				writel(((j + hb) << 16) | ((out >> (16 * (k - 1 - j))) & 0xffff),
				       d->pcm + PCM_TX16);
			tx += k;
			d->br_moved++;
		}
	}
	return 0;
}

int fxs_pcm_bridge(struct fxs_dev *d, u32 arg)
{
	u32 ai = arg & 0x3f, ao = (arg >> 8) & 0x3f;
	u32 bi = (arg >> 16) & 0x3f, bo = (arg >> 24) & 0x3f;

	if (d->br_task) {
		kthread_stop(d->br_task);
		d->br_task = NULL;
		if (d->pcm_on)
			pcm_restore_idle(d);
	}
	WRITE_ONCE(d->br_user, 0);
	wake_up_interruptible(&d->auq);
	if (!(arg & BIT(31)))
		return 0;
	if (!d->pcm_on || max(max(ai, ao), max(bi, bo)) > 48)
		return -EINVAL;
	d->br_ai = ai;
	d->br_ao = ao;
	d->br_bi = bi;
	d->br_bo = bo;
	d->br_words = (max(max(ai, ao), max(bi, bo)) + 15) / 16 + 1;
	d->br_wb = !!(arg & BIT(30));
	au_reset(d);
	d->br_user = !!(arg & BIT(7));
	d->br_moved = d->br_resync = d->br_ticks = d->br_status = 0;
	br_sync(d);
	d->br_task = kthread_run(br_thread, d, "fxs-bridge");
	if (IS_ERR(d->br_task)) {
		int e = PTR_ERR(d->br_task);

		d->br_task = NULL;
		pcm_restore_idle(d);
		return e;
	}
	return 0;
}
