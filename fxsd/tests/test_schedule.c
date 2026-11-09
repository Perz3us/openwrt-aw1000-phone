// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 perz3us
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/schedule.h"

#define SUN 0
#define MON 1
#define TUE 2
#define HM(h, m) ((h) * 60 + (m))

int main(void)
{
	struct sched_win night = { 1 << MON, HM(22, 0), HM(7, 0), SCHED_REPEAT, ACT_SILENT, 0, 0, "" };
	struct sched_win allday = { 1 << MON, 0, 0, SCHED_ALLOWED, ACT_SILENT, 0, 0, "" };
	struct sched_win two[2] = {
		{ 127, HM(10, 0), HM(12, 0), SCHED_REPEAT, ACT_SILENT, 0, 0, "" },
		{ 127, HM(11, 0), HM(13, 0), SCHED_ALL, ACT_SILENT, 0, 0, "" },
	};
	struct sched_win sun = { 1 << SUN, HM(23, 0), HM(1, 0), SCHED_ALL, ACT_SILENT, 0, 0, "" };

	assert(sched_active(&night, 1, MON, HM(23, 0)) == SCHED_REPEAT);
	assert(sched_active(&night, 1, TUE, HM(6, 59)) == SCHED_REPEAT);
	assert(sched_active(&night, 1, TUE, HM(7, 0)) == -1);
	assert(sched_active(&night, 1, MON, HM(6, 0)) == -1);
	assert(sched_active(&night, 1, TUE, HM(23, 0)) == -1);
	assert(sched_active(&sun, 1, MON, HM(0, 30)) == SCHED_ALL);

	assert(sched_active(&allday, 1, MON, HM(15, 0)) == SCHED_ALLOWED);
	assert(sched_active(&allday, 1, TUE, HM(15, 0)) == -1);

	assert(sched_active(two, 2, MON, HM(11, 30)) == SCHED_ALL);
	assert(sched_active(two, 2, MON, HM(10, 30)) == SCHED_REPEAT);
	assert(sched_active(two, 0, MON, HM(10, 30)) == -1);

	assert(sched_parse_days("mon,tue") == ((1 << MON) | (1 << TUE)));
	assert(sched_parse_days("sun,mon,tue,wed,thu,fri,sat") == 127);
	assert(sched_parse_days("") == -1);
	assert(sched_parse_days("mon,xyz") == -1);
	assert(sched_parse_days("monday") == -1);

	assert(sched_parse_mode("all") == SCHED_ALL);
	assert(sched_parse_mode("repeat") == SCHED_REPEAT);
	assert(sched_parse_mode("x") == -1);

	{
		struct sched_win w[3] = {
			{ 127, HM(22, 0), HM(7, 0), SCHED_ALLOWED, ACT_SILENT, 0, 0, "Night" },
			{ 127, HM(23, 0), HM(6, 0), SCHED_ALL, ACT_BUSY, 0, 0, "Late" },
			{ 0, HM(22, 0), HM(7, 0), SCHED_ALL, ACT_ONCE, 20000, 20002, "New Year" },
		};
		struct dnd_in in = { .wday = MON, .minute = HM(23, 30), .day = 19000, .clock_ok = 1, .now = 1000 };
		struct dnd_out out;

		dnd_decide(w, 2, &in, &out);
		assert(!out.ring && out.quiet && out.action == ACT_SILENT && !strcmp(out.reason, "quiet:Late"));
		in.sip_on = 1;
		w[0].action = ACT_BUSY;
		dnd_decide(w, 2, &in, &out);
		assert(!out.ring && out.action == ACT_SILENT);
		in.sip_on = 0;
		dnd_decide(w, 2, &in, &out);
		assert(!out.ring && out.action == ACT_BUSY);
		w[0].action = ACT_ONCE;
		dnd_decide(w, 2, &in, &out);
		assert(out.action == ACT_ONCE);
		w[0].action = ACT_SILENT;

		in.priority = DND_IMPORTANT;
		dnd_decide(w, 2, &in, &out);
		assert(out.ring && !strcmp(out.reason, "important"));
		in.priority = DND_NEVER;
		in.minute = HM(12, 0);
		dnd_decide(w, 2, &in, &out);
		assert(!out.ring && !out.quiet && !strcmp(out.reason, "never"));
		in.priority = DND_NORMAL;
		dnd_decide(w, 2, &in, &out);
		assert(out.ring && !out.quiet && !*out.reason);

		in.minute = HM(22, 30);
		in.allowed = 1;
		dnd_decide(w, 2, &in, &out);
		assert(out.ring && !strcmp(out.reason, "allowed"));
		in.allowed = 0;
		in.repeat = 1;
		dnd_decide(w, 2, &in, &out);
		assert(!out.ring && !strcmp(out.reason, "quiet:Night"));
		w[0].mode = SCHED_REPEAT;
		dnd_decide(w, 2, &in, &out);
		assert(out.ring && !strcmp(out.reason, "repeat"));
		w[0].mode = SCHED_ALLOWED;
		in.repeat = 0;

		in.day = 20001;
		in.minute = HM(12, 0);
		dnd_decide(w, 3, &in, &out);
		assert(out.ring && !out.quiet);
		in.minute = HM(23, 30);
		dnd_decide(w, 3, &in, &out);
		assert(!out.ring && out.action == ACT_ONCE && !strcmp(out.reason, "holiday:New Year"));
		in.day = 20003;
		in.minute = HM(5, 0);
		dnd_decide(w, 3, &in, &out);
		assert(!out.ring && out.quiet);
		in.minute = HM(6, 0);
		dnd_decide(&w[2], 1, &in, &out);
		assert(!out.ring && !strcmp(out.reason, "holiday:New Year"));
		in.day = 20004;
		dnd_decide(&w[2], 1, &in, &out);
		assert(out.ring);
		w[2].from = w[2].to = 0;
		in.day = 20000;
		in.minute = HM(12, 0);
		dnd_decide(w, 3, &in, &out);
		assert(!out.ring && !strcmp(out.reason, "holiday:New Year"));
		assert(sched_active(w, 3, MON, HM(12, 0)) == -1);

		in.day = 19000;
		in.minute = HM(12, 0);
		in.quick = 1;
		in.quick_until = 1600;
		dnd_decide(w, 2, &in, &out);
		assert(!out.ring && out.quiet && !strcmp(out.reason, "quick"));
		in.now = 1600;
		dnd_decide(w, 2, &in, &out);
		assert(out.ring && !out.quiet);
		in.now = 1000;
		in.clock_ok = 0;
		dnd_decide(w, 3, &in, &out);
		assert(out.ring && !out.quiet);
		in.quick_until = 0;
		dnd_decide(w, 3, &in, &out);
		assert(!out.ring && !strcmp(out.reason, "quick"));
		in.allowed = 1;
		dnd_decide(w, 3, &in, &out);
		assert(out.ring);
	}

	assert(sched_parse_action("once") == ACT_ONCE);
	assert(sched_parse_action("busy") == ACT_BUSY);
	assert(sched_parse_action("silent") == ACT_SILENT);
	assert(sched_parse_action("x") == ACT_SILENT);
	assert(sched_parse_date("2026-12-25") == 20812);
	assert(sched_parse_date("2026-13-01") == -1);
	assert(sched_parse_date("") == -1);

	printf("schedule ok\n");
	return 0;
}
