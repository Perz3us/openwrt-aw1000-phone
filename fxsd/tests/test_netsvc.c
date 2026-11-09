// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 perz3us
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/netsvc.h"

static void registered(void)
{
	assert(netsvc_registered("\r\n+CREG: 0,1\r\n\r\nOK\r\n", NULL, NULL) == 1);
	assert(netsvc_registered("+CREG: 0,5\r\nOK", "", "") == 1);
	assert(netsvc_registered("+CREG: 0,0\r\nOK", "+CEREG: 0,5\r\nOK", "+C5GREG: 0,0\r\nOK") == 1);
	assert(netsvc_registered("ERROR", "+CEREG: 0,5\r\nOK", "+C5GREG: 0,0\r\nOK") == 1);
	assert(netsvc_registered("+CREG: 0,0\r\nOK", "+CEREG: 0,2\r\nOK", "+C5GREG: 0,0\r\nOK") == 0);
	assert(netsvc_registered(NULL, NULL, "+C5GREG: 0,3\r\nOK") == 0);
	assert(netsvc_registered("+CREG: 2,1,\"1A2B\",\"00FF\"\r\nOK", NULL, NULL) == 1);
	assert(netsvc_registered("ERROR", "ERROR", "ERROR") == -1);
	assert(netsvc_registered(NULL, NULL, NULL) == -1);
	assert(netsvc_registered("", "", "") == -1);
	assert(netsvc_registered("+CEREG: 0,1\r\nOK", NULL, NULL) == -1);
}

static void ccfc(void)
{
	struct netsvc_fwd f;

	memset(&f, 0x55, sizeof(f));
	assert(netsvc_ccfc("\r\n+CCFC: 1,1,\"+94771234567\",145,,,20\r\n\r\nOK\r\n", &f) == 0);
	assert(f.active == 1 && f.seconds == 20 && !strcmp(f.number, "+94771234567"));
	assert(netsvc_ccfc("+CCFC: 1,1,\"0771234567\",129\r\nOK", &f) == 0);
	assert(f.active == 1 && f.seconds == 0 && !strcmp(f.number, "0771234567"));
	assert(netsvc_ccfc("\r\n+CCFC: 0,1\r\n\r\nOK\r\n", &f) == 0);
	assert(f.active == 0 && f.seconds == 0 && f.number[0] == 0);
	assert(netsvc_ccfc("+CCFC: 0,7\r\n+CCFC: 1,1,\"+4412345\",145,,,30\r\nOK", &f) == 0);
	assert(f.active == 1 && f.seconds == 30 && !strcmp(f.number, "+4412345"));
	assert(netsvc_ccfc("+CCFC: 1,2,\"+4412\",145\r\nOK", &f) == 0);
	assert(f.active == 0 && f.number[0] == 0);
	assert(netsvc_ccfc("+CCFC: 1,2,\"+4412\",145\r\n+CCFC: 0,1\r\nOK", &f) == 0);
	assert(f.active == 0);
	assert(netsvc_ccfc("+CCFC: 1,2,\"+4412\",145\r\n+CCFC: 1,1,\"+9477\",145,,,5\r\nOK", &f) == 0);
	assert(f.active == 1 && !strcmp(f.number, "+9477") && f.seconds == 5);
	assert(netsvc_ccfc("+CCFC: 1,7,\"+9477\",145\r\nOK", &f) == 0);
	assert(f.active == 1);
	assert(netsvc_ccfc("ERROR", &f) == -1);
	assert(netsvc_ccfc("+CME ERROR: 30", &f) == -1);
	assert(netsvc_ccfc("", &f) == -1);
	assert(netsvc_ccfc(NULL, &f) == -1);
	assert(netsvc_ccfc("+CCFC: 1,1,\"+94771234567\",145,,,20", NULL) == -1);
}

