// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 perz3us
#include <assert.h>
#include <string.h>
#include "../src/voip.h"

static int calls(const char *state, const char *origin, const char *answered_by, struct clcc_call *c)
{
	struct voip_snap s;

	voip_snap_set(&s, state, origin, answered_by, "0771234567", 1);
	return voip_calls(&s, c);
}

int main(void)
{
	struct clcc_call c;
	struct voip_snap s;

	assert(calls("ringing", "network", "", &c) == 1);
	assert(c.stat == CLCC_INCOMING && c.dir == 1 && !strcmp(c.number, "0771234567"));

	assert(calls("dialing", "phone", "", &c) == 1 && c.stat == CLCC_DIALING);
	assert(calls("alerting", "phone", "", &c) == 1 && c.stat == CLCC_ALERTING);
	assert(calls("active", "phone", "phone", &c) == 1 && c.stat == CLCC_ACTIVE);
	assert(calls("active", "network", "phone", &c) == 1 && c.stat == CLCC_ACTIVE && c.dir == 1);

	assert(calls("active", "network", "sip1", &c) == 0);
	voip_snap_set(&s, "active", "network", "sip1", "", 1);
	assert(s.owner == VO_OTHER && s.answered && voip_sip_audio(&s));

	voip_snap_set(&s, "active", "network", "", "", 1);
	assert(s.owner == VO_NONE && !voip_sip_audio(&s));
	voip_snap_set(&s, "active", "none", "none", "", 1);
	assert(s.owner == VO_NONE && !s.answered);

	voip_snap_set(&s, "alerting", "sip1", "", "", 1);
	assert(s.owner == VO_OTHER && voip_sip_audio(&s));

	assert(calls("idle", "", "", &c) == 0);
	assert(calls("ending", "phone", "phone", &c) == 0);
	assert(calls("something_new", "phone", "phone", &c) == 0);
	voip_snap_set(&s, "active", "network", "Pixel", "", 1);
	assert(!strcmp(s.who, "Pixel") && !s.from_sip);
	voip_snap_set(&s, "dialing", "Laptop", "none", "", 1);
	assert(!strcmp(s.who, "Laptop") && s.from_sip);
	voip_snap_set(&s, "active", "phone", "phone", "", 1);
	assert(!s.who[0] && !s.from_sip);
	voip_snap_set(&s, "ringing", "network", "none", "", 1);
	assert(!s.who[0] && !s.from_sip);
	voip_snap_set(&s, "off", "", "", "", 0);
	assert(s.st == VS_DISABLED && !s.enabled);
	return 0;
}
