// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 perz3us
#include <libubox/blobmsg.h>
#include <libubus.h>
#include <poll.h>
#include <string.h>
#include "voip.h"

#define CALL_TIMEOUT_MS 4000

enum { F_STATE, F_ORIGIN, F_OWNER, F_NUMBER, F_ENABLED, F_MAX };

static const struct blobmsg_policy policy[F_MAX] = {
	[F_STATE] = { .name = "state", .type = BLOBMSG_TYPE_STRING },
	[F_ORIGIN] = { .name = "origin", .type = BLOBMSG_TYPE_STRING },
	[F_OWNER] = { .name = "answer_owner", .type = BLOBMSG_TYPE_STRING },
	[F_NUMBER] = { .name = "remote_number", .type = BLOBMSG_TYPE_STRING },
	[F_ENABLED] = { .name = "enabled", .type = BLOBMSG_TYPE_BOOL },
};

static struct ubus_context *ev_ctx, *req_ctx;
static struct ubus_event_handler handler;
static struct blob_buf buf;
static struct voip_snap snap;
static uint32_t object;
static int lost, changed;

static const char *str(struct blob_attr *a)
{
	return a ? blobmsg_get_string(a) : NULL;
}

static void take(struct blob_attr *msg)
{
	struct blob_attr *tb[F_MAX];
	struct voip_snap s;

	if (!msg)
		return;
	blobmsg_parse(policy, F_MAX, tb, blob_data(msg), blob_len(msg));
	if (!tb[F_STATE])
		return;
	memset(&s, 0, sizeof(s));
	voip_snap_set(&s, str(tb[F_STATE]), str(tb[F_ORIGIN]), str(tb[F_OWNER]), str(tb[F_NUMBER]),
		      tb[F_ENABLED] ? blobmsg_get_bool(tb[F_ENABLED]) : 1);
	if (memcmp(&s, &snap, sizeof(s))) {
		snap = s;
		changed = 1;
	}
}

static void on_event(struct ubus_context *ctx, struct ubus_event_handler *h,
		     const char *type, struct blob_attr *msg)
{
	(void)ctx;
	(void)h;
	(void)type;
	take(msg);
}

static void on_reply(struct ubus_request *req, int type, struct blob_attr *msg)
{
	(void)req;
	(void)type;
	take(msg);
}

static void on_lost(struct ubus_context *ctx)
{
	(void)ctx;
	lost = 1;
}

int voip_connect(void)
{
	if (lost)
		voip_disconnect();
	if (!ev_ctx) {
		if (!(ev_ctx = ubus_connect(NULL)))
			return -1;
		ev_ctx->connection_lost = on_lost;
		handler.cb = on_event;
		if (ubus_register_event_handler(ev_ctx, &handler, "qmodem_voip.call")) {
			voip_disconnect();
			return -1;
		}
	}
	if (!req_ctx) {
		if (!(req_ctx = ubus_connect(NULL)))
			return -1;
		req_ctx->connection_lost = on_lost;
	}
	if (ubus_lookup_id(req_ctx, "qmodem_voip", &object))
		return -1;
	return voip_refresh();
}

void voip_disconnect(void)
{
	if (ev_ctx)
		ubus_free(ev_ctx);
	if (req_ctx)
		ubus_free(req_ctx);
	ev_ctx = req_ctx = NULL;
	object = 0;
	lost = 0;
	memset(&snap, 0, sizeof(snap));
	changed = 1;
}

int voip_connected(void)
{
	return ev_ctx && req_ctx && object && !lost;
}

void voip_events(void)
{
	struct pollfd p;

	if (!ev_ctx || lost)
		return;
	p.fd = ev_ctx->sock.fd;
	p.events = POLLIN;
	while (!lost && poll(&p, 1, 0) > 0 && (p.revents & POLLIN))
		ubus_handle_event(ev_ctx);
	if (p.revents & (POLLHUP | POLLERR))
		lost = 1;
}

static int invoke(const char *method)
{
	int e;

	if (!req_ctx || !object || lost)
		return -1;
	e = ubus_invoke(req_ctx, object, method, buf.head, on_reply, NULL, CALL_TIMEOUT_MS);
	if (e == UBUS_STATUS_NOT_FOUND || e == UBUS_STATUS_CONNECTION_FAILED)
		object = 0;
	return e ? -1 : 0;
}

int voip_refresh(void)
{
	blob_buf_init(&buf, 0);
	return invoke("status");
}

int voip_request(const char *method, const char *number)
{
	blob_buf_init(&buf, 0);
	blobmsg_add_string(&buf, "endpoint", "rj11");
	if (number)
		blobmsg_add_string(&buf, "number", number);
	return invoke(method);
}

const struct voip_snap *voip_snap(void)
{
	return &snap;
}

int voip_changed(void)
{
	int c = changed;

	changed = 0;
	return c;
}
