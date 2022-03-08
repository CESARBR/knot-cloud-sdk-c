/*
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
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <ell/ell.h>
#include <json-c/json.h>
#include <amqp.h>

#include <knot/knot_protocol.h>

#include "mq.h"
#include "parser.h"
#include "log.h"
#include "knot_cloud.h"

knot_cloud_cb_t knot_cloud_cb;
char *user_auth_token;
char *knot_cloud_events[MSG_TYPES_LENGTH];

static void knot_cloud_device_free(void *data)
{
	struct knot_cloud_device *device = data;

	if (unlikely(!device))
		return;

	l_queue_destroy(device->config_list, l_free);
	l_free(device->id);
	l_free(device->uuid);
	l_free(device->name);
	l_free(device);
}

static void knot_cloud_msg_destroy(struct knot_cloud_msg *msg)
{
	if (msg->type == UPDATE_MSG || msg->type == REQUEST_MSG)
		l_queue_destroy(msg->list, l_free);
	else if (msg->type == LIST_MSG)
		l_queue_destroy(msg->list, knot_cloud_device_free);
	if (msg->token) {
		l_free(msg->token);
		msg->token = NULL;
	}
	l_free(msg);
}

static void *create_device_item(const char *id, const char *name,
				struct l_queue *config_list)
{
	struct knot_cloud_device *device;

	device = l_new(struct knot_cloud_device, 1);
	device->id = l_strdup(id);
	device->name = l_strdup(name);
	device->uuid = l_strdup(id);
	device->config_list = config_list;

	return device;
}

static int map_routing_key_to_msg_type(const char *routing_key)
{
	int msg_type;

	for (msg_type = UPDATE_MSG; msg_type < MSG_TYPES_LENGTH; msg_type++) {
		if (!strcmp(routing_key, knot_cloud_events[msg_type]))
			return msg_type;
	}

	return -1;
}

static struct knot_cloud_msg *create_msg(const char *routing_key,
					 const char *json_str)
{
	bool has_err;

	struct knot_cloud_msg *msg = l_new(struct knot_cloud_msg, 1);

	msg->type = map_routing_key_to_msg_type(routing_key);

	has_err = false;
	if (msg->type == LIST_MSG) {
		msg->device_id = NULL;
	} else {
		msg->device_id = parser_get_key_str_from_json_str(json_str,
			KNOT_JSON_FIELD_DEVICE_ID);
		has_err = msg->device_id ? false : true;
	}

	msg->error = parser_get_key_str_from_json_str(json_str,
			KNOT_JSON_FIELD_ERROR);

	if (msg->error)
		has_err = parser_is_key_str_or_null(json_str,
			KNOT_JSON_FIELD_ERROR) ? false : true;

	if (has_err) {
		l_error("Ill-formed JSON message");
		knot_cloud_msg_destroy(msg);
		return NULL;
	}

	switch (msg->type) {
	case UPDATE_MSG:
		msg->list = parser_update_to_list(json_str);
		has_err = msg->list ? false : true;
		break;
	case REQUEST_MSG:
		msg->list = parser_request_to_list(json_str);
		has_err = msg->list ? false : true;
		break;
	case REGISTER_MSG:
		msg->token = parser_get_key_str_from_json_str(json_str,
			KNOT_JSON_FIELD_DEVICE_TOKEN);
		has_err = msg->token ? false : true;
		break;
	case UNREGISTER_MSG:
		msg->id = parser_get_key_str_from_json_str(json_str,
			KNOT_JSON_FIELD_DEVICE_ID);
		has_err = msg->id ? false : true;
		break;
	case AUTH_MSG:
		break;
	case CONFIG_MSG:
		msg->list = parser_config_to_list(json_str);
		has_err = msg->list ? false : true;
		break;
	case LIST_MSG:
		msg->list = parser_queue_from_json_array(json_str,
				create_device_item);
		has_err = msg->list ? false : true;
		break;
	case MSG_TYPES_LENGTH:
	default:
		l_error("Unknown event %s", routing_key);
		has_err = true;
		break;
	}

	if (has_err) {
		l_error("Ill-formed JSON message");
		knot_cloud_msg_destroy(msg);
		return NULL;
	}

	return msg;
}

/**
 * Callback function to consume and parse the received message from AMQP queue
 * and call the respective handling callback function. In case of a error on
 * parse, the message is consumed, but not used.
 *
 * Returns true if the message envelope was consumed or returns false otherwise.
 */
