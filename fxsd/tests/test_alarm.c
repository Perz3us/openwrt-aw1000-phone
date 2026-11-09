// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 perz3us
#include <assert.h>
#include <stdio.h>
#include "../src/alarm.h"

int main(void)
{
	struct alarm a[2] = {
		{ 1 << 1, 7 * 60, 60, -1 },
		{ 1 << 2, 8 * 60, 30, -1 },
	};

	assert(alarm_due(a, 2, 1, 7 * 60, 100) == 0);
	assert(alarm_due(a, 2, 1, 7 * 60, 100) == -1);
	assert(alarm_due(a, 2, 1, 7 * 60, 107) == 0);
	assert(alarm_due(a, 2, 2, 7 * 60, 108) == -1);
	assert(alarm_due(a, 2, 1, 7 * 60 + 1, 114) == -1);
	assert(alarm_due(a, 2, 2, 8 * 60, 108) == 1);
	assert(alarm_due(a, 0, 1, 7 * 60, 200) == -1);

	printf("alarm ok\n");
	return 0;
}
