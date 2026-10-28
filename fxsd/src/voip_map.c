// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 perz3us
#include <stdio.h>
#include <string.h>
#include "voip.h"

static enum voip_state state_of(const char *s)
{
	static const struct { const char *name; enum voip_state st; } map[] = {
		{ "disabled", VS_DISABLED }, { "idle", VS_IDLE }, { "outgoing_setup", VS_SETUP },
		{ "incoming_ringing", VS_RINGING }, { "early_media", VS_EARLY }, { "active", VS_ACTIVE },
		{ "terminating", VS_ENDING },
	};
	size_t i;

	for (i = 0; s && i < sizeof(map) / sizeof(map[0]); i++)
		if (!strcmp(s, map[i].name))
			return map[i].st;
	return VS_OTHER;
}

static int named(const char *s)
{
	return s && *s && strcmp(s, "none");
}

static enum voip_owner owner_of(const char *s)
{
	if (!named(s) || !strcmp(s, "cellular"))
		return VO_NONE;
	return strcmp(s, "rj11") ? VO_OTHER : VO_RJ11;
}

void voip_snap_set(struct voip_snap *s, const char *state, const char *origin,
		   const char *answer_owner, const char *number, int enabled)
{
	s->st = state_of(state);
	s->answered = named(answer_owner);
	s->owner = owner_of(s->answered ? answer_owner : origin);
	s->enabled = enabled;
	snprintf(s->number, sizeof(s->number), "%s", number ? number : "");
}

int voip_calls(const struct voip_snap *s, struct clcc_call *c)
{
	memset(c, 0, sizeof(*c));
	c->id = 1;
	snprintf(c->number, sizeof(c->number), "%s", s->number);
	switch (s->st) {
	case VS_RINGING:
		if (!s->answered) {
			c->dir = 1;
			c->stat = CLCC_INCOMING;
			return 1;
		}
		break;
	case VS_SETUP:
	case VS_EARLY:
		if (s->owner != VO_RJ11 || s->answered)
			break;
		c->stat = s->st == VS_SETUP ? CLCC_DIALING : CLCC_ALERTING;
		return 1;
	case VS_ACTIVE:
		break;
	default:
		return 0;
	}
	if (s->owner != VO_RJ11)
		return 0;
	c->dir = s->answered;
	c->stat = CLCC_ACTIVE;
	return 1;
}

int voip_sip_audio(const struct voip_snap *s)
{
	return s->owner == VO_OTHER &&
	       (s->st == VS_ACTIVE || s->st == VS_EARLY || (s->st == VS_RINGING && s->answered));
}
