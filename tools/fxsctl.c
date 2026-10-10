// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 perz3us
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "../kernel/aw1000-fxs/fxs_uapi.h"

static unsigned long num(const char *s)
{
	return strtoul(s, NULL, 0);
}

static int need(int i, int n, int argc)
{
	if (i + n >= argc) {
		fprintf(stderr, "missing arguments\n");
		return -1;
	}
	return 0;
}

int main(int argc, char **argv)
{
	int fd, i;

	fd = open("/dev/fxs0", O_RDWR);
	if (fd < 0) {
		perror("open /dev/fxs0");
		return 1;
	}
	for (i = 1; i < argc; i++) {
		const char *c = argv[i];

		if (!strcmp(c, "rd")) {
			struct fxs_reg r;

			if (need(i, 2, argc))
				return 2;
			r.ch = num(argv[i + 1]);
			r.addr = num(argv[i + 2]);
			if (ioctl(fd, FXS_RD_REG, &r))
				return perror("rd"), 1;
			printf("rd %u %u 0x%02x\n", r.ch, r.addr, r.val);
			i += 2;
		} else if (!strcmp(c, "wr")) {
			struct fxs_reg r;

			if (need(i, 3, argc))
				return 2;
			r.ch = num(argv[i + 1]);
			r.addr = num(argv[i + 2]);
			r.val = num(argv[i + 3]);
			if (ioctl(fd, FXS_WR_REG, &r))
				return perror("wr"), 1;
			i += 3;
		} else if (!strcmp(c, "rdram")) {
			struct fxs_ram m;

			if (need(i, 2, argc))
				return 2;
			m.ch = num(argv[i + 1]);
			m.addr = num(argv[i + 2]);
			if (ioctl(fd, FXS_RD_RAM, &m))
				return perror("rdram"), 1;
			printf("rdram %u %u 0x%08x\n", m.ch, m.addr, m.val);
			i += 2;
		} else if (!strcmp(c, "reset")) {
			__u32 v;

			if (need(i, 1, argc))
				return 2;
			v = num(argv[++i]);
			if (ioctl(fd, FXS_RESET, &v))
				return perror("reset"), 1;
		} else if (!strcmp(c, "pcm")) {
			__u32 v;

			if (need(i, 1, argc))
				return 2;
			v = num(argv[++i]);
			if (ioctl(fd, FXS_PCM_ENABLE, &v))
				return perror("pcm"), 1;
		} else if (!strcmp(c, "pcmrd")) {
			struct fxs_pcmreg pr;

			if (need(i, 1, argc))
				return 2;
			pr.off = num(argv[++i]);
			if (ioctl(fd, FXS_PCM_RD, &pr))
				return perror("pcmrd"), 1;
			printf("pcmrd 0x%02x 0x%08x\n", pr.off, pr.val);
		} else if (!strcmp(c, "pcmwr")) {
			struct fxs_pcmreg pr;

			if (need(i, 2, argc))
				return 2;
			pr.off = num(argv[i + 1]);
			pr.val = num(argv[i + 2]);
			if (ioctl(fd, FXS_PCM_WR, &pr))
				return perror("pcmwr"), 1;
			i += 2;
		} else if (!strcmp(c, "ack")) {
			if (ioctl(fd, FXS_IRQ_ACK))
				return perror("ack"), 1;
		} else if (!strcmp(c, "sleep")) {
			if (need(i, 1, argc))
				return 2;
			usleep(num(argv[++i]) * 1000);
		} else if (!strcmp(c, "wait")) {
			struct fxs_event ev;

			if (read(fd, &ev, sizeof(ev)) != sizeof(ev))
				return perror("wait"), 1;
			printf("irq %u\n", ev.irq_count);
		} else {
			fprintf(stderr, "unknown command %s\n", c);
			return 2;
		}
	}
	close(fd);
	return 0;
}
