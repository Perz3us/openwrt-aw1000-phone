/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2026 perz3us */
#ifndef ALARM_H
#define ALARM_H

struct alarm {
	unsigned char days;
	short minute;
	short seconds;
	long last_day;
};

int alarm_due(struct alarm *a, int n, int wday, int minute, long day);

#endif
