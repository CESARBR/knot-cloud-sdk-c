/**
 * This file is part of the KNOT Project
 *
 * Copyright (c) 2019, CESAR. All rights reserved.
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 */

/**
 *  Message Queue source file
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdbool.h>
#include <sys/time.h>
#include <errno.h>
#include <ell/ell.h>
#include <amqp.h>
#include <amqp_framing.h>
#include <amqp_tcp_socket.h>

#include "mq.h"

#define AMQP_EXCHANGE_TYPE_DIRECT "direct"
#define AMQP_EXCHANGE_TYPE_FANOUT "fanout"

#define MQ_CONNECTION_CONSUME_TIMEOUT_US 10000
#define MQ_CONNECTION_CONNECT_TIMEOUT_SEC 10
#define MQ_CONNECTION_RETRY_TIMEOUT_MS 1000

#define MQ_NUM_OF_HEADERS 1

struct mq_context {
	amqp_connection_state_t conn;
	struct l_io *amqp_io;
	struct l_timeout *conn_retry_timeout;
	mq_connected_cb_t connected_cb;
	mq_disconnected_cb_t disconnected_cb;
	void *connection_data;
	mq_read_cb_t read_cb;
};

static struct mq_context mq_ctx;
static const int8_t num_of_headers = MQ_NUM_OF_HEADERS;
amqp_table_entry_t headers[MQ_NUM_OF_HEADERS];
amqp_bytes_t current_queue;

static void on_disconnect(struct l_io *io, void *user_data)
{
	l_debug("AMQP broker disconnected");

	if (mq_ctx.disconnected_cb)
		mq_ctx.disconnected_cb(mq_ctx.connection_data);

	if (mq_ctx.conn_retry_timeout)
		l_timeout_modify_ms(mq_ctx.conn_retry_timeout,
				    MQ_CONNECTION_RETRY_TIMEOUT_MS);
}

static const char *mq_server_exception_string(amqp_rpc_reply_t reply)
{
	amqp_connection_close_t *m = reply.reply.decoded;
	static char r[512];

	switch (reply.reply.id) {
	case AMQP_CONNECTION_CLOSE_METHOD:
		snprintf(r, sizeof(r),
			 "server connection error %uh, message: %.*s\n",
			 m->reply_code, (int)m->reply_text.len,
			 (char *)m->reply_text.bytes);
		break;
	case AMQP_CHANNEL_CLOSE_METHOD:
		snprintf(r, sizeof(r),
			 "server channel error %uh, message: %.*s\n",
			 m->reply_code, (int)m->reply_text.len,
			 (char *)m->reply_text.bytes);
		break;
	default:
		snprintf(r, sizeof(r),
			 "unknown server error, method id 0x%08X\n",
			 reply.reply.id);
		break;
	}

	return r;
}

static const char *mq_rpc_reply_string(amqp_rpc_reply_t reply)
{
	switch (reply.reply_type) {
	case AMQP_RESPONSE_NONE:
		return "missing RPC reply type!";
	case AMQP_RESPONSE_LIBRARY_EXCEPTION:
		return amqp_error_string2(reply.library_error);
	case AMQP_RESPONSE_SERVER_EXCEPTION:
		return mq_server_exception_string(reply);
	case AMQP_RESPONSE_NORMAL:
	default:
		return "";
	}
}

static void close_connection(void)
{
	amqp_rpc_reply_t r;
	int err;

	if (!mq_ctx.conn)
		return;

	r = amqp_channel_close(mq_ctx.conn, 1, AMQP_REPLY_SUCCESS);
	if (r.reply_type != AMQP_RESPONSE_NORMAL)
		l_error("amqp_channel_close: %s",
				mq_rpc_reply_string(r));

	r = amqp_connection_close(mq_ctx.conn, AMQP_REPLY_SUCCESS);
	if (r.reply_type != AMQP_RESPONSE_NORMAL)
		l_error("amqp_connection_close: %s",
				mq_rpc_reply_string(r));

	err = amqp_destroy_connection(mq_ctx.conn);
	if (err < 0)
		l_error("amqp_destroy_connection: %s",
				amqp_error_string2(err));

	mq_ctx.conn = NULL;
}

static void attempt_connection(struct l_timeout *ltimeout, void *user_data)
{
	const char *url = user_data;
	amqp_socket_t *socket;
	struct amqp_connection_info cinfo;
	char *tmp_url = l_strdup(url);
	amqp_rpc_reply_t r;
	struct timeval timeout = {.tv_sec = MQ_CONNECTION_CONNECT_TIMEOUT_SEC};
	int status;

	l_debug("Trying to connect to rabbitmq");

	/* Check and close if a connection is already up */
	close_connection();

	/* Check and destroy if an IO is already allocated */
	if (mq_ctx.amqp_io) {
		l_io_destroy(mq_ctx.amqp_io);
		mq_ctx.amqp_io = NULL;
	}

	// This function will change the url after processed
	status = amqp_parse_url(tmp_url, &cinfo);
	if (status) {
		l_error("amqp_parse_url: %s", amqp_error_string2(status));
		goto done;
	}

	mq_ctx.conn = amqp_new_connection();
	if (!mq_ctx.conn) {
		l_error("amqp_new_connection: Error on creation");
		goto done;
	}

	socket = amqp_tcp_socket_new(mq_ctx.conn);
	if (!socket) {
		l_error("error creating tcp socket");
		goto destroy_conn;
	}

	status = amqp_socket_open_noblock(socket, cinfo.host, cinfo.port,
					  &timeout);
	if (status < 0) {
		l_error("error opening socket: %s",
					amqp_error_string2(status));
		goto close_conn;
	}

	r = amqp_login(mq_ctx.conn, cinfo.vhost,
		       AMQP_DEFAULT_MAX_CHANNELS, AMQP_DEFAULT_FRAME_SIZE,
		       AMQP_DEFAULT_HEARTBEAT, AMQP_SASL_METHOD_PLAIN,
		       cinfo.user, cinfo.password);
	if (r.reply_type != AMQP_RESPONSE_NORMAL) {
		l_error("amqp_login(): %s", mq_rpc_reply_string(r));
		goto close_conn;
	}

	amqp_channel_open(mq_ctx.conn, 1);
	r = amqp_get_rpc_reply(mq_ctx.conn);
	if (r.reply_type != AMQP_RESPONSE_NORMAL) {
		l_error("amqp_channel_open(): %s",
			mq_rpc_reply_string(r));
		goto close_conn;
	}

	mq_ctx.amqp_io = l_io_new(amqp_get_sockfd(mq_ctx.conn));
	if (!mq_ctx.amqp_io)
		goto close_channel;

	status = l_io_set_disconnect_handler(mq_ctx.amqp_io, on_disconnect,
					     NULL, NULL);
	if (!status) {
		l_error("Error on set up disconnect handler");
		goto io_destroy;
	}

	if (mq_ctx.connected_cb)
		mq_ctx.connected_cb(mq_ctx.connection_data);

	goto done;

