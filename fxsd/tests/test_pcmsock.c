// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 perz3us
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>
#include "../src/pcmsock.h"

static void test_helpers(void)
{
	int16_t in[320], out[640], ref[640];
	struct pcm_down d = { 0 }, dr = { 0 };
	struct pcm_up u = { 0 }, ur = { 0 };
	int i, n;

	for (i = 0; i < 320; i++)
		in[i] = (int16_t)(i * 7 - 1000);

	assert(pcm_frame_samples(0, 0) == 160);
	assert(pcm_frame_samples(0, 1) == 160);
	assert(pcm_frame_samples(1, 0) == 160);
	assert(pcm_frame_samples(1, 1) == 320);

	assert(pcm_from_modem(&d, 1, 1, in, 320, out) == 320 && !memcmp(in, out, 640));
	assert(pcm_from_modem(&d, 0, 1, in, 160, out) == 160 && !memcmp(in, out, 320));
	assert(pcm_from_modem(&d, 0, 0, in, 160, out) == 160 && !memcmp(in, out, 320));
	n = pcm_from_modem(&d, 1, 0, in, 320, out);
	assert(n == 160 && pcm_downsample(&dr, in, 320, ref) == 160 && !memcmp(out, ref, 320));

	assert(pcm_to_modem(&u, 1, 1, in, 320, out) == 320 && !memcmp(in, out, 640));
	assert(pcm_to_modem(&u, 0, 1, in, 160, out) == 160 && !memcmp(in, out, 320));
	assert(pcm_to_modem(&u, 0, 0, in, 160, out) == 160 && !memcmp(in, out, 320));
	n = pcm_to_modem(&u, 1, 0, in, 160, out);
	assert(n == 320 && pcm_upsample(&ur, in, 160, ref) == 320 && !memcmp(out, ref, 640));
}

static void test_socket(const char *dir, int wb, int native, int want)
{
	char sock_path[108], dev_path[108];
	struct sockaddr_un a = { .sun_family = AF_UNIX };
	struct timeval tv = { .tv_sec = 2 };
	uint32_t pairs[640];
	int16_t got[400];
	int dev, c, i;
	ssize_t r;

	snprintf(sock_path, sizeof(sock_path), "%s/pcm.sock", dir);
	snprintf(dev_path, sizeof(dev_path), "%s/dev", dir);
	unlink(dev_path);
	assert(!mkfifo(dev_path, 0600));
	assert(!pcmsock_start(sock_path, dev_path, wb, native));
	dev = open(dev_path, O_WRONLY);
	assert(dev >= 0);

	c = socket(AF_UNIX, SOCK_SEQPACKET, 0);
	assert(c >= 0);
	snprintf(a.sun_path, sizeof(a.sun_path), "%s", sock_path);
	assert(!connect(c, (struct sockaddr *)&a, sizeof(a)));
	assert(!setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)));
	usleep(300000);
	pcmsock_live(1);
	usleep(100000);

	for (i = 0; i < 640; i++)
		pairs[i] = (uint16_t)(i + 1);
	assert(write(dev, pairs, wb ? sizeof(pairs) : 320 * 4) > 0);

	r = recv(c, got, sizeof(got), 0);
	assert(r == want * 2);
	if (native && wb)
		for (i = 0; i < want; i++)
			assert(got[i] == i + 1);
	r = recv(c, got, sizeof(got), 0);
	assert(r == want * 2);

	pcmsock_live(0);
	close(c);
	close(dev);
	pcmsock_stop();
}

static void test_oversize(const char *dir)
{
	char sock_path[108], dev_path[108];
	struct sockaddr_un a = { .sun_family = AF_UNIX };
	int16_t big[321] = { 0 };
	uint32_t before[4], after[4];
	int dev, c;

	snprintf(sock_path, sizeof(sock_path), "%s/pcm.sock", dir);
	snprintf(dev_path, sizeof(dev_path), "%s/dev", dir);
	assert(!pcmsock_start(sock_path, dev_path, 0, 0));
	dev = open(dev_path, O_RDONLY | O_NONBLOCK);
	assert(dev >= 0);
	c = socket(AF_UNIX, SOCK_SEQPACKET, 0);
	assert(c >= 0);
	snprintf(a.sun_path, sizeof(a.sun_path), "%s", sock_path);
	assert(!connect(c, (struct sockaddr *)&a, sizeof(a)));
	usleep(300000);
	pcmsock_live(1);
	usleep(300000);
	pcmsock_stats(before);

	assert(send(c, big, 161 * 2, 0) == 161 * 2);
	usleep(300000);
	pcmsock_stats(after);
	assert(after[1] == before[1] && after[3] == before[3] + 1);

	assert(send(c, big, 160 * 2, 0) == 160 * 2);
	usleep(300000);
	pcmsock_stats(after);
	assert(after[1] == before[1] + 1);

	pcmsock_live(0);
	close(c);
	close(dev);
	pcmsock_stop();
}

int main(void)
{
	char dir[] = "/tmp/pcmsockXXXXXX";

	assert(mkdtemp(dir));
	test_helpers();
	test_socket(dir, 1, 1, 320);
	test_socket(dir, 1, 0, 160);
	test_socket(dir, 0, 1, 160);
	test_socket(dir, 0, 0, 160);
	test_oversize(dir);
	return 0;
}
