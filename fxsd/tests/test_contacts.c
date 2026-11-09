// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 perz3us
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "../src/cid.h"
#include "../src/contacts.h"

static int checksum_ok(const uint8_t *m, int n)
{
	uint8_t sum = 0;
	int i;

	for (i = 0; i < n; i++)
		sum += m[i];
	return !sum;
}

int main(void)
{
	static const char path[] = "/tmp/fxs-contacts-test";
	static const uint8_t plain[] = {
		0x80, 0x10, 0x01, 0x08, '0', '3', '1', '5', '0', '9', '0', '5',
		0x02, 0x04, '0', '7', '7', '1',
	};
	const struct contact *c;
	struct tm t = { .tm_mon = 2, .tm_mday = 15, .tm_hour = 9, .tm_min = 5 };
	uint8_t a[CID_MSG_MAX], b[CID_MSG_MAX];
	char out[32];
	FILE *f;
	int n, m;

	f = fopen(path, "w");
	assert(f);
	fputs("+94771234567\tshort\timportant\tAmma\n0112345678\tdefault\tnever\t\n555\tbad\tnormal\tShort one\n", f);
	fclose(f);
	assert(contacts_load(path) == 3);
	remove(path);

	c = contacts_find("0771234567");
	assert(c && !strcmp(c->name, "Amma") && c->ring == CONTACT_RING_SHORT && c->priority == DND_IMPORTANT);
	c = contacts_find("0112345678");
	assert(c && !c->name[0] && c->ring == CONTACT_RING_DEFAULT && c->priority == DND_NEVER);
	c = contacts_find("555");
	assert(c && c->ring == CONTACT_RING_DEFAULT && c->priority == DND_NORMAL && !strcmp(c->name, "Short one"));
	assert(!contacts_find("0779999999"));
	assert(!contacts_find(""));
	assert(!contacts_load("/nonexistent/file") && !contacts_find("555"));

	assert(cid_name_ascii("Amma", out, sizeof(out)) == 4 && !strcmp(out, "Amma"));
	assert(cid_name_ascii("Jos\xc3\xa9", out, sizeof(out)) == 3 && !strcmp(out, "Jos"));
	assert(cid_name_ascii("\xe0\xb6\x85\xe0\xb6\xb8\xe0\xb7\x8a\xe0\xb6\xb8\xe0\xb7\x8f", out, sizeof(out)) == 0 && !*out);
	assert(cid_name_ascii("abcdefgh", out, 4) == 3 && !strcmp(out, "abc"));

	n = cid_mdmf("0771", NULL, &t, a, sizeof(a));
	assert(n == (int)sizeof(plain) + 1 && !memcmp(a, plain, sizeof(plain)) && checksum_ok(a, n));
	assert(cid_mdmf("0771", "", &t, b, sizeof(b)) == n && !memcmp(a, b, n));
	m = cid_mdmf("0771", "Amma", &t, b, sizeof(b));
	assert(m == n + 6 && checksum_ok(b, m));
	assert(b[1] == a[1] + 6 && !memcmp(b + 2, a + 2, 16));
	assert(b[18] == 0x07 && b[19] == 4 && !memcmp(b + 20, "Amma", 4));

	printf("contacts ok\n");
	return 0;
}
