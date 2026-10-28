// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 perz3us
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include "pcmsock.h"

static int afd = -1, lfd = -1, cfd = -1, wb, quit, live, running;
static uint32_t stats[4];
static pthread_t thread;
static char sock_path[108];

int pcm_downsample(struct pcm_down *d, const int16_t *in, int n, int16_t *out)
{
	int i, k = 0;

	for (i = 0; i < n; i++) {
		if (!d->have) {
			d->held = in[i];
			d->have = 1;
			continue;
		}
		out[k++] = (int16_t)((d->prev + 2 * d->held + in[i]) / 4);
		d->prev = in[i];
		d->have = 0;
	}
	return k;
}

int pcm_upsample(struct pcm_up *u, const int16_t *in, int n, int16_t *out)
{
	int i;

	for (i = 0; i < n; i++) {
		out[2 * i] = (int16_t)((u->last + in[i]) / 2);
		out[2 * i + 1] = in[i];
		u->last = in[i];
	}
	return 2 * n;
}

static void drop_client(void)
{
	if (cfd >= 0)
		close(cfd);
	cfd = -1;
}

static void from_modem(int16_t *frame, int *nf, struct pcm_down *down)
{
	uint32_t pairs[640];
	int16_t s[640], d[640];
	ssize_t r = read(afd, pairs, sizeof(pairs));
	int i, n, k;

	if (r <= 0)
		return;
	n = r / 4;
	for (i = 0; i < n; i++)
		s[i] = (int16_t)(pairs[i] & 0xffff);
	k = wb ? pcm_downsample(down, s, n, d) : n;
	for (i = 0; i < k; i++) {
		frame[(*nf)++] = wb ? d[i] : s[i];
		if (*nf < PCMSOCK_FRAME)
			continue;
		*nf = 0;
		if (cfd >= 0 && send(cfd, frame, PCMSOCK_FRAME * 2, MSG_DONTWAIT | MSG_NOSIGNAL) == PCMSOCK_FRAME * 2)
			stats[0]++;
		else
			stats[3]++;
	}
}

static void to_modem(int on, struct pcm_up *up)
{
	int16_t in[PCMSOCK_FRAME * 2], s[PCMSOCK_FRAME * 4];
	uint32_t pairs[PCMSOCK_FRAME * 4];
	ssize_t r = recv(cfd, in, sizeof(in), MSG_DONTWAIT);
	int i, n;

	if (r == 0 || (r < 0 && errno != EAGAIN && errno != EINTR)) {
		drop_client();
		return;
	}
	if (r <= 0 || !on)
		return;
	stats[1]++;
	n = r / 2;
	if (wb)
		n = pcm_upsample(up, in, n, s);
	else
		memcpy(s, in, n * 2);
	for (i = 0; i < n; i++)
		pairs[i] = (uint16_t)s[i];
	if (write(afd, pairs, n * 4) < 0)
		stats[3]++;
}

static void *run(void *arg)
{
	struct pcm_down down = { 0 };
	struct pcm_up up = { 0 };
	int16_t frame[PCMSOCK_FRAME];
	int nf = 0, was = 0;

	(void)arg;
	while (!__atomic_load_n(&quit, __ATOMIC_ACQUIRE)) {
		struct pollfd p[3] = { { .fd = lfd, .events = POLLIN }, { .fd = cfd, .events = POLLIN },
				       { .fd = afd, .events = POLLIN } };
		int on = __atomic_load_n(&live, __ATOMIC_ACQUIRE);

		if (on != was) {
			memset(&down, 0, sizeof(down));
			memset(&up, 0, sizeof(up));
			nf = 0;
			was = on;
		}
		if (poll(p, on ? 3 : 2, on ? 40 : 200) <= 0)
			continue;
		if (p[0].revents & POLLIN) {
			int fd = accept4(lfd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);

			if (fd >= 0) {
				drop_client();
				cfd = fd;
				stats[2]++;
				continue;
			}
		}
		if (cfd >= 0 && (p[1].revents & (POLLIN | POLLHUP | POLLERR)))
			to_modem(on, &up);
		if (on && (p[2].revents & POLLIN))
			from_modem(frame, &nf, &down);
	}
	return NULL;
}

int pcmsock_start(const char *path, const char *dev, int wideband)
{
	struct sockaddr_un a = { .sun_family = AF_UNIX };
	char dir[108], *slash;
	int e;

	if (strlen(path) >= sizeof(a.sun_path))
		return -ENAMETOOLONG;
	snprintf(sock_path, sizeof(sock_path), "%s", path);
	snprintf(dir, sizeof(dir), "%s", path);
	if ((slash = strrchr(dir, '/')) && slash != dir) {
		*slash = 0;
		mkdir(dir, 0755);
	}
	if ((afd = open(dev, O_RDWR | O_CLOEXEC)) < 0)
		return -errno;
	lfd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
	if (lfd < 0)
		goto fail;
	snprintf(a.sun_path, sizeof(a.sun_path), "%s", path);
	unlink(path);
	if (bind(lfd, (struct sockaddr *)&a, sizeof(a)) || chmod(path, 0600) || listen(lfd, 1))
		goto fail;
	wb = wideband;
	quit = 0;
	if (pthread_create(&thread, NULL, run, NULL))
		goto fail;
	running = 1;
	return 0;
fail:
	e = errno ? -errno : -EIO;
	pcmsock_stop();
	return e;
}

void pcmsock_live(int on)
{
	__atomic_store_n(&live, on, __ATOMIC_RELEASE);
}

void pcmsock_stop(void)
{
	if (running) {
		__atomic_store_n(&quit, 1, __ATOMIC_RELEASE);
		pthread_join(thread, NULL);
		running = 0;
	}
	drop_client();
	if (lfd >= 0) {
		close(lfd);
		unlink(sock_path);
	}
	if (afd >= 0)
		close(afd);
	lfd = afd = -1;
}

void pcmsock_stats(uint32_t st[4])
{
	memcpy(st, stats, sizeof(stats));
}
