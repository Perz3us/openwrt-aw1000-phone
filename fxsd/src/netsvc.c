// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 perz3us
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "netsvc.h"

static const char *line_end(const char *p)
{
	while (*p && *p != '\r' && *p != '\n')
		p++;
	return p;
}

static int reg_stat(const char *resp, const char *tag)
{
	const char *p;
	char *e;
	long a, b;

	if (!resp || !(p = strstr(resp, tag)))
		return -1;
	p += strlen(tag);
	a = strtol(p, &e, 10);
	if (e == p)
		return -1;
	p = e;
	while (*p == ' ')
		p++;
	if (*p != ',')
		return (int)a;
	p++;
	while (*p == ' ')
		p++;
	if (!isdigit((unsigned char)*p))
		return (int)a;
	b = strtol(p, NULL, 10);
	return (int)b;
}

int netsvc_registered(const char *creg, const char *cereg, const char *c5greg)
{
	int s[3], i, seen = 0;

	s[0] = reg_stat(creg, "+CREG:");
	s[1] = reg_stat(cereg, "+CEREG:");
	s[2] = reg_stat(c5greg, "+C5GREG:");
	for (i = 0; i < 3; i++) {
		if (s[i] < 0)
			continue;
		if (s[i] == 1 || s[i] == 5)
			return 1;
		seen = 1;
	}
	return seen ? 0 : -1;
}

static int split(char *line, char **f, int max)
{
	int n = 0, q = 0;

	f[n++] = line;
	for (; *line; line++) {
		if (*line == '"')
			q = !q;
		else if (*line == ',' && !q && n < max) {
			*line = 0;
			f[n++] = line + 1;
		}
	}
	return n;
}

static void unquote(char *s)
{
	size_t n = strlen(s);

	if (n >= 2 && s[0] == '"' && s[n - 1] == '"') {
		memmove(s, s + 1, n - 2);
		s[n - 2] = 0;
	}
}

int netsvc_ccfc(const char *resp, struct netsvc_fwd *out)
{
	const char *p = resp, *e;
	struct netsvc_fwd best;
	int found = 0, seen = 0;

	if (!resp || !out)
		return -1;
	memset(&best, 0, sizeof(best));
	while ((p = strstr(p, "+CCFC:"))) {
		char line[160], *f[8];
		int n, st;

		p += 6;
		e = line_end(p);
		if ((size_t)(e - p) >= sizeof(line)) {
			p = e;
			continue;
		}
		memcpy(line, p, e - p);
		line[e - p] = 0;
		p = e;
		n = split(line, f, 8);
		while (*f[0] == ' ')
			f[0]++;
		if ((*f[0] != '0' && *f[0] != '1') || f[0][1])
			continue;
		seen = 1;
		if (n > 1 && !(atoi(f[1]) & 1))
			continue;
		st = *f[0] == '1';
		if (found && (best.active || !st))
			continue;
		memset(&best, 0, sizeof(best));
		best.active = st;
		if (n > 2) {
			unquote(f[2]);
			if (strlen(f[2]) < sizeof(best.number))
				strcpy(best.number, f[2]);
		}
		if (n > 6)
			best.seconds = atoi(f[6]);
		found = 1;
	}
	if (!found && !seen)
		return -1;
	*out = best;
	return 0;
}

