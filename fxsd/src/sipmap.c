// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 perz3us
#include <stdlib.h>
#include <string.h>
#include "sipmap.h"

static const struct {
	const char *word;
	const char *ours[8];
} kinds[SIPMAP_KINDS] = {
	[SIPMAP_METHOD] = { "method", { "status", "dial", "answer", "reject", "hangup", "digit" } },
	[SIPMAP_ARG] = { "arg", { "line", "number", "digit" } },
	[SIPMAP_FIELD] = { "field", { "state", "origin", "answered_by", "number", "enabled" } },
	[SIPMAP_STATE] = { "state", { "off", "idle", "dialing", "alerting", "ringing", "active", "ending" } },
	[SIPMAP_OWNER] = { "owner", { "phone", "none", "network" } },
};

void sipmap_defaults(struct sipmap *m)
{
	memset(m, 0, sizeof(*m));
	strcpy(m->object, "fxs_sip");
	strcpy(m->event, "fxs_sip.call");
	m->audio = 8000;
}

static int kind_of(const char *word)
{
	int k;

	for (k = 0; k < SIPMAP_KINDS; k++)
		if (!strcmp(word, kinds[k].word))
			return k;
	return -1;
}

static int known(int k, const char *ours)
{
	int i;

	for (i = 0; i < 8 && kinds[k].ours[i]; i++)
		if (!strcmp(ours, kinds[k].ours[i]))
			return 1;
	return 0;
}

static int add_pair(struct sipmap *m, int k, const char *ours, const char *theirs)
{
	int i;

	if (!known(k, ours) || m->n >= SIPMAP_PAIRS)
		return -1;
	for (i = 0; i < m->n; i++)
		if ((int)m->pair[i].kind == k &&
		    (!strcmp(m->pair[i].ours, ours) || !strcmp(m->pair[i].theirs, theirs)))
			return -1;
	m->pair[m->n].kind = k;
	strcpy(m->pair[m->n].ours, ours);
	strcpy(m->pair[m->n].theirs, theirs);
	m->n++;
	return 0;
}

static int take_line(struct sipmap *m, const char *line)
{
	char a[64], b[64], c[64], d[2];
	int n = sscanf(line, "%63s %63s %63s %1s", a, b, c, d), k;

	if (n <= 0 || a[0] == '#')
		return 0;
	if (strlen(a) >= SIPMAP_NAME_MAX || (n > 1 && strlen(b) >= SIPMAP_NAME_MAX) ||
	    (n > 2 && strlen(c) >= SIPMAP_NAME_MAX))
		return -1;
	if (n == 2) {
		if (!strcmp(a, "object")) {
			strcpy(m->object, b);
		} else if (!strcmp(a, "event")) {
			strcpy(m->event, b);
		} else if (!strcmp(a, "audio")) {
			m->audio = atoi(b);
			if (m->audio != 8000 && m->audio != 16000)
				return -1;
		} else {
			return -1;
		}
		return 0;
	}
	if (n != 3 || (k = kind_of(a)) < 0)
		return -1;
	return add_pair(m, k, b, c);
}

int sipmap_load(struct sipmap *m, FILE *f)
{
	char line[256];

	sipmap_defaults(m);
	while (fgets(line, sizeof(line), f))
		if (take_line(m, line))
			return -1;
	return 0;
}

const char *sipmap_out(const struct sipmap *m, enum sipmap_kind k, const char *ours)
{
	int i;

	for (i = 0; i < m->n; i++)
		if (m->pair[i].kind == k && !strcmp(m->pair[i].ours, ours))
			return m->pair[i].theirs;
	return ours;
}

const char *sipmap_in(const struct sipmap *m, enum sipmap_kind k, const char *theirs)
{
	int i;

	for (i = 0; i < m->n; i++)
		if (m->pair[i].kind == k && !strcmp(m->pair[i].theirs, theirs))
			return m->pair[i].ours;
	return theirs;
}
