/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2026 perz3us */
#ifndef MODEM_H
#define MODEM_H

#include <stddef.h>

void modem_set_port(const char *port);
int modem_at(const char *cmd, char *resp, size_t n);

#endif
