/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2026 perz3us */
#ifndef NETSVC_H
#define NETSVC_H

#include <stddef.h>

struct netsvc_fwd {
	int active;
	char number[32];
	int seconds;
};

int netsvc_registered(const char *creg, const char *cereg, const char *c5greg);
int netsvc_ccfc(const char *resp, struct netsvc_fwd *out);
int netsvc_cusd(const char *resp, char *text, size_t n);
int netsvc_ceer_congested(const char *resp);
int netsvc_unwrap(const char *json, char *out, size_t n);
int netsvc_raw_line(const char *json, char *out, size_t n);

#endif
