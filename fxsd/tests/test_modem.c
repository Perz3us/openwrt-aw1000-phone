// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 perz3us
#define AT_LOCK_DIR "/tmp"
#include <assert.h>
#include "../src/modem.c"

int main(void)
{
	int other = open("/tmp/5gmodem-at-ttyUSB2.lock", O_RDWR | O_CREAT, 0644);
	long t0;
	int fd;

	assert(other >= 0 && !flock(other, LOCK_EX));

	t0 = ms_now();
	assert(at_lock("/dev/ttyUSB2", t0 + 200) < 0);
	assert(ms_now() - t0 >= 200);

	fd = at_lock("/dev/ttyUSB3", ms_now() + 200);
	assert(fd >= 0);
	close(fd);

	flock(other, LOCK_UN);
	fd = at_lock("/dev/ttyUSB2", ms_now() + 200);
	assert(fd >= 0);
	assert(flock(other, LOCK_EX | LOCK_NB));
	close(fd);
	assert(!flock(other, LOCK_EX | LOCK_NB));

	{
		char r[256];

		assert(!mm_reply("response: '+CLCC: 1,1,4,0,0,\"0771234567\",129'\n", r, sizeof(r)));
		assert(!strcmp(r, "+CLCC: 1,1,4,0,0,\"0771234567\",129\r\nOK\r\n"));
		assert(!mm_reply("response: ''\n", r, sizeof(r)) && !strcmp(r, "\r\nOK\r\n"));
		assert(!mm_reply("error: command failed: 'GDBus.Error:org.freedesktop.ModemManager1.Error.MobileEquipment.Unknown: +CME ERROR: 30'\n",
				 r, sizeof(r)) && strstr(r, "ERROR\r\n"));
		assert(mm_reply("error: couldn't find modem\n", r, sizeof(r)) < 0 && !mm_no_at);
		assert(mm_reply("error: command failed: 'GDBus.Error:org.freedesktop.ModemManager1.Error.Core.Unauthorized: operation only allowed in debug mode'\n",
				r, sizeof(r)) < 0 && mm_no_at && !mm_owns());
	}

	puts("test_modem ok");
	return 0;
}