io_destroy:
	l_io_destroy(mq_ctx.amqp_io);
	mq_ctx.amqp_io = NULL;
close_channel:
	r = amqp_channel_close(mq_ctx.conn, 1, AMQP_REPLY_SUCCESS);
	if (r.reply_type != AMQP_RESPONSE_NORMAL)
		l_error("amqp_channel_close: %s",
				mq_rpc_reply_string(r));
close_conn:
	r = amqp_connection_close(mq_ctx.conn, AMQP_REPLY_SUCCESS);
	if (r.reply_type != AMQP_RESPONSE_NORMAL)
		l_error("amqp_connection_close: %s",
			mq_rpc_reply_string(r));
destroy_conn:
	status = amqp_destroy_connection(mq_ctx.conn);
	if (status < 0)
		l_error("status destroy: %s", amqp_error_string2(status));

	mq_ctx.conn = NULL;
	l_timeout_modify_ms(ltimeout, MQ_CONNECTION_RETRY_TIMEOUT_MS);
done:
	l_free(tmp_url);
}

static char *mq_bytes_to_new_string(amqp_bytes_t data)
{
	char *str = l_new(char, data.len + 1);

	memcpy(str, data.bytes, data.len);
	str[data.len] = '\0';
	return str;
}

/**
 * Callback function to consume message envelope from AMQP queue.
 *
 * Returns true on success or false if the read callback is not set.
 */