static void cusd(void)
{
	char t[64];

	assert(netsvc_cusd("\r\n+CUSD: 0,\"00420061006C\",72\r\n", t, sizeof(t)) == 0);
	assert(!strcmp(t, "Bal"));
	assert(netsvc_cusd("+CUSD: 1,\"Your balance is Rs 25.50\",15\r\nOK", t, sizeof(t)) == 1);
	assert(!strcmp(t, "Your balance is Rs 25.50"));
	assert(netsvc_cusd("+CUSD: 2", t, sizeof(t)) == 2);
	assert(t[0] == 0);
	assert(netsvc_cusd("+CUSD: 0,\"00480065006C006C006F0020D83DDE00\",72", t, sizeof(t)) == 0);
	assert(!strcmp(t, "Hello \xF0\x9F\x98\x80"));
	assert(netsvc_cusd("+CUSD: 0,\"20AC00E9\",72", t, sizeof(t)) == 0);
	assert(!strcmp(t, "\xE2\x82\xAC\xC3\xA9"));
	assert(netsvc_cusd("+CUSD: 1,\"1. Balance\r\n2. Offers\",15\r\nOK", t, sizeof(t)) == 1);
	assert(!strcmp(t, "1. Balance\r\n2. Offers"));
	assert(netsvc_cusd("+CUSD: 0,\"00420061006C\",8", t, sizeof(t)) == 0);
	assert(!strcmp(t, "Bal"));
	assert(netsvc_cusd("+CUSD: 0,\"00420061006C\",4", t, sizeof(t)) == 0);
	assert(!strcmp(t, "00420061006C"));
	assert(netsvc_cusd("+CUSD: 0,\"00420061006C\",15", t, sizeof(t)) == 0);
	assert(!strcmp(t, "Bal"));
	assert(netsvc_cusd("+CUSD: 0,\"00420061006C\"", t, sizeof(t)) == 0);
	assert(!strcmp(t, "Bal"));
	assert(netsvc_cusd("+CUSD: 0,\"1234\",15", t, sizeof(t)) == 0);
	assert(!strcmp(t, "1234"));
	assert(netsvc_cusd("+CUSD: 0,\"C8329BFD06\",15", t, sizeof(t)) == 0);
	assert(!strcmp(t, "C8329BFD06"));
	assert(netsvc_cusd("+CUSD: 0,\"0012\",15", t, sizeof(t)) == 0);
	assert(!strcmp(t, "0012"));
	assert(netsvc_cusd("+CUSD: 0,\"a\xC3\xA9\xC3\xA9\",15", t, 3) == 0);
	assert(!strcmp(t, "a"));
	assert(netsvc_cusd("+CUSD: 0,\"a\xC3\xA9\xC3\xA9\",15", t, 4) == 0);
	assert(!strcmp(t, "a\xC3\xA9"));
	assert(netsvc_cusd("+CUSD: 0,\"00420061006C\",72", t, 3) == 0);
	assert(!strcmp(t, "Ba"));
	assert(netsvc_cusd("+CUSD: 0,\"Pay 100\"", t, sizeof(t)) == 0);
	assert(!strcmp(t, "Pay 100"));
	assert(netsvc_cusd("+CUSD: 1,\"1. Balance\r\n2. Offers\",15\r\n+CMTI: \"SM\",1\r\nOK", t, sizeof(t)) == 1);
	assert(!strcmp(t, "1. Balance\r\n2. Offers"));
	assert(netsvc_cusd("+CUSD: 2\r\n+CMTI: \"SM\",1", t, sizeof(t)) == 2);
	assert(t[0] == 0);
	assert(netsvc_cusd("+CUSD: 0,\"Say \"hi\" now\",15\r\n", t, sizeof(t)) == 0);
	assert(!strcmp(t, "Say \"hi\" now"));
	assert(netsvc_cusd("+CUSD: 0,\"00420061006C\",72\r\n+CUSD: 0,\"Other\",15", t, sizeof(t)) == 0);
	assert(!strcmp(t, "Bal"));
	assert(netsvc_cusd("+CUSD: 0,\"unterminated", t, sizeof(t)) == 0);
	assert(t[0] == 0);
	assert(netsvc_cusd("ERROR", t, sizeof(t)) == -1);
	assert(netsvc_cusd("+CME ERROR: 30", t, sizeof(t)) == -1);
	assert(netsvc_cusd(NULL, t, sizeof(t)) == -1);
	assert(netsvc_cusd("+CUSD: 0,\"x\",15", t, 0) == 0);
}

