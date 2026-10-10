// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 perz3us
#include <stdlib.h>
#include <string.h>
#include "clcc.h"

static const char *field(const char *p, long *v)
{
	char *end;

	while (*p == ' ')
		p++;
	*v = strtol(p, &end, 10);
	if (end == p)
		return NULL;
	return *end == ',' ? end + 1 : end;
}

static void number(const char *p, char *out)
{
	size_t n = 0;

	while (*p == '\\' || *p == '"' || *p == ' ')
		p++;
	while (n + 1 < CLCC_NUM_MAX && (strchr("0123456789+*#", *p) && *p))
		out[n++] = *p++;
	out[n] = 0;
}

int clcc_parse_voice(const char *resp, struct clcc_call *call)
{
	const char *p = resp;

	while ((p = strstr(p, "+CLCC:"))) {
		long v[5];
		const char *q = p + 6;
		int i;

		p += 6;
		for (i = 0; i < 5 && q; i++)
			q = field(q, &v[i]);
		if (!q || v[3] != 0)
			continue;
		call->id = v[0];
		call->dir = v[1];
		call->stat = v[2];
		call->number[0] = 0;
		if (q[-1] == ',')
			number(q, call->number);
		return 1;
	}
	return 0;
}