static bool on_receive(struct l_io *io, void *user_data)
{
	amqp_rpc_reply_t res;
	amqp_envelope_t envelope;
	char *exchange, *routing_key, *body;
	struct timeval time_out = {.tv_usec = MQ_CONNECTION_CONSUME_TIMEOUT_US};
	bool success;

	if (amqp_release_buffers_ok(mq_ctx.conn))
		amqp_release_buffers(mq_ctx.conn);

	res = amqp_consume_message(mq_ctx.conn, &envelope, &time_out, 0);

	if (res.reply_type != AMQP_RESPONSE_NORMAL)
		return true;

	l_debug("Receive %u -> exchange: %.*s, routingkey: %.*s\nBody: %.*s\n",
		(unsigned int)envelope.delivery_tag,
		(int)envelope.exchange.len,
		(char *)envelope.exchange.bytes,
		(int)envelope.routing_key.len,
		(char *)envelope.routing_key.bytes,
		(int)envelope.message.body.len,
		(char *)envelope.message.body.bytes);

	if (!mq_ctx.read_cb) {
		l_debug("AMQP read callback is not set");
		amqp_destroy_envelope(&envelope);
		return false;
	}

	exchange = mq_bytes_to_new_string(envelope.exchange);
	routing_key = mq_bytes_to_new_string(envelope.routing_key);
	body = mq_bytes_to_new_string(envelope.message.body);

	success = mq_ctx.read_cb(exchange, routing_key, body, user_data);
	if (!success)
		/* TODO: Add the msg on the queue again */
		l_debug("Message envelope not consumed");

	l_debug("Destroy received envelope");
	amqp_destroy_envelope(&envelope);
	l_free(exchange);
	l_free(routing_key);
	l_free(body);

	return true;
}

static int mq_prepare_queue(const char *exchange, const char *exchange_type,
		 const char *routing_key)
{
	if (exchange == NULL || exchange_type == NULL || routing_key == NULL)
		return -1;

	/* Declare the exchange as durable */
	amqp_exchange_declare(mq_ctx.conn, 1,
			amqp_cstring_bytes(exchange),
			amqp_cstring_bytes(exchange_type),
			0 /* passive*/,
			1 /* durable */,
			0 /* auto_delete*/,
			0 /* internal */,
			amqp_empty_table);

	/* Set up to bind a queue to an exchange */
	amqp_queue_bind(mq_ctx.conn, 1, current_queue,
			amqp_cstring_bytes(exchange),
			amqp_cstring_bytes(routing_key),
			amqp_empty_table);

	if (amqp_get_rpc_reply(mq_ctx.conn).reply_type !=
			       AMQP_RESPONSE_NORMAL) {
		l_error("Error while binding queue");
		return -1;
	}

	return 0;
}

