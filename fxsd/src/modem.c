// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 perz3us
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include "modem.h"

#define QUECTEL_VID	"2c7c"
#define AT_IFACE	2
#define AT_DAEMON	"/etc/init.d/ubus-at-daemon"

static char at_port[64] = "auto";
static char cur[64];

static int read_line(const char *path, char *buf, size_t n)
{
	FILE *f = fopen(path, "r");
	int ok;

	if (!f)
		return -1;
	ok = fgets(buf, n, f) != NULL;
	fclose(f);
	if (!ok)
		return -1;
	buf[strcspn(buf, "\r\n")] = 0;
	return 0;
}

static int write_file(const char *path, const char *val)
{
	FILE *f = fopen(path, "w");

	if (!f)
		return -1;
	fputs(val, f);
	return fclose(f);
}

static int find_tty(int iface, char *tty, size_t tn, char *dev, size_t dn)
{
	char path[PATH_MAX], real[PATH_MAX], val[16];
	struct dirent *e;
	DIR *d;
	int found = 0;

	if (!(d = opendir("/sys/class/tty")))
		return -1;
	while (!found && (e = readdir(d))) {
		if (strncmp(e->d_name, "ttyUSB", 6))
			continue;
		snprintf(path, sizeof(path), "/sys/class/tty/%s/device/..", e->d_name);
		if (!realpath(path, real))
			continue;
		if (iface >= 0) {
			snprintf(path, sizeof(path), "%s/bInterfaceNumber", real);
			if (read_line(path, val, sizeof(val)) || strtol(val, NULL, 16) != iface)
				continue;
		}
		snprintf(path, sizeof(path), "%s/..", real);
		if (!realpath(path, real))
			continue;
		snprintf(path, sizeof(path), "%s/idVendor", real);
		if (read_line(path, val, sizeof(val)) || strcmp(val, QUECTEL_VID))
			continue;
		if (tty)
			snprintf(tty, tn, "/dev/%s", e->d_name);
		if (dev)
			snprintf(dev, dn, "%s", real);
		found = 1;
	}
	closedir(d);
	return found ? 0 : -1;
}

static const char *port(void)
{
	if (strcmp(at_port, "auto"))
		return at_port;
	if ((!*cur || access(cur, F_OK)) && find_tty(AT_IFACE, cur, sizeof(cur), NULL, 0))
		cur[0] = 0;
	return cur;
}

void modem_set_port(const char *p)
{
	snprintf(at_port, sizeof(at_port), "%s", *p ? p : "auto");
	cur[0] = 0;
}

const char *modem_port(void)
{
	return port();
}

int modem_port_ok(void)
{
	const char *p = port();

	return *p && !access(p, F_OK);
}

static int replug(void)
{
	char dev[PATH_MAX], auth[PATH_MAX + 16];

	if (find_tty(-1, NULL, 0, dev, sizeof(dev)))
		return -1;
	snprintf(auth, sizeof(auth), "%s/authorized", dev);
	if (write_file(auth, "0"))
		return -1;
	sleep(2);
	return write_file(auth, "1");
}

static int at_ready(const char *tty)
{
	struct termios t;
	struct pollfd pf;
	char buf[128];
	size_t got = 0;
	ssize_t k;
	int fd, ok = 0;

	if ((fd = open(tty, O_RDWR | O_NOCTTY | O_NONBLOCK)) < 0)
		return 0;
	if (!tcgetattr(fd, &t)) {
		cfmakeraw(&t);
		cfsetspeed(&t, B115200);
		tcsetattr(fd, TCSANOW, &t);
	}
	tcflush(fd, TCIOFLUSH);
	if (write(fd, "AT\r", 3) == 3) {
		pf.fd = fd;
		pf.events = POLLIN;
		while (!ok && got + 1 < sizeof(buf) && poll(&pf, 1, 1000) > 0) {
			if ((k = read(fd, buf + got, sizeof(buf) - 1 - got)) <= 0)
				break;
			got += k;
			buf[got] = 0;
			ok = strstr(buf, "OK") != NULL;
		}
	}
	close(fd);
	return ok;
}

static int wait_back(int secs)
{
	char tty[64];
	int i;

	for (i = 0; i < secs; i++) {
		if (!find_tty(AT_IFACE, tty, sizeof(tty), NULL, 0) && at_ready(tty))
			break;
		sleep(1);
	}
	cur[0] = 0;
	return i < secs ? 0 : -1;
}

void modem_heal(void)
{
	if (!access(AT_DAEMON, X_OK))
		system(AT_DAEMON " restart >/dev/null 2>&1");
	cur[0] = 0;
}

int modem_restart(void)
{
	char before[64], after[64], r[256];
	int atd = !access(AT_DAEMON, X_OK), i;

	if (find_tty(AT_IFACE, before, sizeof(before), NULL, 0))
		before[0] = 0;
	modem_at("AT+CFUN=1,1", r, sizeof(r));
	if (atd)
		system(AT_DAEMON " stop >/dev/null 2>&1");
	for (i = 0; i < 30 && !find_tty(-1, NULL, 0, NULL, 0); i++)
		sleep(1);
	i = wait_back(180);
	if (!i && *before && (find_tty(AT_IFACE, after, sizeof(after), NULL, 0) || strcmp(before, after))) {
		if (!replug())
			i = wait_back(60);
	}
	if (atd)
		system(AT_DAEMON " start >/dev/null 2>&1");
	return i || !modem_port_ok() ? -1 : 0;
}

int modem_at(const char *cmd, char *resp, size_t n)
{
	const char *ap = port();
	char line[320], esc[128];
	size_t got, k = 0;
	FILE *p;

	if (!*ap || strpbrk(cmd, "'\\") || strpbrk(ap, "'\"\\"))
		return -1;
	for (; *cmd && k + 3 < sizeof(esc); cmd++) {
		if (*cmd == '"')
			esc[k++] = '\\';
		esc[k++] = *cmd;
	}
	esc[k] = 0;
	snprintf(line, sizeof(line),
		 "ubus call at-daemon sendat '{\"at_port\":\"%s\",\"at_cmd\":\"%s\",\"timeout\":3}' 2>/dev/null",
		 ap, esc);
	p = popen(line, "r");
	if (!p)
		return -1;
	got = fread(resp, 1, n - 1, p);
	resp[got] = 0;
	return pclose(p) == 0 ? 0 : -1;
}
