// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 perz3us
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../src/recorder.h"
#include "../src/wav.h"

static const char dir[] = "/tmp/fxs-wav-test";

static uint32_t rd32(const uint8_t *p)
{
	return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}

static long size_of(const char *p)
{
	struct stat st;

	return stat(p, &st) ? -1 : st.st_size;
}

static void wait_idle(void)
{
	int i;

	for (i = 0; i < 50 && recorder_active(); i++)
		usleep(50000);
}

int main(void)
{
	uint8_t h[WAV_HEADER], z[1000] = { 0 };
	int16_t a[160], b[160];
	char p[256];
	FILE *f;
	int i;

	wav_header(h, 8000, 2, 32000);
	assert(!memcmp(h, "RIFF", 4) && rd32(h + 4) == 32036);
	assert(rd32(h + 24) == 8000 && rd32(h + 28) == 32000);
	assert((h[32] | h[33] << 8) == 4 && (h[22] | h[23] << 8) == 2);
	assert(rd32(h + 40) == 32000);
	wav_header(h, 16000, 1, 0);
	assert(rd32(h + 28) == 32000 && (h[32] | h[33] << 8) == 2);

	assert(wav_mix(1000, -3000) == -1000);
	assert(wav_mix(32767, 32767) == 32767);

	assert(!system("rm -rf /tmp/fxs-wav-test && mkdir -p /tmp/fxs-wav-test"));
	snprintf(p, sizeof(p), "%s/cut.wav", dir);
	f = fopen(p, "w");
	wav_header(h, 8000, 2, 0);
	fwrite(h, 1, WAV_HEADER, f);
	fwrite(z, 1, sizeof(z), f);
	fclose(f);
	assert(wav_fix(p) == 1);
	f = fopen(p, "r");
	assert(fread(h, 1, WAV_HEADER, f) == WAV_HEADER);
	fclose(f);
	assert(rd32(h + 4) == 1036 && rd32(h + 40) == 1000);
	assert(wav_fix(p) == 0);
	assert(recorder_fix_all(dir) == 0);

	for (i = 0; i < 160; i++) {
		a[i] = 100;
		b[i] = -100;
	}
	assert(recorder_start(dir, "sip.wav", 8000, 1, REC_SRC_SIP, 1000000) == 0);
	assert(recorder_start(dir, "two.wav", 8000, 1, REC_SRC_SIP, 1000000) == -1);
	for (i = 0; i < 10; i++) {
		recorder_push(REC_SIDE_CALLER, a, 160);
		recorder_push(REC_SIDE_HOUSE, b, 160);
	}
	usleep(300000);
	recorder_stop();
	snprintf(p, sizeof(p), "%s/sip.wav", dir);
	assert(size_of(p) == WAV_HEADER + 1600 * 4);
	f = fopen(p, "r");
	assert(fread(h, 1, WAV_HEADER, f) == WAV_HEADER);
	assert(rd32(h + 40) == 6400);
	{
		int16_t s[2];

		assert(fread(s, 2, 2, f) == 2 && s[0] == 100 && s[1] == -100);
	}
	fclose(f);

	assert(recorder_start(dir, "lag.wav", 8000, 0, REC_SRC_SIP, 1000000) == 0);
	for (i = 0; i < 40; i++)
		recorder_push(REC_SIDE_CALLER, a, 160);
	usleep(300000);
	recorder_stop();
	snprintf(p, sizeof(p), "%s/lag.wav", dir);
	assert(size_of(p) == WAV_HEADER + 6400 * 2);

	assert(recorder_start(dir, "full.wav", 8000, 1, REC_SRC_SIP, WAV_HEADER + 4000) == 0);
	for (i = 0; i < 20; i++) {
		recorder_push(REC_SIDE_CALLER, a, 160);
		recorder_push(REC_SIDE_HOUSE, b, 160);
	}
	wait_idle();
	assert(!recorder_active() && recorder_hit_limit());
	recorder_stop();
	snprintf(p, sizeof(p), "%s/full.wav", dir);
	assert(size_of(p) == WAV_HEADER + 4000);

	{
		struct rlimit rl, old;

		getrlimit(RLIMIT_FSIZE, &old);
		rl = old;
		rl.rlim_cur = 20000;
		signal(SIGXFSZ, SIG_IGN);
		assert(!setrlimit(RLIMIT_FSIZE, &rl));
		assert(recorder_start(dir, "gone.wav", 8000, 1, REC_SRC_SIP, 1000000) == 0);
		for (i = 0; i < 60; i++) {
			recorder_push(REC_SIDE_CALLER, a, 160);
			recorder_push(REC_SIDE_HOUSE, b, 160);
		}
		wait_idle();
		assert(!recorder_active() && !recorder_hit_limit());
		recorder_stop();
		setrlimit(RLIMIT_FSIZE, &old);
	}

	assert(recorder_start("/tmp/fxs-wav-test/no/such", "x.wav", 8000, 1, REC_SRC_SIP, 1000000) == -1);

	system("rm -rf /tmp/fxs-wav-test");
	puts("test_wav ok");
	return 0;
}
