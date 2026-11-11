// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 perz3us
#include <assert.h>
#include <stdio.h>
#include "../src/bringup.h"

int main(void)
{
	assert(qaudcfg_pcmidx("AT+QAUDCFG=\"pcmidx\"\r\n+QAUDCFG: \"PCMIDX\",2\r\n\r\nOK\r\n") == 2);
	assert(qaudcfg_pcmidx("+QAUDCFG: \"PCMIDX\",1\r\nOK") == 1);
	assert(qaudcfg_pcmidx("{\"response\":\"\\r\\n+QAUDCFG: \\\"PCMIDX\\\",2\\r\\nOK\"}") == 2);
	assert(qaudcfg_pcmidx("+QAUDCFG: \"PCMIDX\",") == -1);
	assert(qaudcfg_pcmidx("+QAUDCFG: \"PCMIDX\",7") == -1);
	assert(qaudcfg_pcmidx("ERROR") == -1);
	assert(qaudcfg_pcmidx("") == -1);

	assert(qpcmv_on("AT+QPCMV?\r\n+QPCMV: 0,0\r\n\r\nOK\r\n") == 0);
	assert(qpcmv_on("+QPCMV: 1,0\r\nOK") == 1);
	assert(qpcmv_on("+QPCMV: ") == -1);
	assert(qpcmv_on("ERROR") == -1);

	assert(qdai_is_slave("+QDAI: x,1,0,4,0,0,1,1") == 1);
	assert(qdai_rate("+QDAI: x,1,0,4,0,1,1,1") == 1);
	puts("test_modem_audio ok");
	return 0;
}