static bool on_amqp_receive_message(const char *exchange,
				    const char *routing_key,
				    const char *body, void *user_data)
{
	struct knot_cloud_msg *msg;
	bool consumed = true;

	msg = create_msg(routing_key, body);
	if (msg) {
		consumed = knot_cloud_cb(msg, user_data);
		knot_cloud_msg_destroy(msg);
	}

	return consumed;
}

static int create_cloud_queue(const char *id)
{
	char queue_fog_name[100];
	int msg_type;
	int err;

	snprintf(queue_fog_name, sizeof(queue_fog_name), "%s-%s",
		 MQ_QUEUE_FOG_OUT, id);

	err = mq_declare_new_queue(queue_fog_name);
	if (err < 0) {
		l_error("Error on declare a new queue");
		return err;
	}

	for (msg_type = UPDATE_MSG; msg_type < MSG_TYPES_LENGTH; msg_type++) {
		err = mq_prepare_direct_queue(MQ_EXCHANGE_DEVICE,
					      knot_cloud_events[msg_type]);
		if (err) {
			l_error("Error on set up queue to consume");
			return -1;
		}
	}

	err = mq_consumer_queue();
	if (err) {
		l_error("Error on start a queue consumer");
		return -1;
	}

	return 0;
}

static void destroy_knot_cloud_events(void)
{
	int msg_type;

	for (msg_type = UPDATE_MSG; msg_type < MSG_TYPES_LENGTH; msg_type++) {
		if (knot_cloud_events[msg_type] != NULL) {
			l_free(knot_cloud_events[msg_type]);
			knot_cloud_events[msg_type] = NULL;
		}
	}
}

static int set_knot_cloud_events(const char *id)
{
	char binding_key_auth_reply[100];
	char binding_key_list_reply[100];
	char binding_key_update[100];
	char binding_key_request[100];

	snprintf(binding_key_auth_reply, sizeof(binding_key_auth_reply),
		 "%s-%s", MQ_EVENT_AUTH_REPLY, id);

	snprintf(binding_key_list_reply, sizeof(binding_key_list_reply),
		 "%s-%s", MQ_EVENT_LIST_REPLY, id);

	snprintf(binding_key_update, sizeof(binding_key_update), "%s.%s.%s",
		 MQ_EVENT_PREFIX_DEVICE, id, MQ_EVENT_POSTFIX_DATA_UPDATE);

	snprintf(binding_key_request, sizeof(binding_key_request), "%s.%s.%s",
		 MQ_EVENT_PREFIX_DEVICE, id, MQ_EVENT_POSTFIX_DATA_REQUEST);

	/* Free knot_cloud_events if already allocated */
	destroy_knot_cloud_events();

	knot_cloud_events[UPDATE_MSG] =
				l_strdup(binding_key_update);
	knot_cloud_events[REQUEST_MSG] =
				l_strdup(binding_key_request);
	knot_cloud_events[REGISTER_MSG] =
				l_strdup(MQ_EVENT_DEVICE_REGISTERED);
	knot_cloud_events[UNREGISTER_MSG] =
				l_strdup(MQ_EVENT_DEVICE_UNREGISTERED);
	knot_cloud_events[AUTH_MSG] =
				l_strdup(binding_key_auth_reply);
	knot_cloud_events[CONFIG_MSG] =
				l_strdup(MQ_EVENT_DEVICE_CONFIG_UPDATED);
	knot_cloud_events[LIST_MSG] =
				l_strdup(binding_key_list_reply);

	return 0;
}

/**
 * knot_cloud_set_log_priority:
 * @priority: Log Priority
 *
 * Changes the knot cloud SDK log priority.
 *
 * Returns: void.
 */