static int mq_publish(const char *exchange,
			      const char *type,
			      const char *routing_key,
			      amqp_table_entry_t *headers,
			      size_t num_headers,
			      uint64_t expiration_ms,
			      amqp_bytes_t reply_to,
			      const char *correlation_id,
			      const char *body)
{
	amqp_basic_properties_t props;
	amqp_rpc_reply_t resp;
	amqp_bytes_t routing_key_bytes;
	char *expiration_str;
	int8_t rc; // Return Code

	/* Declare the exchange as durable */
	amqp_exchange_declare(mq_ctx.conn, 1,
			amqp_cstring_bytes(exchange),
			amqp_cstring_bytes(type),
			0 /* passive*/,
			1 /* durable */,
			0 /* auto_delete*/,
			0 /* internal */,
			amqp_empty_table);
	resp = amqp_get_rpc_reply(mq_ctx.conn);
	if (resp.reply_type != AMQP_RESPONSE_NORMAL) {
		l_error("amqp_exchange_declare(): %s",
			mq_rpc_reply_string(resp));
		return -1;
	}

	props._flags =	AMQP_BASIC_CONTENT_TYPE_FLAG	|
			AMQP_BASIC_DELIVERY_MODE_FLAG;

	if (reply_to.bytes) {
		if (correlation_id)
			props.correlation_id =
					amqp_cstring_bytes(correlation_id);
		else
			return -1;

		props._flags |= AMQP_BASIC_REPLY_TO_FLAG |
				AMQP_BASIC_CORRELATION_ID_FLAG;

		props.reply_to = amqp_bytes_malloc_dup(reply_to);
		if (!props.reply_to.bytes) {
			l_error("Out of memory while copying queue name");
			return -1;
		}
	} else {
		props.reply_to = amqp_empty_bytes;
	}

	if (expiration_ms) {
		props._flags |= AMQP_BASIC_EXPIRATION_FLAG;
		expiration_str = l_strdup_printf("%"PRIu64, expiration_ms);
		props.expiration = amqp_cstring_bytes(expiration_str);
	}

	if (num_headers > 0) {
		props._flags |= AMQP_BASIC_HEADERS_FLAG;
		props.headers.num_entries = num_headers;
		props.headers.entries = headers;
	}

	props.content_type = amqp_cstring_bytes("text/plain");
	props.delivery_mode = AMQP_DELIVERY_PERSISTENT;

	if (routing_key)
		routing_key_bytes = amqp_cstring_bytes(routing_key);
	else
		routing_key_bytes = amqp_empty_bytes;

	l_debug("Publish -> exchange: %s, routingkey: %s\nBody: %s\n",
		exchange,
		routing_key,
		body);

	rc = amqp_basic_publish(mq_ctx.conn, 1,
			amqp_cstring_bytes(exchange),
			routing_key_bytes,
			0 /* mandatory */,
			0 /* immediate */,
			&props, amqp_cstring_bytes(body));
	if (rc < 0)
		l_error("amqp_basic_publish(): %s",
			amqp_error_string2(rc));

	if (expiration_ms)
		l_free(expiration_str);

	if (props.reply_to.bytes)
		amqp_bytes_free(props.reply_to);

	return rc;
}

/**
 * mq_publish_message:
 * @message: message data to be published
 *
 * Publish a message with exchange type Direct with a
 * Remote Procedure Call (RPC) pattern to relate a message
 * sent to your reply.
 *
 * Returns: 0 if successful and negative integer otherwise.
 */
int8_t mq_publish_message(const mq_message_data_t *message) {
	int8_t res;
	switch (message->msg_type) {
		case MQ_MESSAGE_TYPE_DIRECT:
			res = mq_publish(message->exchange,
				AMQP_EXCHANGE_TYPE_DIRECT,
				message->routing_key, headers, num_of_headers,
				message->expiration_ms, amqp_empty_bytes, NULL,
				message->body);
			break;
		case MQ_MESSAGE_TYPE_DIRECT_RPC:
			{
				amqp_bytes_t reply_to = amqp_cstring_bytes(
					message->reply_to);
				res = mq_publish(message->exchange,
					AMQP_EXCHANGE_TYPE_DIRECT,
					message->routing_key, headers,
					num_of_headers, message->expiration_ms,
					reply_to, message->correlation_id,
					message->body);
			break;
			}
		case MQ_MESSAGE_TYPE_FANOUT:
			res = mq_publish(message->exchange,
				AMQP_EXCHANGE_TYPE_FANOUT, NULL, headers,
				num_of_headers, message->expiration_ms,
				amqp_empty_bytes, NULL, message->body);
			break;
		default:
			res = -1;
	}
	return res;
}

/**
 * mq_prepare_direct_queue:
 * @name: queue's name
 * @exchange: exchange to be declared
 * @routing_key: routing key to bind
 *
 * Declares a exchange and bind a routing key to a queue to be a consumer.
 *
 * Returns: 0 if successful and -1 otherwise.
 */
int mq_prepare_direct_queue(const char *exchange,
			    const char *routing_key)
{
	return mq_prepare_queue(exchange, AMQP_EXCHANGE_TYPE_DIRECT,
				 routing_key);
}

