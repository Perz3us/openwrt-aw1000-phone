// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 perz3us
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "../../kernel/aw1000-fxs/fxs_uapi.h"
#include "fxsdev.h"

static int fd = -1;

static int io(unsigned long req, void *arg)
{
	return ioctl(fd, req, arg) ? -errno : 0;
}

static int rd(void *ctx, uint8_t ch, uint8_t reg, uint8_t *v)
{
	struct fxs_reg r = { .ch = ch, .addr = reg };
	int e = io(FXS_RD_REG, &r);

	(void)ctx;
	*v = r.val;
	return e;
}

static int wr(void *ctx, uint8_t ch, uint8_t reg, uint8_t v)
{
	struct fxs_reg r = { .ch = ch, .addr = reg, .val = v };

	(void)ctx;
	return io(FXS_WR_REG, &r);
}

static int rdram(void *ctx, uint8_t ch, uint16_t a, uint32_t *v)
{
	struct fxs_ram m = { .ch = ch, .addr = a };
	int e = io(FXS_RD_RAM, &m);

	(void)ctx;
	*v = m.val;
	return e;
}

static int wrram(void *ctx, uint8_t ch, uint16_t a, uint32_t v)
{
	struct fxs_ram m = { .ch = ch, .addr = a, .val = v };

	(void)ctx;
	return io(FXS_WR_RAM, &m);
}

static void delay(void *ctx, unsigned ms)
{
	(void)ctx;
	usleep(ms * 1000);
}

int fxsdev_open(struct slic *s, const char *path)
{
	fd = open(path, O_RDWR | O_CLOEXEC);
	if (fd < 0)
		return -errno;
	s->rd = rd;
	s->wr = wr;
	s->rdram = rdram;
	s->wrram = wrram;
	s->delay = delay;
	s->ctx = NULL;
	return 0;
}

void fxsdev_close(struct slic *s)
{
	(void)s;
	if (fd >= 0)
		close(fd);
	fd = -1;
}

int fxsdev_reset(struct slic *s, int assert_reset)
{
	__u32 v = !!assert_reset;

	(void)s;
	return io(FXS_RESET, &v);
}

int fxsdev_pcm(struct slic *s, int on)
{
	__u32 v = !!on;

	(void)s;
	return io(FXS_PCM_ENABLE, &v);
}

int fxsdev_ring_arm(struct slic *s, unsigned ms)
{
	__u32 v = ms;

	(void)s;
	return io(FXS_RING_ARM, &v);
}

int fxsdev_wait(struct slic *s, int timeout_ms)
{
	struct pollfd p = { .fd = fd, .events = POLLIN };
	struct fxs_event ev;
	int n;

	(void)s;
	n = poll(&p, 1, timeout_ms);
	if (n <= 0)
		return n < 0 ? -errno : 0;
	if (read(fd, &ev, sizeof(ev)) != sizeof(ev))
		return -errno;
	return 1;
}

int fxsdev_ack(struct slic *s)
{
	(void)s;
	return ioctl(fd, FXS_IRQ_ACK) ? -errno : 0;
}

int fxsdev_capture(struct slic *s, uint32_t bitmap, uint32_t ms, uint16_t *w, uint32_t *n)
{
	struct fxs_pcmcap *c = calloc(1, sizeof(*c));
	int e;

	(void)s;
	if (!c)
		return -ENOMEM;
	c->bitmap = bitmap;
	c->ms = ms;
	e = io(FXS_PCM_CAPTURE, c);
	if (!e) {
		*n = c->n;
		memcpy(w, c->w, c->n * sizeof(*w));
	}
	free(c);
	return e;
}

int fxsdev_bridge(struct slic *s, uint32_t arg)
{
	__u32 v = arg;

	(void)s;
	return io(FXS_PCM_BRIDGE, &v);
}

int fxsdev_stat(struct slic *s, uint32_t st[6])
{
	struct fxs_pcmstat p;
	int e = io(FXS_PCM_STAT, &p);

	(void)s;
	if (!e) {
		st[0] = p.moved;
		st[1] = p.resync;
		st[2] = p.ticks;
		st[3] = p.status;
		st[4] = p.peak_a;
		st[5] = p.peak_b;
	}
	return e;
}

int fxsdev_austat(struct slic *s, uint32_t st[4])
{
	struct fxs_austat a;
	int e = io(FXS_AUDIO_STAT, &a);

	(void)s;
	if (!e) {
		st[0] = a.under;
		st[1] = a.over;
		st[2] = a.drop;
		st[3] = a.avail;
	}
	return e;
}

int fxsdev_pcm_rd(struct slic *s, uint32_t off, uint32_t *v)
{
	struct fxs_pcmreg r = { .off = off };
	int e = io(FXS_PCM_RD, &r);

	(void)s;
	*v = r.val;
	return e;
}

int fxsdev_pcm_wr(struct slic *s, uint32_t off, uint32_t v)
{
	struct fxs_pcmreg r = { .off = off, .val = v };

	(void)s;
	return io(FXS_PCM_WR, &r);
}
