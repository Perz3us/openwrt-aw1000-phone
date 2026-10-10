// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 perz3us
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "patch.h"

static uint32_t get32(const uint8_t *b)
{
	return b[0] | (b[1] << 8) | (b[2] << 16) | ((uint32_t)b[3] << 24);
}

static uint16_t get16(const uint8_t *b)
{
	return b[0] | (b[1] << 8);
}

void si_patch_free(struct si_patch *p)
{
	free(p->data);
	free(p->ps_addr);
	free(p->ps_data);
	memset(p, 0, sizeof(*p));
}

int si_patch_parse(const uint8_t *b, size_t len, struct si_patch *p)
{
	size_t o, need;
	uint32_t i;

	memset(p, 0, sizeof(*p));
	if (len < 12 || memcmp(b, "FXSP", 4))
		goto bad;
	p->serial = get32(b + 4);
	p->n_data = get32(b + 8);
	if (p->n_data == 0 || p->n_data > SI_PATCH_MAX_DATA)
		goto bad;
	o = 12;
	need = o + 4 * (size_t)p->n_data + 2 * SI_PATCH_ENTRIES + 4;
	if (len < need)
		goto bad;
	p->data = malloc(4 * p->n_data);
	if (!p->data)
		goto bad;
	for (i = 0; i < p->n_data; i++, o += 4)
		p->data[i] = get32(b + o);
	for (i = 0; i < SI_PATCH_ENTRIES; i++, o += 2)
		p->entries[i] = get16(b + o);
	p->n_ps = get32(b + o);
	o += 4;
	if (p->n_ps > SI_PATCH_MAX_PS || len != o + 6 * (size_t)p->n_ps)
		goto bad;
	p->ps_addr = malloc(2 * p->n_ps + 1);
	p->ps_data = malloc(4 * p->n_ps + 1);
	if (!p->ps_addr || !p->ps_data)
		goto bad;
	for (i = 0; i < p->n_ps; i++, o += 2)
		p->ps_addr[i] = get16(b + o);
	for (i = 0; i < p->n_ps; i++, o += 4)
		p->ps_data[i] = get32(b + o);
	return 0;
bad:
	si_patch_free(p);
	return -EINVAL;
}

int si_patch_load_file(const char *path, struct si_patch *p)
{
	uint8_t buf[16384];
	size_t n;
	FILE *f = fopen(path, "rb");

	memset(p, 0, sizeof(*p));
	if (!f)
		return -errno;
	n = fread(buf, 1, sizeof(buf), f);
	fclose(f);
	return si_patch_parse(buf, n, p);
}
