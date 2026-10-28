// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 perz3us
#include <libubox/blobmsg.h>
#include <libubus.h>
#include <poll.h>
#include <string.h>
#include "urc.h"

static const struct blobmsg_policy policy[] = {
	{ .name = "raw_line", .type = BLOBMSG_TYPE_STRING },
};

static struct ubus_context *ctx;
static struct ubus_event_handler handler;
static int lost, released;

static void on_event(struct ubus_context *c, struct ubus_event_handler *h,
		     const char *type, struct blob_attr *msg)
{
	struct blob_attr *tb[1];

	(void)c;
	(void)h;
	(void)type;
	blobmsg_parse(policy, 1, tb, blob_data(msg), blob_len(msg));
	if (tb[0] && !strncmp(blobmsg_get_string(tb[0]), "NO CARRIER", 10))
		released = 1;
}

static void on_lost(struct ubus_context *c)
{
	(void)c;
	lost = 1;
}

int urc_start(void)
{
	if (ctx && !lost)
		return 0;
	urc_stop();
	if (!(ctx = ubus_connect(NULL)))
		return -1;
	ctx->connection_lost = on_lost;
	handler.cb = on_event;
	if (ubus_register_event_handler(ctx, &handler, "qmodem.at.line")) {
		urc_stop();
		return -1;
	}
	return 0;
}

void urc_stop(void)
{
	if (ctx)
		ubus_free(ctx);
	ctx = NULL;
	lost = 0;
}

int urc_released(void)
{
	struct pollfd p;
	int r;

	if (ctx && !lost) {
		p.fd = ctx->sock.fd;
		p.events = POLLIN;
		while (!lost && poll(&p, 1, 0) > 0 && (p.revents & POLLIN))
			ubus_handle_event(ctx);
		if (p.revents & (POLLHUP | POLLERR))
			lost = 1;
	}
	r = released;
	released = 0;
	return r;
}
