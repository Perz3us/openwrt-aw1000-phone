// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 perz3us
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "recorder.h"
#include "wav.h"

#define SIDE_RING	32768
#define SIDE_MASK	(SIDE_RING - 1)
#define REC_DEV		"/dev/fxs0rec"

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t thread;
static int16_t side[2][SIDE_RING];
static unsigned int head[2], tail[2];
static int fd = -1, src, chans, rate_hz, quit, active, limit_hit;
static long written, max_data;
static char path[320];

static int put(const int16_t *caller, const int16_t *house, int n)
{
	int16_t out[2048];
	int i, k = 0;

	for (i = 0; i < n; i++) {
		if (chans == 2) {
			out[k++] = caller[i];
			out[k++] = house[i];
		} else {
			out[k++] = wav_mix(caller[i], house[i]);
		}
		if (k + 2 > (int)(sizeof(out) / sizeof(out[0])) || i == n - 1) {
			ssize_t want = k * 2;

			if (written + want > max_data) {
				want = (max_data - written) & ~(chans * 2 - 1);
				limit_hit = 1;
			}
			if (want > 0 && write(fd, out, want) != want)
				return -1;
			written += want;
			if (limit_hit)
				return -1;
			k = 0;
		}
	}
	return 0;
}

static int take(int all, int *more)
{
	int16_t a[1024], b[1024];
	unsigned int na, nb, n, i, lag = rate_hz / 2;

	pthread_mutex_lock(&lock);
	na = head[0] - tail[0];
	nb = head[1] - tail[1];
	n = na < nb ? na : nb;
	if (all || na > n + lag || nb > n + lag)
		n = na > nb ? na : nb;
	if (n > 1024)
		n = 1024;
	for (i = 0; i < n; i++) {
		a[i] = i < na ? side[0][tail[0]++ & SIDE_MASK] : 0;
		b[i] = i < nb ? side[1][tail[1]++ & SIDE_MASK] : 0;
	}
	pthread_mutex_unlock(&lock);
	*more = n == 1024;
	return n ? put(a, b, n) : 0;
}

static void *run_sip(void *arg)
{
	int more;

	(void)arg;
	while (!__atomic_load_n(&quit, __ATOMIC_ACQUIRE)) {
		if (take(0, &more))
			goto out;
		if (!more)
			usleep(100000);
	}
	do {
		if (take(1, &more))
			break;
	} while (more);
out:
	__atomic_store_n(&active, 0, __ATOMIC_RELEASE);
	return NULL;
}

static void *run_line(void *arg)
{
	int dev = open(REC_DEV, O_RDONLY | O_CLOEXEC);

	(void)arg;
	while (dev >= 0 && !__atomic_load_n(&quit, __ATOMIC_ACQUIRE)) {
		uint32_t w[1024];
		int16_t a[1024], b[1024];
		ssize_t r = read(dev, w, sizeof(w));
		int i, n;

		if (r < 0 && (errno == EAGAIN || errno == EINTR))
			continue;
		if (r <= 0) {
			usleep(100000);
			continue;
		}
		n = r / 4;
		for (i = 0; i < n; i++) {
			a[i] = (int16_t)(w[i] & 0xffff);
			b[i] = (int16_t)(w[i] >> 16);
		}
		if (put(a, b, n))
			break;
	}
	if (dev >= 0)
		close(dev);
	__atomic_store_n(&active, 0, __ATOMIC_RELEASE);
	return NULL;
}

int recorder_start(const char *dir, const char *name, int rate, int stereo, int source, long max_bytes)
{
	uint8_t h[WAV_HEADER];

	if (fd >= 0)
		return -1;
	mkdir(dir, 0700);
	snprintf(path, sizeof(path), "%s/%s", dir, name);
	fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
	if (fd < 0)
		return -1;
	chans = stereo ? 2 : 1;
	rate_hz = rate;
	src = source;
	written = 0;
	max_data = max_bytes - WAV_HEADER;
	limit_hit = 0;
	wav_header(h, rate, chans, 0);
	if (max_data <= 0 || write(fd, h, WAV_HEADER) != WAV_HEADER) {
		close(fd);
		unlink(path);
		fd = -1;
		return -1;
	}
	pthread_mutex_lock(&lock);
	head[0] = tail[0] = head[1] = tail[1] = 0;
	pthread_mutex_unlock(&lock);
	quit = 0;
	active = 1;
	if (pthread_create(&thread, NULL, source == REC_SRC_SIP ? run_sip : run_line, NULL)) {
		active = 0;
		close(fd);
		unlink(path);
		fd = -1;
		return -1;
	}
	return 0;
}

void recorder_push(int s, const int16_t *x, int n)
{
	int i;

	if (fd < 0 || src != REC_SRC_SIP || (s != REC_SIDE_CALLER && s != REC_SIDE_HOUSE) ||
	    !__atomic_load_n(&active, __ATOMIC_ACQUIRE))
		return;
	pthread_mutex_lock(&lock);
	for (i = 0; i < n; i++) {
		if (head[s] - tail[s] == SIDE_RING)
			tail[s]++;
		side[s][head[s]++ & SIDE_MASK] = x[i];
	}
	pthread_mutex_unlock(&lock);
}

void recorder_stop(void)
{
	if (fd < 0)
		return;
	__atomic_store_n(&quit, 1, __ATOMIC_RELEASE);
	pthread_join(thread, NULL);
	close(fd);
	fd = -1;
	active = 0;
	if (written <= 0)
		unlink(path);
	else
		wav_fix(path);
}

int recorder_active(void)
{
	return fd >= 0 && __atomic_load_n(&active, __ATOMIC_ACQUIRE);
}

int recorder_hit_limit(void)
{
	return limit_hit;
}

int recorder_fix_all(const char *dir)
{
	DIR *d = opendir(dir);
	struct dirent *e;
	int n = 0;

	if (!d)
		return 0;
	while ((e = readdir(d))) {
		char p[512];
		size_t l = strlen(e->d_name);

		if (l < 5 || strcmp(e->d_name + l - 4, ".wav"))
			continue;
		snprintf(p, sizeof(p), "%s/%s", dir, e->d_name);
		if (wav_fix(p) == 1)
			n++;
	}
	closedir(d);
	return n;
}
