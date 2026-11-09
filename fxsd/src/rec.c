// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 perz3us
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "contacts.h"
#include "rec.h"

int rec_parse_mode(const char *s)
{
	if (!strcmp(s, "all"))
		return REC_ALL;
	if (!strcmp(s, "chosen"))
		return REC_CHOSEN;
	if (!strcmp(s, "every"))
		return REC_EVERY;
	return REC_OFF;
}

int rec_wanted(int mode, const struct rec_rule *r, int n, const char *number)
{
	int i;

	if (mode == REC_OFF)
		return 0;
	if (mode == REC_EVERY)
		return 1;
	for (i = n - 1; *number && i >= 0; i--)
		if (same_number(number, r[i].number))
			return r[i].always;
	return mode == REC_ALL;
}

struct rec_file {
	char name[64];
	time_t t;
	long size;
	int keep;
};

static int is_wav(const char *n)
{
	size_t l = strlen(n);

	return l > 4 && l < 64 && !strcmp(n + l - 4, ".wav");
}

static int scan(const char *dir, struct rec_file **out)
{
	DIR *d = opendir(dir);
	struct dirent *e;
	struct rec_file *v = NULL;
	int n = 0, cap = 0;

	*out = NULL;
	if (!d)
		return 0;
	while ((e = readdir(d))) {
		char p[512];
		struct stat st;

		if (!is_wav(e->d_name))
			continue;
		snprintf(p, sizeof(p), "%s/%s", dir, e->d_name);
		if (stat(p, &st) || !S_ISREG(st.st_mode))
			continue;
		if (n == cap) {
			struct rec_file *nv = realloc(v, (cap = cap ? cap * 2 : 64) * sizeof(*v));

			if (!nv)
				break;
			v = nv;
		}
		snprintf(v[n].name, sizeof(v[n].name), "%s", e->d_name);
		v[n].t = st.st_mtime;
		v[n].size = st.st_size;
		snprintf(p, sizeof(p), "%s/%s.keep", dir, e->d_name);
		v[n].keep = !access(p, F_OK);
		n++;
	}
	closedir(d);
	*out = v;
	return n;
}

static int older(const void *a, const void *b)
{
	const struct rec_file *x = a, *y = b;

	if (x->t != y->t)
		return x->t < y->t ? -1 : 1;
	return strcmp(x->name, y->name);
}

long rec_dir_bytes(const char *dir)
{
	struct rec_file *v;
	int i, n = scan(dir, &v);
	long sum = 0;

	for (i = 0; i < n; i++)
		sum += v[i].size;
	free(v);
	return sum;
}

int rec_prune(const char *dir, long limit_bytes, long need_bytes, char removed[][64], int max)
{
	struct rec_file *v;
	int i, n = scan(dir, &v), gone = 0;
	long used = 0;

	for (i = 0; i < n; i++)
		used += v[i].size;
	qsort(v, n, sizeof(*v), older);
	for (i = 0; i < n && used + need_bytes > limit_bytes; i++) {
		char p[512];

		if (v[i].keep)
			continue;
		snprintf(p, sizeof(p), "%s/%s", dir, v[i].name);
		if (unlink(p))
			continue;
		used -= v[i].size;
		if (gone < max)
			snprintf(removed[gone], 64, "%s", v[i].name);
		gone++;
	}
	free(v);
	return used + need_bytes > limit_bytes ? -1 : gone;
}

int rec_pick_dir(const char *mounts, char *out, size_t n)
{
	const char *l = mounts;

	while (l && *l) {
		char dev[128], mnt[256];

		if (sscanf(l, "%127s %255s", dev, mnt) == 2 && !strncmp(dev, "/dev/sd", 7) && !strncmp(mnt, "/mnt/", 5)) {
			snprintf(out, n, "%s/" REC_USB_SUB, mnt);
			return 1;
		}
		l = strchr(l, '\n');
		if (l)
			l++;
	}
	snprintf(out, n, "%s", REC_FLASH_DIR);
	return 0;
}
