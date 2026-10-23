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
	char line[320], esc[128];
	size_t got, k = 0;
	FILE *p;

	if (strpbrk(cmd, "'\\") || strpbrk(at_port, "'\"\\"))
		return -1;
	for (; *cmd && k + 3 < sizeof(esc); cmd++) {
		if (*cmd == '"')
			esc[k++] = '\\';
		esc[k++] = *cmd;
	}
	esc[k] = 0;
	snprintf(line, sizeof(line),
		 "ubus call at-daemon sendat '{\"at_port\":\"%s\",\"at_cmd\":\"%s\",\"timeout\":3}' 2>/dev/null",
		 at_port, esc);
	p = popen(line, "r");
	if (!p)
		return -1;
	got = fread(resp, 1, n - 1, p);
	resp[got] = 0;
	return pclose(p) == 0 ? 0 : -1;
}
