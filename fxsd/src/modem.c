// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 perz3us
#include <stdio.h>
#include <string.h>
#include "modem.h"

static char at_port[64] = "/dev/ttyUSB2";

void modem_set_port(const char *port)
{
	snprintf(at_port, sizeof(at_port), "%s", port);
}

int modem_at(const char *cmd, char *resp, size_t n)
{
	char line[256];
	size_t got;
	FILE *p;

	if (strpbrk(cmd, "'\"\\") || strpbrk(at_port, "'\"\\"))
		return -1;
	snprintf(line, sizeof(line),
		 "ubus call at-daemon sendat '{\"at_port\":\"%s\",\"at_cmd\":\"%s\",\"timeout\":3}' 2>/dev/null",
		 at_port, cmd);
	p = popen(line, "r");
	if (!p)
		return -1;
	got = fread(resp, 1, n - 1, p);
	resp[got] = 0;
	return pclose(p) == 0 ? 0 : -1;
}