int knot_cloud_set_log_priority(int priority)
{
	switch (priority) {
	case CLOUD_LOG_PRIORITY_ERROR:
		log_set_priority(L_LOG_ERR);
		break;
	case CLOUD_LOG_PRIORITY_WARN:
		log_set_priority(L_LOG_WARNING);
		break;
	case CLOUD_LOG_PRIORITY_INFO:
		log_set_priority(L_LOG_INFO);
		break;
	case CLOUD_LOG_PRIORITY_DEBUG:
		log_set_priority(L_LOG_DEBUG);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

/**
 * knot_cloud_register_device:
 * @id: device id
 * @name: device name
 *
 * Requests cloud to add a device.
 * The confirmation that the cloud received the message comes from a callback
 * set in function knot_cloud_read_start with message type REGISTER_MSG.
 *
 * Returns: 0 if successful and a KNoT error otherwise.
 */
int knot_cloud_register_device(const char *id, const char *name)
{
	char *json_str;
	int result;

	json_str = parser_device_json_create(id, name);
	if (!json_str)
		return KNOT_ERR_CLOUD_FAILURE;

	/**
	 * Exchange
	 *	Type: Direct
	 *	Name: device
	 * Routing Key
	 *	Name: device.register
	 * Headers
	 *	[0]: User Token
	 * Expiration
	 *	2000 ms
	 */
	mq_message_data_t mq_message = {
		MQ_MESSAGE_TYPE_DIRECT, MQ_EXCHANGE_DEVICE,
		MQ_CMD_DEVICE_REGISTER, MQ_MSG_EXPIRATION_TIME_MS, json_str,
		NULL, NULL
	};

	result = mq_publish_message(&mq_message);
	if (result < 0)
		result = KNOT_ERR_CLOUD_FAILURE;

	l_free(json_str);

	return result;
}

/**
 * knot_cloud_unregister_device:
 * @id: device id
 *
 * Requests cloud to remove a device.
 * The confirmation that the cloud received the message comes from a callback
 * set in function knot_cloud_read_start  with message type UNREGISTER_MSG.
 *
 * Returns: 0 if successful and a KNoT error otherwise.
 */
int knot_cloud_unregister_device(const char *id)
{
	char *json_str;
	int result;

	json_str = parser_unregister_json_create(id);
	if (!json_str)
		return KNOT_ERR_CLOUD_FAILURE;

	/**
	 * Exchange
	 *	Type: Direct
	 *	Name: device
	 * Routing Key
	 *	Name: device.unregister
	 * Headers
	 *	[0]: User Token
	 * Expiration
	 *	2000 ms
	 */
	mq_message_data_t mq_message = {
		MQ_MESSAGE_TYPE_DIRECT, MQ_EXCHANGE_DEVICE,
		MQ_CMD_DEVICE_UNREGISTER, MQ_MSG_EXPIRATION_TIME_MS, json_str,
		NULL, NULL
	};

	result = mq_publish_message(&mq_message);
	if (result < 0)
		return KNOT_ERR_CLOUD_FAILURE;

	l_free(json_str);

	return 0;
}

/**
 * knot_cloud_auth_device:
 * @id: device id
 * @token: device token
 *
 * Requests cloud to auth a device.
 * The confirmation that the cloud received the message comes from a callback
 * set in function knot_cloud_read_start.
 *
 * Returns: 0 if successful and a KNoT error otherwise.
 */
int knot_cloud_auth_device(const char *id, const char *token)
{
	char *json_str;
	int result;

	json_str = parser_auth_json_create(id, token);
	if (!json_str)
		return KNOT_ERR_CLOUD_FAILURE;

	/**
	 * Exchange
	 *	Type: Direct
	 *	Name: device
	 * Routing Key
	 *	Name: device.auth
	 * Headers
	 *	[0]: User Token
	 * Expiration
	 *	2000 ms
	 */
	mq_message_data_t mq_message = {
		MQ_MESSAGE_TYPE_DIRECT_RPC, MQ_EXCHANGE_DEVICE,
		MQ_CMD_DEVICE_AUTH, MQ_MSG_EXPIRATION_TIME_MS, json_str,
		knot_cloud_events[AUTH_MSG], MQ_DEFAULT_CORRELATION_ID
	 };
	result = mq_publish_message(&mq_message);
	if (result < 0)
		result = KNOT_ERR_CLOUD_FAILURE;

	l_free(json_str);

	return result;
}

/**
 * knot_cloud_update_config:
 *
 * Requests cloud to update the device config.
 * The confirmation that the cloud received the message comes from a callback
 * set in function knot_cloud_read_start with message type CONFIG_MSG.
 *
 * Returns: 0 if successful and a KNoT error otherwise.
 */
int knot_cloud_update_config(const char *id, struct l_queue *config_list)
{
	char *json_str;
	int result;

	json_str = parser_config_create_object(id, config_list);
	if (!json_str)
		return KNOT_ERR_CLOUD_FAILURE;

	/**
	 * Exchange
	 *	Type: Direct
	 *	Name: device
	 * Routing Key
	 *	Name: device.config.sent
	 * Headers
	 *	[0]: User Token
	 * Expiration
	 *	2000 ms
	 */
	mq_message_data_t mq_message = {
		MQ_MESSAGE_TYPE_DIRECT,
		MQ_EXCHANGE_DEVICE, MQ_CMD_CONFIG_SENT,
		MQ_MSG_EXPIRATION_TIME_MS, json_str,
		NULL, NULL
	};

	result = mq_publish_message(&mq_message);
	if (result < 0)
		result = KNOT_ERR_CLOUD_FAILURE;

	l_free(json_str);

	return result;
}

/**
 * knot_cloud_list_devices:
 *
 * Sends a request to cloud list all devices
 *
 * Returns: 0 if successful and a KNoT error otherwise.
 */
int knot_cloud_list_devices(void)
{
	json_object *jobj_empty;
	const char *json_str;
	int result;

	jobj_empty = json_object_new_object();
	json_str = json_object_to_json_string(jobj_empty);

	/**
	 * Exchange
	 *	Type: Direct
	 *	Name: device
	 * Routing Key
	 *	Name: device.list
	 * Headers
	 *	[0]: User Token
	 * Expiration
	 *	2000 ms
	 */
	mq_message_data_t mq_message = {
		MQ_MESSAGE_TYPE_DIRECT_RPC, MQ_EXCHANGE_DEVICE,
		MQ_CMD_DEVICE_LIST, MQ_MSG_EXPIRATION_TIME_MS, json_str,
		knot_cloud_events[LIST_MSG], MQ_DEFAULT_CORRELATION_ID
	};

	result = mq_publish_message(&mq_message);
	if (result < 0)
		result = KNOT_ERR_CLOUD_FAILURE;

	json_object_put(jobj_empty);

	return result;
}

/**
 * knot_cloud_publish_data:
 * @id: device id
 * @sensor_id: schema sensor id
 * @value_type: schema value type defined in KNoT protocol
 * @value: value to be sent
 * @kval_len: length of @value
 *
 * Sends device's data to cloud.
 *
 * Returns: 0 if successful and a KNoT error otherwise.
 */
int knot_cloud_publish_data(const char *id, uint8_t sensor_id,
			    uint8_t value_type, const knot_value_type *value,
			    uint8_t kval_len, struct tm *ptm)
{
	char *json_str;
	int result;

	json_str = parser_data_create_object(id, sensor_id, value_type, value,
					      kval_len, ptm);
	if (!json_str)
		return KNOT_ERR_CLOUD_FAILURE;

	/**
	 * Exchange
	 *	Type: Fanout
	 *	Name: data.sent
	 * Expiration
	 *	2000 ms
	 */
	mq_message_data_t mq_message = {
		MQ_MESSAGE_TYPE_FANOUT, MQ_EXCHANGE_DATA_SENT,
		NULL, MQ_MSG_EXPIRATION_TIME_MS, json_str,
		NULL, NULL
	};

	result = mq_publish_message(&mq_message);
	if (result < 0)
		result = KNOT_ERR_CLOUD_FAILURE;

	l_free(json_str);

	return result;
}

/**
 * knot_cloud_read_start:
 * @id: thing id
 * @read_handler_cb: callback to handle message received from cloud
 * @user_data: user data provided to callbacks
 *
 * Start Cloud to receive messages on read_handler_cb function.
 *
 * Returns: 0 if successful and -1 otherwise.
 */
int knot_cloud_read_start(const char *id, knot_cloud_cb_t read_handler_cb,
			  void *user_data)
{
	knot_cloud_cb = read_handler_cb;

	/* Delete queues if already declared */
	mq_delete_queue();

	if (set_knot_cloud_events(id))
		return -1;

	if (create_cloud_queue(id))
		return -1;

	if (mq_set_read_cb(on_amqp_receive_message, user_data)) {
		l_error("Error on set up read callback");
		return -1;
	}

	return 0;
}

int knot_cloud_start(char *url, char *user_token,
		     knot_cloud_connected_cb_t connected_cb,
		     knot_cloud_disconnected_cb_t disconnected_cb,
		     void *user_data)
{
	log_ell_enable();
	user_auth_token = l_strdup(user_token);
	return mq_start(url, connected_cb, disconnected_cb, user_data,
				 user_auth_token);
}

void knot_cloud_stop(void)
{
	destroy_knot_cloud_events();
	mq_stop();
}
