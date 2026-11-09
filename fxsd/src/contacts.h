/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2026 perz3us */
#ifndef CONTACTS_H
#define CONTACTS_H

#include "clcc.h"
#include "schedule.h"

enum contact_ring { CONTACT_RING_DEFAULT, CONTACT_RING_STANDARD, CONTACT_RING_SHORT, CONTACT_RING_DOUBLE };

struct contact {
	char number[CLCC_NUM_MAX];
	char name[48];
	int ring;
	int priority;
};

int same_number(const char *a, const char *b);
int contact_parse_ring(const char *s);
int contacts_load(const char *path);
const struct contact *contacts_find(const char *number);

#endif
