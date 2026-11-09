// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 perz3us
#include "alarm.h"

int alarm_due(struct alarm *a, int n, int wday, int minute, long day)
{
	int i;

	for (i = 0; i < n; i++) {
		if (!(a[i].days & (1 << wday)) || a[i].minute != minute || a[i].last_day == day)
			continue;
		a[i].last_day = day;
		return i;
	}
	return -1;
}
