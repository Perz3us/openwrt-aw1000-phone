// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 perz3us
#include <errno.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define SAVE_FRAMES 1000

static long now_s(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec;
}

int main(int argc, char **argv)
{
	struct sockaddr_un a = { .sun_family = AF_UNIX };
	const char *path = "/var/run/fxsd/pcm.sock", *out = NULL;
	struct pollfd p = { .events = POLLIN };
	int16_t buf[1024];
	FILE *f = NULL;
	int echo = 1, saved = 0, frames = 0, echoed = 0, low = 0, high = 0, peak = 0, was = 0, o;
	long second;

	while ((o = getopt(argc, argv, "s:o:n")) != -1) {
		switch (o) {
		case 's': path = optarg; break;
		case 'o': out = optarg; break;
		case 'n': echo = 0; break;
		default:
			fprintf(stderr, "usage: %s [-s SOCKET] [-o FILE] [-n]\n", argv[0]);
			return 2;
		}
	}
	if (out && !(f = fopen(out, "wb"))) {
		perror(out);
		return 1;
	}
	if (strlen(path) >= sizeof(a.sun_path)) {
		fprintf(stderr, "socket path is too long\n");
		return 2;
	}
	strcpy(a.sun_path, path);
	p.fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
	if (p.fd < 0 || connect(p.fd, (struct sockaddr *)&a, sizeof(a))) {
		perror(path);
		return 1;
	}
	second = now_s();
	for (;;) {
		int r = poll(&p, 1, 1000);

		if (r < 0 && errno != EINTR) {
			perror("poll");
			return 1;
		}
		if (r > 0) {
			ssize_t n = recv(p.fd, buf, sizeof(buf), 0);
			int i, samples = n / 2;

			if (n == 0) {
				printf("socket closed\n");
				break;
			}
			if (n < 0) {
				if (errno == EINTR)
					continue;
				perror("recv");
				return 1;
			}
			frames++;
			if (!low || samples < low)
				low = samples;
			if (samples > high)
				high = samples;
			for (i = 0; i < samples; i++)
				if (abs(buf[i]) > peak)
					peak = abs(buf[i]);
			if (echo && send(p.fd, buf, n, MSG_DONTWAIT | MSG_NOSIGNAL) == n)
				echoed++;
			if (f && saved < SAVE_FRAMES) {
				fwrite(buf, 2, samples, f);
				saved++;
			}
		}
		if (now_s() != second) {
			second = now_s();
			if (frames || was) {
				printf("frames %d samples %d-%d peak %d echoed %d saved %d\n",
				       frames, low, high, peak, echoed, saved);
				fflush(stdout);
				if (f)
					fflush(f);
			}
			was = frames;
			frames = echoed = low = high = peak = 0;
		}
	}
	if (f)
		fclose(f);
	close(p.fd);
	return 0;
}