static int hexval(int c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

static int is_hex(const char *s, size_t len)
{
	size_t i;

	if (!len || len % 2)
		return 0;
	for (i = 0; i < len; i++)
		if (hexval((unsigned char)s[i]) < 0)
			return 0;
	return 1;
}

static int put_utf8(char *out, size_t n, size_t *o, unsigned c)
{
	char b[4];
	int l;

	if (c < 0x80) {
		b[0] = c;
		l = 1;
	} else if (c < 0x800) {
		b[0] = 0xC0 | (c >> 6);
		b[1] = 0x80 | (c & 0x3F);
		l = 2;
	} else if (c < 0x10000) {
		b[0] = 0xE0 | (c >> 12);
		b[1] = 0x80 | ((c >> 6) & 0x3F);
		b[2] = 0x80 | (c & 0x3F);
		l = 3;
	} else {
		b[0] = 0xF0 | (c >> 18);
		b[1] = 0x80 | ((c >> 12) & 0x3F);
		b[2] = 0x80 | ((c >> 6) & 0x3F);
		b[3] = 0x80 | (c & 0x3F);
		l = 4;
	}
	if (*o + l + 1 > n)
		return -1;
	memcpy(out + *o, b, l);
	*o += l;
	return 0;
}

static unsigned hex2(const char *s)
{
	return hexval((unsigned char)s[0]) << 4 | hexval((unsigned char)s[1]);
}

static void ucs2_decode(const char *s, size_t len, char *out, size_t n)
{
	size_t i, o = 0;
	unsigned c, lo;

	for (i = 0; i + 4 <= len; i += 4) {
		c = hex2(s + i) << 8 | hex2(s + i + 2);
		if (c >= 0xD800 && c < 0xDC00 && i + 8 <= len) {
			lo = hex2(s + i + 4) << 8 | hex2(s + i + 6);
			if (lo >= 0xDC00 && lo < 0xE000) {
				c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
				i += 4;
			}
		}
		if (put_utf8(out, n, &o, c))
			break;
	}
	out[o] = 0;
}

static int dcs_ucs2(long dcs)
{
	if (dcs == 0x11)
		return 1;
	return ((dcs & 0xC0) == 0 || (dcs & 0xC0) == 0x40) && (dcs & 0x0C) == 0x08;
}

static int dcs_8bit(long dcs)
{
	return dcs >= 0 && (((dcs & 0xC0) == 0 || (dcs & 0xC0) == 0x40) ? (dcs & 0x0C) == 0x04 : (dcs & 0xF4) == 0xF4);
}

static int looks_ucs2(const char *s, size_t len)
{
	size_t i, hi = 0;

	if (!is_hex(s, len) || len % 4)
		return 0;
	for (i = 0; i < len; i += 4) {
		if (s[i] != '0' || s[i + 1] != '0' || hex2(s + i + 2) < 0x20)
			continue;
		hi++;
	}
	return hi * 4 > len / 4 * 3;
}

static const char *find_close(const char *p, long *dcs)
{
	const char *c, *d;

	for (c = p; (c = strchr(c, '"')); c++) {
		d = c + 1;
		if (!*d || *d == '\r' || *d == '\n') {
			*dcs = -1;
			return c;
		}
		if (*d == ',' && isdigit((unsigned char)d[1])) {
			*dcs = strtol(d + 1, (char **)&d, 10);
			if (!*d || *d == '\r' || *d == '\n')
				return c;
		}
	}
	return NULL;
}

int netsvc_cusd(const char *resp, char *text, size_t n)
{
	const char *p, *q1, *q2;
	char *x;
	long st, dcs = -1;
	size_t len;

	if (n)
		text[0] = 0;
	if (!resp || !(p = strstr(resp, "+CUSD:")))
		return -1;
	p += 6;
	st = strtol(p, &x, 10);
	if (x == p)
		return -1;
	q1 = memchr(x, '"', line_end(x) - x);
	if (!q1 || !n)
		return (int)st;
	q2 = find_close(q1 + 1, &dcs);
	if (!q2)
		return (int)st;
	q1++;
	len = q2 - q1;
	if (len % 4 == 0 && is_hex(q1, len) && (dcs_ucs2(dcs) || (!dcs_8bit(dcs) && looks_ucs2(q1, len)))) {
		ucs2_decode(q1, len, text, n);
		return (int)st;
	}
	if (len >= n) {
		len = n - 1;
		while (len && ((unsigned char)q1[len] & 0xC0) == 0x80)
			len--;
	}
	memcpy(text, q1, len);
	text[len] = 0;
	return (int)st;
}

int netsvc_ceer_congested(const char *resp)
{
	static const int codes[] = { 34, 38, 41, 42, 44, 47 };
	const char *p, *e;
	unsigned i;
	int q = 0;

	if (!resp || !(p = strstr(resp, "+CEER:")))
		return 0;
	p += 6;
	e = line_end(p);
	while (p < e) {
		if (*p == '"') {
			q = !q;
			p++;
		} else if (!q && isdigit((unsigned char)*p) && !isalnum((unsigned char)p[-1])) {
			long v = strtol(p, (char **)&p, 10);

			if (!isalnum((unsigned char)*p))
				for (i = 0; i < sizeof(codes) / sizeof(*codes); i++)
					if (v == codes[i])
						return 1;
		} else {
			p++;
		}
	}
	return 0;
}

int netsvc_unwrap(const char *json, char *out, size_t n)
{
	const char *p = strstr(json, "\"response\"");
	size_t k = 0;
	char c;

	if (!p || !(p = strchr(p + 10, ':')) || !(p = strchr(p, '"')) || !n)
		return -1;
	for (p++; *p && *p != '"'; p++) {
		c = *p;
		if (c == '\\' && p[1]) {
			c = *++p;
			c = c == 'r' ? '\r' : c == 'n' ? '\n' : c == 't' ? '\t' : c;
		}
		if (k + 1 < n)
			out[k++] = c;
	}
	out[k] = 0;
	return *p == '"' ? 0 : -1;
}

int netsvc_raw_line(const char *json, char *out, size_t n)
{
	const char *p = strstr(json, "\"raw_line\"");
	size_t k = 0;

	if (!p || !(p = strchr(p + 10, ':')) || !(p = strchr(p, '"')) || !n)
		return -1;
	for (p++; *p && *p != '"'; p++) {
		if (*p == '\\' && p[1]) {
			p++;
			if (*p == 'n' || *p == 'r' || *p == 't')
				continue;
		}
		if (k + 1 < n)
			out[k++] = *p;
	}
	out[k] = 0;
	return *p == '"' ? 0 : -1;
}
