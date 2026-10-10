/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2026 perz3us */
#ifndef PATCH_H
#define PATCH_H

#include <stddef.h>
#include <stdint.h>

#define SI_PATCH_ENTRIES 16
#define SI_PATCH_MAX_DATA 1024
#define SI_PATCH_MAX_PS 64

struct si_patch {
	uint32_t serial;
	uint32_t n_data;
	uint32_t *data;
	uint16_t entries[SI_PATCH_ENTRIES];
	uint32_t n_ps;
	uint16_t *ps_addr;
	uint32_t *ps_data;
};

int si_patch_parse(const uint8_t *buf, size_t len, struct si_patch *p);
int si_patch_load_file(const char *path, struct si_patch *p);
void si_patch_free(struct si_patch *p);

#endif
