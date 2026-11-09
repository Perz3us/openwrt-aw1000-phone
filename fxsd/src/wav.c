// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 perz3us
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "wav.h"

static void le32(uint8_t *p, uint32_t v)
{
	p[0] = v;
	p[1] = v >> 8;
	p[2] = v >> 16;
	p[3] = v >> 24;
}

static void le16(uint8_t *p, uint16_t v)
{
	p[0] = v;
	p[1] = v >> 8;
}

static uint32_t rd32(const uint8_t *p)
{
	return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}

void wav_header(uint8_t h[WAV_HEADER], int rate, int channels, uint32_t data_bytes)
{
	memcpy(h, "RIFF", 4);
	le32(h + 4, 36 + data_bytes);
	memcpy(h + 8, "WAVEfmt ", 8);
	le32(h + 16, 16);
	le16(h + 20, 1);
	le16(h + 22, channels);
	le32(h + 24, rate);
	le32(h + 28, rate * channels * 2);
	le16(h + 32, channels * 2);
	le16(h + 34, 16);
	memcpy(h + 36, "data", 4);
	le32(h + 40, data_bytes);
}

int wav_fix(const char *path)
{
	uint8_t h[WAV_HEADER];
	struct stat st;
	uint32_t data, align;
	int fd = open(path, O_RDWR), changed = 0;

	if (fd < 0)
		return -1;
	if (fstat(fd, &st) || st.st_size < WAV_HEADER || read(fd, h, WAV_HEADER) != WAV_HEADER ||
	    memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVEfmt ", 8) || memcmp(h + 36, "data", 4)) {
		close(fd);
		return -1;
	}
	align = h[32] | h[33] << 8;
	data = st.st_size - WAV_HEADER;
	if (align)
		data -= data % align;
	if (rd32(h + 40) != data || rd32(h + 4) != 36 + data) {
		le32(h + 4, 36 + data);
		le32(h + 40, data);
		changed = pwrite(fd, h, WAV_HEADER, 0) == WAV_HEADER;
	}
	close(fd);
	return changed;
}

int16_t wav_mix(int16_t a, int16_t b)
{
	return (int16_t)(((int)a + b) / 2);
}
