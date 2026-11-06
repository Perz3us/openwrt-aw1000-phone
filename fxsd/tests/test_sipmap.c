// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 perz3us
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/sipmap.h"

static const char qmodem[] =
	"object qmodem_voip\n"
	"event qmodem_voip.call\n"
	"audio 8000\n"
	"method dial originate\n"
	"method digit send_dtmf\n"
	"arg line endpoint\n"
	"field answered_by answer_owner\n"
	"field number remote_number\n"
	"state off disabled\n"
	"state dialing outgoing_setup\n"
	"state alerting early_media\n"
	"state ringing incoming_ringing\n"
	"state ending terminating\n"
	"owner phone rj11\n"
	"owner network cellular\n";

static int load(struct sipmap *m, const char *text)
{
	FILE *f = tmpfile();
	int r;

	assert(f);
	fputs(text, f);
	rewind(f);
	r = sipmap_load(m, f);
	fclose(f);
	return r;
}

int main(void)
{
	static const char *const bad[] = {
		"color dial originate\n",
		"state bogus x\n",
		"method bogus x\n",
		"audio 44100\n",
		"audio\n",
		"state off\n",
		"state off a b\n",
		"object a b\n",
		"state off 1234567890123456789012345678901234567890\n",
		"object 1234567890123456789012345678901234567890\n",
		"state off a\nstate off b\n",
		"state off a\nstate idle a\n",
	};
	struct sipmap m;
	size_t i;

	sipmap_defaults(&m);
	assert(!strcmp(m.object, "fxs_sip") && !strcmp(m.event, "fxs_sip.call") && m.audio == 8000);
	assert(!strcmp(sipmap_out(&m, SIPMAP_METHOD, "dial"), "dial"));
	assert(!strcmp(sipmap_in(&m, SIPMAP_STATE, "ringing"), "ringing"));

	assert(!load(&m, qmodem));
	assert(!strcmp(m.object, "qmodem_voip") && !strcmp(m.event, "qmodem_voip.call") && m.audio == 8000);
	assert(!strcmp(sipmap_out(&m, SIPMAP_METHOD, "dial"), "originate"));
	assert(!strcmp(sipmap_out(&m, SIPMAP_METHOD, "answer"), "answer"));
	assert(!strcmp(sipmap_out(&m, SIPMAP_ARG, "line"), "endpoint"));
	assert(!strcmp(sipmap_out(&m, SIPMAP_FIELD, "number"), "remote_number"));
	assert(!strcmp(sipmap_out(&m, SIPMAP_OWNER, "phone"), "rj11"));
	assert(!strcmp(sipmap_in(&m, SIPMAP_STATE, "incoming_ringing"), "ringing"));
	assert(!strcmp(sipmap_in(&m, SIPMAP_STATE, "active"), "active"));
	assert(!strcmp(sipmap_in(&m, SIPMAP_OWNER, "rj11"), "phone"));
	assert(!strcmp(sipmap_in(&m, SIPMAP_OWNER, "cellular"), "network"));
	assert(!strcmp(sipmap_in(&m, SIPMAP_OWNER, "lan_sip"), "lan_sip"));

	assert(!load(&m, "audio 16000\n"));
	assert(m.audio == 16000 && !strcmp(m.object, "fxs_sip"));
	assert(!load(&m, "\n# my service\r\nobject mine\r\n"));
	assert(!strcmp(m.object, "mine"));

	for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
		assert(load(&m, bad[i]) == -1);
	return 0;
}
