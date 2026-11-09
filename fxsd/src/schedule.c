// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 perz3us
#include <stdio.h>
#include <string.h>
#include "schedule.h"

int sched_parse_days(const char *s)
{
	static const char *const names[] = { "sun", "mon", "tue", "wed", "thu", "fri", "sat" };
	int mask = 0, i;

	while (*s) {
		for (i = 0; i < 7; i++)
			if (!strncmp(s, names[i], 3) && (s[3] == ',' || !s[3]))
				break;
		if (i == 7)
			return -1;
		mask |= 1 << i;
		s += 3;
		if (*s == ',')
			s++;
	}
	return mask ? mask : -1;
}

int sched_parse_mode(const char *s)
{
	if (!strcmp(s, "all"))
		return SCHED_ALL;
	if (!strcmp(s, "allowed"))
		return SCHED_ALLOWED;
	if (!strcmp(s, "repeat"))
		return SCHED_REPEAT;
	return -1;
}

int sched_parse_action(const char *s)
{
	if (!strcmp(s, "once"))
		return ACT_ONCE;
	if (!strcmp(s, "busy"))
		return ACT_BUSY;
	return ACT_SILENT;
}

int sched_parse_date(const char *s)
{
	static const int mdays[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
	int y, m, d, days, i;
	char end;

	if (sscanf(s, "%4d-%2d-%2d%c", &y, &m, &d, &end) != 3 || y < 1970 || y > 2200 || m < 1 || m > 12 || d < 1 ||
	    d > mdays[m - 1] + (m == 2 && ((y % 4 == 0 && y % 100) || y % 400 == 0)))
		return -1;
	days = (y - 1970) * 365 + (y - 1969) / 4 - (y - 1901) / 100 + (y - 1601) / 400;
	for (i = 1; i < m; i++)
		days += mdays[i - 1] + (i == 2 && ((y % 4 == 0 && y % 100) || y % 400 == 0));
	return days + d - 1;
}

static int win_active(const struct sched_win *w, int wday, int minute)
{
	int today = w->days & (1 << wday);
	int yesterday = w->days & (1 << ((wday + 6) % 7));

	if (w->from == w->to)
		return today;
	if (w->from < w->to)
		return today && minute >= w->from && minute < w->to;
	return (today && minute >= w->from) || (yesterday && minute < w->to);
}

int sched_active(const struct sched_win *w, int n, int wday, int minute)
{
	int mode = -1, i;

	for (i = 0; i < n; i++)
		if (!w[i].first_day && win_active(&w[i], wday, minute) && (mode < 0 || w[i].mode < mode))
			mode = w[i].mode;
	return mode;
}

static int in_range(const struct sched_win *w, int day)
{
	return day >= w->first_day && day <= w->last_day;
}

static int holiday_active(const struct sched_win *w, int day, int minute)
{
	if (w->from == w->to)
		return in_range(w, day);
	if (w->from < w->to)
		return in_range(w, day) && minute >= w->from && minute < w->to;
	return (in_range(w, day) && minute >= w->from) || (in_range(w, day - 1) && minute < w->to);
}

static int gentler(int a, int b)
{
	static const int rank[] = { [ACT_ONCE] = 0, [ACT_SILENT] = 1, [ACT_BUSY] = 2 };

	return rank[a] <= rank[b] ? a : b;
}

void dnd_decide(const struct sched_win *w, int n, const struct dnd_in *in, struct dnd_out *out)
{
	int mode = -1, action = ACT_BUSY, holiday = 0, i;
	const struct sched_win *by = NULL;

	memset(out, 0, sizeof(*out));
	out->ring = 1;
	if (in->priority == DND_NEVER) {
		out->ring = 0;
		out->action = ACT_SILENT;
		snprintf(out->reason, sizeof(out->reason), "never");
		return;
	}
	if (in->clock_ok)
		for (i = 0; i < n; i++)
			if (w[i].first_day && in_range(&w[i], in->day))
				holiday = 1;
	for (i = 0; in->clock_ok && i < n; i++) {
		const struct sched_win *x = &w[i];
		int on = x->first_day ? holiday_active(x, in->day, in->minute) :
			 !holiday && win_active(x, in->wday, in->minute);

		if (!on)
			continue;
		action = gentler(action, x->action);
		if (mode < 0 || x->mode < mode) {
			mode = x->mode;
			by = x;
		}
	}
	if (in->quick && (!in->quick_until || (in->clock_ok && in->now < in->quick_until))) {
		action = mode < 0 ? ACT_SILENT : gentler(action, ACT_SILENT);
		if (mode < 0 || SCHED_ALLOWED < mode)
			mode = SCHED_ALLOWED;
		by = NULL;
	}
	if (mode < 0)
		return;
	out->quiet = 1;
	if (in->priority == DND_IMPORTANT) {
		snprintf(out->reason, sizeof(out->reason), "important");
		return;
	}
	if (mode != SCHED_ALL && in->allowed) {
		snprintf(out->reason, sizeof(out->reason), "allowed");
		return;
	}
	if (mode == SCHED_REPEAT && in->repeat) {
		snprintf(out->reason, sizeof(out->reason), "repeat");
		return;
	}
	out->ring = 0;
	out->action = action == ACT_BUSY && in->sip_on ? ACT_SILENT : action;
	if (by)
		snprintf(out->reason, sizeof(out->reason), "%s:%s", by->first_day ? "holiday" : "quiet", by->name);
	else
		snprintf(out->reason, sizeof(out->reason), "quick");
}
