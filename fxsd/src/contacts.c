// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 perz3us
#include <stdio.h>
#include <string.h>
#include "contacts.h"

#define CONTACTS_MAX 64

static struct contact list[CONTACTS_MAX];
static int count;

static size_t digits_of(const char *s, char *out)
{
	size_t n = 0;

	for (; *s && n + 1 < CLCC_NUM_MAX; s++)
		if (*s >= '0' && *s <= '9')
			out[n++] = *s;
	out[n] = 0;
	return n;
}

int same_number(const char *a, const char *b)
{
	char x[CLCC_NUM_MAX], y[CLCC_NUM_MAX];
	size_t nx = digits_of(a, x), ny = digits_of(b, y);

	if (nx >= 9 && ny >= 9)
		return !strcmp(x + nx - 9, y + ny - 9);
	return nx && !strcmp(x, y);
}

int contact_parse_ring(const char *s)
{
	if (!strcmp(s, "standard"))
		return CONTACT_RING_STANDARD;
	if (!strcmp(s, "short"))
		return CONTACT_RING_SHORT;
	if (!strcmp(s, "double"))
		return CONTACT_RING_DOUBLE;
	return CONTACT_RING_DEFAULT;
}

int contacts_load(const char *path)
{
	char buf[160], *number, *ring, *prio, *name, *sp;
	FILE *f = fopen(path, "r");

	count = 0;
	if (!f)
		return 0;
	while (count < CONTACTS_MAX && fgets(buf, sizeof(buf), f)) {
		buf[strcspn(buf, "\r\n")] = 0;
		number = strtok_r(buf, "\t", &sp);
		ring = strtok_r(NULL, "\t", &sp);
		prio = strtok_r(NULL, "\t", &sp);
		name = strtok_r(NULL, "", &sp);
		if (!number || !ring || !prio)
			continue;
		list[count].priority = !strcmp(prio, "important") ? DND_IMPORTANT : !strcmp(prio, "never") ? DND_NEVER : DND_NORMAL;
		snprintf(list[count].number, sizeof(list[count].number), "%s", number);
		snprintf(list[count].name, sizeof(list[count].name), "%s", name ? name : "");
		list[count].ring = contact_parse_ring(ring);
		count++;
	}
	fclose(f);
	return count;
}

const struct contact *contacts_find(const char *number)
{
	int i;

	for (i = 0; *number && i < count; i++)
		if (same_number(number, list[i].number))
			return &list[i];
	return NULL;
}
