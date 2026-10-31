/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2026 perz3us */
#ifndef COUNTRY_H
#define COUNTRY_H

struct country {
	short mcc;
	const char *cc, *trunk, *intl;
};

const struct country *country_find(int mcc);

#endif
