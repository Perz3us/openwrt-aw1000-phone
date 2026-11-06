/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2026 perz3us */
#ifndef VOIP_H
#define VOIP_H

#include "clcc.h"
#include "sipmap.h"

enum voip_state { VS_DISABLED, VS_IDLE, VS_SETUP, VS_RINGING, VS_EARLY, VS_ACTIVE, VS_ENDING, VS_OTHER };
enum voip_owner { VO_NONE, VO_RJ11, VO_OTHER };

struct voip_snap {
	enum voip_state st;
	enum voip_owner owner;
	int answered, enabled;
	char number[CLCC_NUM_MAX];
};

void voip_snap_set(struct voip_snap *s, const char *state, const char *origin,
		   const char *answered_by, const char *number, int enabled);
int voip_calls(const struct voip_snap *s, struct clcc_call *c);
int voip_sip_audio(const struct voip_snap *s);

void voip_service(const struct sipmap *m);
int voip_connect(void);
void voip_disconnect(void);
int voip_connected(void);
void voip_events(void);
int voip_refresh(void);
int voip_request(const char *method, const char *number);
int voip_dtmf(char digit);
const struct voip_snap *voip_snap(void);
int voip_changed(void);

#endif