static void ceer(void)
{
	assert(netsvc_ceer_congested("+CEER: 34\r\nOK") == 1);
	assert(netsvc_ceer_congested("+CEER: \"CC\",34\r\nOK") == 1);
	assert(netsvc_ceer_congested("+CEER: 38") == 1);
	assert(netsvc_ceer_congested("+CEER: 41") == 1);
	assert(netsvc_ceer_congested("+CEER: 42") == 1);
	assert(netsvc_ceer_congested("+CEER: 44") == 1);
	assert(netsvc_ceer_congested("+CEER: 47") == 1);
	assert(netsvc_ceer_congested("+CEER: 16") == 0);
	assert(netsvc_ceer_congested("+CEER: 17") == 0);
	assert(netsvc_ceer_congested("+CEER: \"cause 134 text\",16") == 0);
	assert(netsvc_ceer_congested("ERROR") == 0);
	assert(netsvc_ceer_congested("") == 0);
	assert(netsvc_ceer_congested(NULL) == 0);
}

int main(void)
{
	char raw[64];

	assert(!netsvc_raw_line("{ \"qmodem.at.line\": {\"at_port\":\"/dev/x\",\"raw_line\":\"+CUSD: 1,\\\"Hi\\\",15\"} }", raw, sizeof(raw)));
	assert(!strcmp(raw, "+CUSD: 1,\"Hi\",15"));
	assert(netsvc_raw_line("{\"other\":1}", raw, sizeof(raw)) == -1);
	assert(netsvc_raw_line("{\"raw_line\":\"open", raw, sizeof(raw)) == -1);

	registered();
	ccfc();
	cusd();
	ceer();
	{
		static const char head[] = "{\n\t\"port\": \"/dev/ttyUSB2\",\n\t\"end_flags_used\": [\n\t\t\"OK\",\n\t\t\"ERROR\",\n\t\t\"+CME ERROR:\"\n\t],\n\t\"status\": \"success\",\n\t\"response\": ";
		char json[512], body[256];
		struct netsvc_fwd fw;

		snprintf(json, sizeof(json), "%s\"AT+CREG?\\r\\r\\n+CREG: 2,1,\\\"D83B\\\",\\\"051E601\\\",13\\r\\n\\r\\nOK\\r\\n\",\n\t\"end_flag_matched\": \"OK\"\n}", head);
		assert(!netsvc_unwrap(json, body, sizeof(body)));
		assert(!strstr(body, "ERROR") && strstr(body, "OK\r\n"));
		assert(netsvc_registered(body, NULL, NULL) == 1);

		snprintf(json, sizeof(json), "%s\"AT+CCFC=0,2\\r\\r\\n+CME ERROR: 30\\r\\n\"}", head);
		assert(!netsvc_unwrap(json, body, sizeof(body)));
		assert(strstr(body, "ERROR") && !strstr(body, "OK"));

		snprintf(json, sizeof(json), "%s\"\\r\\n+CCFC: 1,1,\\\"+94771234567\\\",145\\r\\n\\r\\nOK\\r\\n\"}", head);
		assert(!netsvc_unwrap(json, body, sizeof(body)));
		assert(!netsvc_ccfc(body, &fw) && fw.active && !strcmp(fw.number, "+94771234567"));

		assert(netsvc_unwrap("{\"status\": \"timeout\"}", body, sizeof(body)) == -1);
	}

	puts("netsvc ok");
	return 0;
}