/**
 * mq_declare_new_queue:
 * @name: queue's name
 *
 * Declares a durable queue in amqp connection.
 *
 * Returns: the queue declared or NULL otherwise.
 */
int mq_declare_new_queue(const char *name)
{
	amqp_queue_declare_ok_t *r;

	if (!mq_ctx.conn) {
		current_queue.bytes = NULL;
		return -1;
	}

	r = amqp_queue_declare(mq_ctx.conn, 1,
			amqp_cstring_bytes(name),
			0, /* passive */
			1, /* durable */
			0, /* exclusive */
			0, /* auto-delete */
			amqp_empty_table);

	if (amqp_get_rpc_reply(mq_ctx.conn).reply_type !=
			       AMQP_RESPONSE_NORMAL) {
		l_error("Error declaring queue name");
		current_queue.bytes = NULL;
		return -1;
	}

	current_queue = amqp_bytes_malloc_dup(r->queue);
	if (current_queue.bytes == NULL)
		l_error("Out of memory while copying queue buffer");

	return 1;
}

/**
 * mq_delete_queue:
 *
 * Delete the current queue in amqp connection.
 *
 */
void mq_delete_queue(void)
{
	if (current_queue.bytes) {
		if (mq_ctx.conn) {
			amqp_queue_delete(mq_ctx.conn, 1, current_queue,
				0, 0);
			amqp_rpc_reply_t res = amqp_get_rpc_reply(mq_ctx.conn);
			if (res.reply_type != AMQP_RESPONSE_NORMAL) {
				l_error("Error deleting queue name");
			}
		}
		amqp_bytes_free(current_queue);
		current_queue = amqp_empty_bytes;
	}
}

/**
 * mq_consumer_queue:
 *
 * Start a queue consumer.
 *
 * Returns: 0 if successful and -1 otherwise.
 */
int mq_consumer_queue(void)
{
	amqp_basic_consume(mq_ctx.conn, 1,
			current_queue,
			amqp_empty_bytes,
			0, /* no_local */
			1, /* no_ack */
			0, /* exclusive */
			amqp_empty_table);

	if (amqp_get_rpc_reply(mq_ctx.conn).reply_type !=
							AMQP_RESPONSE_NORMAL) {
		l_error("Error while starting consumer");
		return -1;
	}

	return 0;
}

/**
 * mq_set_read_cb:
 * @read_cb: callback to be called when receive some amqp message
 * @user_data: user data provided to callback
 *
 * Set the callback to handle received messages from amqp connection.
 *
 * Returns: 0 if successful and -1 otherwise.
 */
int mq_set_read_cb(mq_read_cb_t read_cb, void *user_data)
{
	int err;

	mq_ctx.read_cb = read_cb;

	if (!mq_ctx.amqp_io) {
		l_error("Error amqp service not started");
		return -1;
	}

	err = l_io_set_read_handler(mq_ctx.amqp_io, on_receive,
				    user_data, NULL);
	if (!err) {
		l_io_destroy(mq_ctx.amqp_io);
		l_error("Error on set up read handler on AMQP io");
		return -1;
	}

	return 0;
}

int mq_start(char *url, mq_connected_cb_t connected_cb,
	     mq_disconnected_cb_t disconnected_cb, void *user_data,
		 const char *user_token)
{
	headers[0].key = amqp_cstring_bytes(MQ_AUTHORIZATION_HEADER);
	headers[0].value.kind = AMQP_FIELD_KIND_UTF8;
	headers[0].value.value.bytes = amqp_cstring_bytes(user_token);

	mq_ctx.connected_cb = connected_cb;
	mq_ctx.disconnected_cb = disconnected_cb;
	mq_ctx.connection_data = user_data;

	mq_ctx.conn_retry_timeout = l_timeout_create_ms(1, // start in oneshot
							attempt_connection,
							url, NULL);

	return 0;
}

void mq_stop(void)
{
	mq_delete_queue();
	l_timeout_remove(mq_ctx.conn_retry_timeout);
	mq_ctx.conn_retry_timeout = NULL;

	l_io_destroy(mq_ctx.amqp_io);
	mq_ctx.amqp_io = NULL;

	close_connection();
}
