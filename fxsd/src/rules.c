// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 perz3us
#include <stdio.h>
#include <string.h>
#include "rules.h"

static void plain(const char *in, char *out, size_t n)
{
	snprintf(out, n, "%s%s", *in == '+' ? "00" : "", *in == '+' ? in + 1 : in);
}

static int listed(const char *number, const char list[][32], int n)
{
	char num[40], pre[40];
	int i;

	plain(number, num, sizeof(num));
	for (i = 0; i < n; i++) {
		plain(list[i], pre, sizeof(pre));
		if (*pre && !strncmp(num, pre, strlen(pre)))
			return 1;
	}
	return 0;
}

int rules_barred(const char *number, int mode, const char list[][32], int n)
{
	if (mode == RULES_BLOCK)
		return listed(number, list, n);
	if (mode == RULES_ALLOW)
		return !listed(number, list, n);
	return 0;
}
