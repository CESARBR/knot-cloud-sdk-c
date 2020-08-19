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
 *  Message Queue header file
 */

#define MQ_QUEUE_FOG_OUT "thingd-fogOut"

/* Exchanges */
#define MQ_EXCHANGE_DEVICE "device"
#define MQ_EXCHANGE_DATA_SENT "data.sent"

/* Headers */
#define MQ_AUTHORIZATION_HEADER "Authorization"

#define MQ_MSG_EXPIRATION_TIME_MS 2000

 /* Southbound traffic (commands) */
#define MQ_EVENT_PREFIX_DEVICE "device"
#define MQ_EVENT_POSTFIX_DATA_UPDATE "data.update"
#define MQ_EVENT_POSTFIX_DATA_REQUEST "data.request"

#define MQ_EVENT_DEVICE_REGISTERED "device.registered"
#define MQ_EVENT_DEVICE_UNREGISTERED "device.unregistered"
#define MQ_EVENT_DEVICE_SCHEMA_UPDATED "device.schema.updated"

#define MQ_EVENT_AUTH_REPLY "thingd-auth-reply"
#define MQ_EVENT_LIST_REPLY "thingd-list-reply"

 /* Northbound traffic (control, measurements) */
#define MQ_CMD_DEVICE_REGISTER "device.register"
#define MQ_CMD_DEVICE_UNREGISTER "device.unregister"
#define MQ_CMD_DEVICE_AUTH "device.auth"
#define MQ_CMD_SCHEMA_SENT "device.schema.sent"
#define MQ_CMD_DEVICE_LIST "device.list"

#define MQ_DEFAULT_CORRELATION_ID "default-corrId"

/**
 * @brief Defines the type of message.
 *
 * This enum defines the mq's message type.
 */
typedef enum {
	MQ_MESSAGE_TYPE_DIRECT = 1,
	MQ_MESSAGE_TYPE_DIRECT_RPC,
	MQ_MESSAGE_TYPE_FANOUT
} mq_message_type;

/**
 * @brief Defines a mq message format.
 *
 * A mq message to be exchange under amqp's protocol. This struct helds
 * authorization parameters, a header, an expiration time for the message and
 * the message's body.
 */
typedef struct {
	mq_message_type msg_type;
	const char *exchange;
	const char *routing_key;
	uint64_t expiration_ms;
	const char *body;
	const char *reply_to;
	const char *correlation_id;
} mq_message_data_t;

typedef bool (*mq_read_cb_t) (const char *exchange, const char *routing_key,
			      const char *body, void *user_data);
typedef void (*mq_connected_cb_t) (void *user_data);
typedef void (*mq_disconnected_cb_t) (void *user_data);

int8_t mq_publish_message(const mq_message_data_t *message);
int mq_prepare_direct_queue(amqp_bytes_t queue, const char *exchange,
			    const char *routing_key);
amqp_bytes_t mq_declare_new_queue(const char *name);
int mq_delete_queue(amqp_bytes_t queue);
int mq_consumer_queue(amqp_bytes_t queue);

int mq_set_read_cb(mq_read_cb_t read_cb, void *user_data);

int mq_start(char *url, mq_connected_cb_t connected_cb,
	     mq_disconnected_cb_t disconnected_cb, void *user_data,
		 const char *user_token);
void mq_stop(void);
