// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 perz3us
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>
#include "../src/rec.h"

static const char dir[] = "/tmp/fxs-rec-test";

static void put(const char *name, long size, time_t age)
{
	char p[256];
	struct utimbuf t = { age, age };
	FILE *f;

	snprintf(p, sizeof(p), "%s/%s", dir, name);
	f = fopen(p, "w");
	assert(f);
	while (size-- > 0)
		fputc(0, f);
	fclose(f);
	utime(p, &t);
}

static int there(const char *name)
{
	char p[256];

	snprintf(p, sizeof(p), "%s/%s", dir, name);
	return !access(p, F_OK);
}

static void fresh(void)
{
	char cmd[128];

	snprintf(cmd, sizeof(cmd), "rm -rf %s && mkdir -p %s", dir, dir);
	assert(!system(cmd));
	put("a.wav", 1000, 1000);
	put("b.wav", 1000, 2000);
	put("c.wav", 1000, 3000);
	put("d.wav", 1000, 4000);
}

int main(void)
{
	struct rec_rule r[2];
	char removed[8][64], out[128];

	assert(rec_parse_mode("all") == REC_ALL);
	assert(rec_parse_mode("chosen") == REC_CHOSEN);
	assert(rec_parse_mode("off") == REC_OFF);
	assert(rec_parse_mode("junk") == REC_OFF);
	assert(rec_parse_mode("every") == REC_EVERY);

	snprintf(r[0].number, sizeof(r[0].number), "0771234567");
	r[0].always = 0;
	assert(rec_wanted(REC_ALL, r, 1, "+94771234567") == 0);
	assert(rec_wanted(REC_ALL, r, 1, "0712345678") == 1);
	assert(rec_wanted(REC_EVERY, r, 1, "+94771234567") == 1);
	assert(rec_wanted(REC_EVERY, r, 0, "") == 1);
	assert(rec_wanted(REC_CHOSEN, r, 0, "0771234567") == 0);
	assert(rec_wanted(REC_OFF, r, 0, "0771234567") == 0);

	snprintf(r[0].number, sizeof(r[0].number), "077123");
	r[0].always = 1;
	snprintf(r[1].number, sizeof(r[1].number), "077123");
	r[1].always = 0;
	assert(rec_wanted(REC_CHOSEN, r, 2, "077123") == 0);
	r[0].always = 0;
	r[1].always = 1;
	assert(rec_wanted(REC_CHOSEN, r, 2, "077123") == 1);
	assert(rec_wanted(REC_ALL, r, 0, "") == 1);
	assert(rec_wanted(REC_CHOSEN, r, 2, "") == 0);

	fresh();
	assert(rec_dir_bytes(dir) == 4000);
	assert(rec_prune(dir, 3000, 1000, removed, 8) == 2);
	assert(!there("a.wav") && !there("b.wav") && there("c.wav") && there("d.wav"));
	assert(!strcmp(removed[0], "a.wav") && !strcmp(removed[1], "b.wav"));

	fresh();
	put("a.wav.keep", 0, 1000);
	put("b.wav.keep", 0, 1000);
	assert(rec_prune(dir, 3000, 1000, removed, 8) == 2);
	assert(there("a.wav") && there("b.wav") && !there("c.wav") && !there("d.wav"));

	fresh();
	put("a.wav.keep", 0, 1000);
	put("b.wav.keep", 0, 1000);
	put("c.wav.keep", 0, 1000);
	put("d.wav.keep", 0, 1000);
	assert(rec_prune(dir, 3000, 1000, removed, 8) == -1);
	assert(there("a.wav") && there("d.wav"));

	fresh();
	assert(rec_prune(dir, 10000, 1000, removed, 8) == 0);

	assert(rec_pick_dir("/dev/ubi0_1 /overlay ubifs rw 0 0\n/dev/sda1 /mnt/sda1 vfat rw 0 0\n", out, sizeof(out)) == 1);
	assert(!strcmp(out, "/mnt/sda1/phone-recordings"));
	assert(rec_pick_dir("/dev/ubi0_1 /overlay ubifs rw 0 0\ntmpfs /tmp tmpfs rw 0 0\n", out, sizeof(out)) == 0);
	assert(rec_pick_dir("/dev/sdb1 /srv/disk ext4 rw 0 0\n", out, sizeof(out)) == 0);
	assert(!strcmp(out, REC_FLASH_DIR));

	assert(!system("rm -rf /tmp/fxs-rec-test"));
	puts("test_rec ok");
	return 0;
}
