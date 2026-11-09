/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2026 perz3us */
#ifndef RULES_H
#define RULES_H

enum rules_mode { RULES_OFF, RULES_BLOCK, RULES_ALLOW };

int rules_barred(const char *number, int mode, const char list[][32], int n);

#endif
