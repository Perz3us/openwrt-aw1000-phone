/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2026 perz3us */
#ifndef CLCC_H
#define CLCC_H

#define CLCC_NUM_MAX 32

enum clcc_stat {
	CLCC_ACTIVE = 0,
	CLCC_HELD = 1,
	CLCC_DIALING = 2,
	CLCC_ALERTING = 3,
	CLCC_INCOMING = 4,
	CLCC_WAITING = 5,
};

struct clcc_call {
	int id;
	int dir;
	int stat;
	char number[CLCC_NUM_MAX];
};

#define CLCC_MAX 4

int clcc_parse_voice(const char *resp, struct clcc_call *call);
int clcc_parse_all(const char *resp, struct clcc_call *calls, int max);

#endif
