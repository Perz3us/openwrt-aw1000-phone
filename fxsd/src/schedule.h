/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2026 perz3us */
#ifndef SCHEDULE_H
#define SCHEDULE_H

enum sched_mode { SCHED_ALL, SCHED_ALLOWED, SCHED_REPEAT };
enum sched_action { ACT_SILENT, ACT_ONCE, ACT_BUSY };
enum dnd_priority { DND_NORMAL, DND_IMPORTANT, DND_NEVER };

struct sched_win {
	unsigned char days;
	short from, to;
	unsigned char mode;
	unsigned char action;
	int first_day, last_day;
	char name[24];
};

struct dnd_in {
	int wday, minute, day, clock_ok;
	int quick;
	long quick_until, now;
	int sip_on, priority, allowed, repeat;
};

struct dnd_out {
	int ring, quiet, action;
	char reason[40];
};

int sched_parse_days(const char *s);
int sched_parse_mode(const char *s);
int sched_parse_action(const char *s);
int sched_parse_date(const char *s);
int sched_active(const struct sched_win *w, int n, int wday, int minute);
void dnd_decide(const struct sched_win *w, int n, const struct dnd_in *in, struct dnd_out *out);

#endif
