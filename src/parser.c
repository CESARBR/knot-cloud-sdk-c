/*
 * This file is part of the KNOT Project
 *
 * Copyright (c) 2018, CESAR. All rights reserved.
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <stdio.h>
#include <time.h>
#include <string.h>

#include <ell/ell.h>

#include <knot/knot_types.h>
#include <knot/knot_protocol.h>

#include <json-c/json.h>

#include "parser.h"

#define MIN(x, y) ((x) < (y) ? (x) : (y))

/*
 * TODO: consider moving this to knot-protocol
 */
static uint64_t knot_value_as_uint64(const knot_value_type *data)
{
	return data->val_u64;
}

/*
 * TODO: consider moving this to knot-protocol
 */
static uint32_t knot_value_as_uint(const knot_value_type *data)
{
	return data->val_u;
}

/*
 * TODO: consider moving this to knot-protocol
 */
static int64_t knot_value_as_int64(const knot_value_type *data)
{
	return data->val_i64;
}

static char *knot_value_as_raw(const knot_value_type *data,
			       uint8_t kval_len, size_t *encoded_len)
{
	char *encoded;
	size_t olen;

	encoded = l_base64_encode(data->raw, kval_len, 0, &olen);
	if (!encoded)
		return NULL;

	*encoded_len = olen;

	return encoded;
}

/*
 * TODO: consider moving this to knot-protocol
 */
static bool knot_value_as_boolean(const knot_value_type *data)
{
	return data->val_b;
}

/*
 * TODO: consider moving this to knot-protocol
 */
static double knot_value_as_double(const knot_value_type *data)
{
	return (double) data->val_f;
}

/*
 * TODO: consider moving this to knot-protocol
 */
static int knot_value_as_int(const knot_value_type *data)
{
	return data->val_i;
}

static const char *get_str_value_from_json(json_object *jso, const char *key)
{
	const char *str_value;
	json_object *jobjkey;

	if (!json_object_object_get_ex(jso, key, &jobjkey))
		return NULL;

	if (json_object_get_type(jobjkey) != json_type_string)
		return NULL;

	str_value = json_object_get_string(jobjkey);

	return str_value;
}

static void *device_array_item(json_object *array_item,
		create_device_item_cb cb)
{
	json_object *jobjkey;
	struct l_queue *config_list;
	const char *id, *name;

	/* Getting 'Id': Mandatory field for registered device */
	id = get_str_value_from_json(array_item, KNOT_JSON_FIELD_DEVICE_ID);
	if (!id)
		return NULL;

	/* Getting 'config': Mandatory field for registered device */
	if (!json_object_object_get_ex(array_item,
				       KNOT_JSON_FIELD_CONFIG, &jobjkey))
		return NULL;

	config_list = parser_config_to_list(
					json_object_to_json_string(jobjkey));
	if (!config_list)
		return NULL;

	/* Getting 'Name' */
	name = get_str_value_from_json(array_item,
		KNOT_JSON_FIELD_DEVICE_NAME);
	if (!name)
		return NULL;

	return cb(id, name, config_list);
}

/*
 * Parsing knot_value_type attribute
 */
static int parse_json2data(json_object *jobj, knot_value_type *kvalue)
{
	json_object *jobjkey;
	const char *str;
	uint8_t *u8val;
	size_t olen = 0;

	jobjkey = jobj;
	switch (json_object_get_type(jobjkey)) {
	case json_type_boolean:
		kvalue->val_b = json_object_get_boolean(jobjkey);
		olen = sizeof(kvalue->val_b);
		break;
	case json_type_double:
		/* FIXME: how to handle overflow? */
		kvalue->val_f = (float) json_object_get_double(jobjkey);
		olen = sizeof(kvalue->val_f);
		break;
	case json_type_int:
		kvalue->val_i = json_object_get_int(jobjkey);
		olen = sizeof(kvalue->val_i);
		break;
	case json_type_string:
		str = json_object_get_string(jobjkey);
		u8val = l_base64_decode(str, strlen(str), &olen);
		if (!u8val)
			break;

		if (olen > KNOT_DATA_RAW_SIZE)
			olen = KNOT_DATA_RAW_SIZE; /* truncate */

		memcpy(kvalue->raw, u8val, olen);
		l_free(u8val);
		break;
	/* FIXME: not implemented */
	case json_type_null:
	case json_type_object:
	case json_type_array:
	default:
		break;
	}

	return olen;
}

static json_object *parse_value_to_json(uint8_t value_type,
					knot_value_type *value)
{
	switch (value_type)
	{
	case KNOT_VALUE_TYPE_INT:
		return json_object_new_int(value->val_i);
	case KNOT_VALUE_TYPE_FLOAT:
		return json_object_new_double(value->val_f);
	case KNOT_VALUE_TYPE_BOOL:
		return json_object_new_boolean(value->val_b);
	case KNOT_VALUE_TYPE_RAW:
		return json_object_new_string(value->raw);
	case KNOT_VALUE_TYPE_INT64:
		return json_object_new_int64(value->val_i64);
	case KNOT_VALUE_TYPE_UINT:
		return json_object_new_uint64(value->val_u64);
	case KNOT_VALUE_TYPE_UINT64:
		return json_object_new_uint64(value->val_u64);
	default:
		break;
	}
}

static json_object *event_item_create_obj(knot_msg_config *config)
{
	json_object *json_event;

	json_event = json_object_new_object();

	if (config->event.event_flags & KNOT_EVT_FLAG_CHANGE)
		json_object_object_add(json_event, KNOT_JSON_FIELD_CHANGE,
				       json_object_new_boolean(true));

	json_object_object_add(json_event, KNOT_JSON_FIELD_TIME_SEC,
			       json_object_new_int(config->event.time_sec));

	if (config->event.event_flags & KNOT_EVT_FLAG_LOWER_THRESHOLD)
		json_object_object_add(json_event,
				KNOT_JSON_FIELD_LOWER_THRESHOLD,
				parse_value_to_json(config->schema.value_type,
						&config->event.lower_limit));

	if (config->event.event_flags & KNOT_EVT_FLAG_UPPER_THRESHOLD)
		json_object_object_add(json_event,
				KNOT_JSON_FIELD_UPPER_THRESHOLD,
				parse_value_to_json(config->schema.value_type,
						&config->event.upper_limit));

	/*
	 * Returned JSON object is in the following format:
	 *
	 * {
	 *   "change": true,
	 *   "timeSec": 10,
	 *   "lowerThreshold": 1000,
	 *   "upperThreshold": 3000
	 * }
	 *
	 */

	return json_event;
}

static json_object *schema_item_create_obj(knot_msg_config *config)
{
	json_object *json_schema;

	json_schema = json_object_new_object();

	json_object_object_add(json_schema, KNOT_JSON_FIELD_VALUE_TYPE,
				json_object_new_int(
					config->schema.value_type));
	json_object_object_add(json_schema, KNOT_JSON_FIELD_UNIT,
				json_object_new_int(
					config->schema.unit));
	json_object_object_add(json_schema, KNOT_JSON_FIELD_TYPE_ID,
				json_object_new_int(
					config->schema.type_id));
	json_object_object_add(json_schema, KNOT_JSON_FIELD_DEVICE_NAME,
				json_object_new_string(
					config->schema.name));

	/*
	 * Returned JSON object is in the following format:
	 *
	 * {
	 *   "typeId": 0xFFF1,
	 *   "unit": 0,
	 *   "valueType": 3,
	 *   "name": "Door lock"
	 * }
	 *
	 */

	return json_schema;
}

static void config_item_create_and_append(void *data, void *user_data)
{
	knot_msg_config *config = data;
	json_object *config_array = user_data;
	json_object *schema, *event, *config_obj;

	config_obj = json_object_new_object();

	json_object_object_add(config_obj, KNOT_JSON_FIELD_SENSOR_ID,
			       json_object_new_int(config->sensor_id));

	schema = schema_item_create_obj(config);
	json_object_object_add(config_obj, KNOT_JSON_FIELD_SCHEMA, schema);

	event = event_item_create_obj(config);
	json_object_object_add(config_obj, KNOT_JSON_FIELD_EVENT, event);

	json_object_array_add(config_array, config_obj);
}

static int get_event(knot_event *event, json_object *data)
{
	json_object *jobjkey, *jobj_event;
	knot_value_type lower_threshold, upper_threshold;
	int sensor_id, time_sec;
	uint8_t evt_flag;
	bool change;

	if (!json_object_object_get_ex(data, KNOT_JSON_FIELD_EVENT,
				       &jobj_event))
		return -1;

	evt_flag = KNOT_EVT_FLAG_NONE;

	/* Getting 'change' */
	if (!json_object_object_get_ex(jobj_event, KNOT_JSON_FIELD_CHANGE,
				       &jobjkey))
		return -1;

	if (json_object_get_type(jobjkey) != json_type_boolean)
		return -1;

	change = json_object_get_boolean(jobjkey);

	if (change)
		evt_flag |= KNOT_EVT_FLAG_CHANGE;

	/* Getting 'timeSec' */
	if (json_object_object_get_ex(jobj_event, KNOT_JSON_FIELD_TIME_SEC,
				      &jobjkey)) {

		if (json_object_get_type(jobjkey) != json_type_int)
			return -1;

		time_sec = json_object_get_int(jobjkey);

		evt_flag |= KNOT_EVT_FLAG_TIME;
	}

	/* Getting 'lowerThreshold' */
	if (json_object_object_get_ex(jobj_event,
				      KNOT_JSON_FIELD_LOWER_THRESHOLD,
				      &jobjkey)) {
		if (!parse_json2data(jobjkey, &lower_threshold))
			return -1;

		evt_flag |= KNOT_EVT_FLAG_LOWER_THRESHOLD;
	}

	/* Getting 'upperThreshold' */
	if (json_object_object_get_ex(jobj_event,
				      KNOT_JSON_FIELD_UPPER_THRESHOLD,
				      &jobjkey)) {
		if (!parse_json2data(jobjkey, &upper_threshold))
			return -1;

		evt_flag |= KNOT_EVT_FLAG_UPPER_THRESHOLD;
	}

	event->time_sec = time_sec;
	event->lower_limit = lower_threshold;
	event->upper_limit = upper_threshold;
	event->event_flags = evt_flag;

	return 0;
}

static int get_schema(knot_schema *schema, json_object *data)
{
	json_object *jobjkey, *jobj_schema;
	int sensor_id, value_type, unit, type_id;
	const char *name;

	if (!json_object_object_get_ex(data, KNOT_JSON_FIELD_SCHEMA,
				       &jobj_schema))
		return -1;

	/* Getting 'valueType' */
	if (!json_object_object_get_ex(jobj_schema, KNOT_JSON_FIELD_VALUE_TYPE,
				       &jobjkey))
		return -1;

	if (json_object_get_type(jobjkey) != json_type_int)
		return -1;

	value_type = json_object_get_int(jobjkey);

	/* Getting 'unit' */
	if (!json_object_object_get_ex(jobj_schema, KNOT_JSON_FIELD_UNIT,
				       &jobjkey))
		return -1;

	if (json_object_get_type(jobjkey) != json_type_int)
		return -1;

	unit = json_object_get_int(jobjkey);

	/* Getting 'typeId' */
	if (!json_object_object_get_ex(jobj_schema, KNOT_JSON_FIELD_TYPE_ID,
				       &jobjkey))
		return -1;

	if (json_object_get_type(jobjkey) != json_type_int)
		return -1;

	type_id = json_object_get_int(jobjkey);

	/* Getting 'name' */
	if (!json_object_object_get_ex(jobj_schema, KNOT_JSON_FIELD_DEVICE_NAME,
				       &jobjkey))
		return -1;

	if (json_object_get_type(jobjkey) != json_type_string)
		return -1;

	name = json_object_get_string(jobjkey);

	schema->value_type = value_type;
	schema->unit = unit;
	schema->type_id = type_id;
	strncpy(schema->name, name, sizeof(schema->name) - 1);

	return 0;
}

static int get_sensor_id(uint8_t *sensor_id, json_object *data)
{
	json_object *jobjkey;

	if (!json_object_object_get_ex(data, KNOT_JSON_FIELD_SENSOR_ID,
				       &jobjkey))
		return -1;

	if (json_object_get_type(jobjkey) != json_type_int)
		return -1;

	*sensor_id = json_object_get_int(jobjkey);

	return 0;
}

char *parser_config_create_object(const char *device_id,
				  struct l_queue *config_list)
{
	char *json_str;
	json_object *json_msg;
	json_object *json_array;

	json_msg = json_object_new_object();

	json_object_object_add(json_msg, KNOT_JSON_FIELD_DEVICE_ID,
			       json_object_new_string(device_id));

	json_array = json_object_new_array();
	l_queue_foreach(config_list, config_item_create_and_append,
			json_array);

	json_object_object_add(json_msg, KNOT_JSON_FIELD_CONFIG, json_array);

	/*
	 * Returned JSON object is in the following format:
	 *
	 * {
	 *   "id": "fbe64efa6c7f717e",
	 *   "config" : [{
	 *         "sensorId": 1,
	 *         "schema": {
	 *               "typeId": 0xFFF1,
	 *               "unit": 0,
	 *               "valueType": 3,
	 *               "name": "Door lock"
	 *         },
	 *         "event": {
	 *               "change": true,
	 *               "timeSec": 10,
	 *               "lowerThreshold": 1000,
	 *               "upperThreshold": 3000
	 *         }
	 *   }],
	 * }
	 *
	 */
	json_str = l_strdup(json_object_to_json_string(json_msg));
	json_object_put(json_msg);

	return json_str;
}

struct l_queue *parser_update_to_list(const char *json_str)
{
	json_object *json_obj;
	json_object *json_array;
	json_object *json_data;
	json_object *jobjkey;
	knot_msg_data *msg;
	struct l_queue *list;
	uint64_t i;
	int jtype;
	int olen;
	uint8_t sensor_id;
	bool has_err;

	json_obj = json_tokener_parse(json_str);
	if (!json_obj)
		return NULL;

	list = l_queue_new();

	if (!json_object_object_get_ex(json_obj, KNOT_JSON_FIELD_DATA,
			&json_array)) {
		l_queue_destroy(list, l_free);
		json_object_put(json_obj);
		return NULL;
	}

	has_err = false;
	for (i = 0; i < json_object_array_length(json_array); i++) {

		json_data = json_object_array_get_idx(json_array, i);
		if (!json_data) {
			has_err = true;
			break;
		}

		/* Getting 'sensorId' */
		if (!json_object_object_get_ex(json_data,
				KNOT_JSON_FIELD_SENSOR_ID, &jobjkey)) {
			has_err = true;
			break;
		}

		if (json_object_get_type(jobjkey) != json_type_int) {
			has_err = true;
			break;
		}

		sensor_id = json_object_get_int(jobjkey);

		/* Getting 'value' */
		if (!json_object_object_get_ex(json_data,
				KNOT_JSON_FIELD_VALUE, &jobjkey)) {
			has_err = true;
			break;
		}

		jtype = json_object_get_type(jobjkey);
		if (jtype != json_type_int &&
		jtype != json_type_double && jtype != json_type_boolean &&
			jtype != json_type_string) {
			has_err = true;
			break;
		}

		msg = l_new(knot_msg_data, 1);

		olen = parse_json2data(jobjkey, &msg->payload);
		if (olen <= 0) {
			l_free(msg);
			has_err = true;
			break;
		}

		msg->sensor_id = sensor_id;
		msg->hdr.type = KNOT_MSG_PUSH_DATA_REQ;
		msg->hdr.payload_len = olen + sizeof(msg->sensor_id);

		if (!l_queue_push_tail(list, msg)) {
			l_free(msg);
			has_err = true;
			break;
		}
	}
	json_object_put(json_obj);
	if (has_err) {
		l_queue_destroy(list, l_free);
		return NULL;
	}

	return list;
}

char *parser_data_create_object(const char *device_id, uint8_t sensor_id,
				       uint8_t value_type,
				       const knot_value_type *value,
				       uint8_t kval_len,
				       struct tm *ptm)
{
	char *json_str;
	json_object *json_msg;
	json_object *data;
	json_object *json_array;
	char *encoded;
	size_t encoded_len;
	bool has_err;
	time_t rawtime = time(NULL);
	char buf[32] = {0};

	json_msg = json_object_new_object();
	json_array = json_object_new_array();

	json_object_object_add(json_msg, KNOT_JSON_FIELD_DEVICE_ID,
			       json_object_new_string(device_id));
	data = json_object_new_object();
	json_object_object_add(data, KNOT_JSON_FIELD_SENSOR_ID,
			       json_object_new_int(sensor_id));

	has_err = false;
	switch (value_type) {
	case KNOT_VALUE_TYPE_INT:
		json_object_object_add(data, KNOT_JSON_FIELD_VALUE,
				json_object_new_int(knot_value_as_int(value)));
		break;
	case KNOT_VALUE_TYPE_FLOAT:
		json_object_object_add(data, KNOT_JSON_FIELD_VALUE,
			json_object_new_double(knot_value_as_double(value)));
		break;
	case KNOT_VALUE_TYPE_BOOL:
		json_object_object_add(data, KNOT_JSON_FIELD_VALUE,
			json_object_new_boolean(knot_value_as_boolean(value)));
		break;
	case KNOT_VALUE_TYPE_RAW:
		/* Encode as base64 */
		encoded = knot_value_as_raw(value, kval_len, &encoded_len);
		if (!encoded) {
			has_err = true;
			json_object_put(json_array);
			json_object_put(data);
			break;
		}
		json_object_object_add(data, KNOT_JSON_FIELD_VALUE,
			json_object_new_string_len(encoded, encoded_len));
		break;
	case KNOT_VALUE_TYPE_INT64:
		json_object_object_add(data, KNOT_JSON_FIELD_VALUE,
			json_object_new_int64(knot_value_as_int64(value)));
		break;
	case KNOT_VALUE_TYPE_UINT:
		json_object_object_add(data, KNOT_JSON_FIELD_VALUE,
			json_object_new_uint64(knot_value_as_uint(value)));
		break;
	case KNOT_VALUE_TYPE_UINT64:
		json_object_object_add(data, KNOT_JSON_FIELD_VALUE,
			json_object_new_uint64(knot_value_as_uint64(value)));
		break;
	default:
		has_err = true;
		break;
	}

	if (ptm == NULL) {
		puts("The localtime() function failed");
		has_err = true;
	} else {
		strftime(buf, 32, "%Y-%m-%dT%X.0%z", ptm);
	}

	json_object_object_add(data, KNOT_JSON_FIELD_TIMESTAMP,
			       json_object_new_string_len(buf, strlen(buf)));
	json_object_array_add(json_array, data);
	json_object_object_add(json_msg, KNOT_JSON_FIELD_DATA, json_array);
	if (!has_err) {
		json_str = l_strdup(json_object_to_json_string(json_msg));
	}
	json_object_put(json_msg);
	/*
	 * Returned JSON object is in the following format:
	 *
	 * { "id": "fbe64efa6c7f717e",
	 *   "data": [{
	 *     "sensorId": 1,
	 *     "value": false,
	 *     "timestamp": "2022-03-04T09:22:15.0Z"
	 *   }]
	 * }
	 */

	return (json_str && !has_err) ? json_str : NULL;
}

struct l_queue *parser_config_to_list(const char *json_str)
{
	json_object *jobjconfig, *jobjarray, *jobjentry;
	struct l_queue *list;
	knot_msg_config *config;
	uint64_t i;
	const char *name;
	bool err;

	jobjconfig = json_tokener_parse(json_str);
	if (!jobjconfig)
		return NULL;

	if (!json_object_object_get_ex(jobjconfig, KNOT_JSON_FIELD_CONFIG,
				       &jobjarray)) {
		json_object_put(jobjconfig);
		return NULL;
	}

	list = l_queue_new();
	err = false;

	for (i = 0; i < json_object_array_length(jobjarray); i++) {
		config = l_new(knot_msg_config, 1);

		jobjentry = json_object_array_get_idx(jobjarray, i);
		if (!jobjentry) {
			err = true;
			break;
		}

		if (get_sensor_id(&config->sensor_id, jobjentry) < 0) {
			err = true;
			break;
		}

		if (get_schema(&config->schema, jobjentry) < 0) {
			err = true;
			break;
		}

		if (get_event(&config->event, jobjentry) < 0) {
			config->event.event_flags &= KNOT_EVT_FLAG_NONE;
			config->event.event_flags |= KNOT_EVT_FLAG_UNREGISTERED;
		}

		if (!l_queue_push_tail(list, config)) {
			err = true;
			break;
		}
	}

	if (err) {
		l_free(config);
		l_queue_destroy(list, l_free);
		json_object_put(jobjarray);
		return NULL;
	}

	return list;
}

struct l_queue *parser_queue_from_json_array(const char *json_str,
					     create_device_item_cb item_cb)
{
	json_object *jobj, *jobjentry, *jarray;
	struct l_queue *list;
	void *item;
	int len;
	int i;

	jobj = json_tokener_parse(json_str);
	if (!jobj)
		return NULL;

	jarray = json_object_object_get(jobj, KNOT_JSON_FIELD_DEVICES);
	if (!jarray)
		return NULL;

	if (json_object_get_type(jarray) != json_type_array)
		return NULL;

	len = json_object_array_length(jarray);
	list = l_queue_new();

	for (i = 0; i < len; i++) {
		jobjentry = json_object_array_get_idx(jarray, i);
		item = device_array_item(jobjentry, item_cb);
		if (item)
			l_queue_push_tail(list, item);
	}
	json_object_put(jobj);

	return list;
}

struct l_queue *parser_request_to_list(const char *json_str)
{
	json_object *jso;
	struct l_queue *list;
	json_object *json_array;
	json_object *jobjentry;
	int sensor_id;
	uint64_t i;
	bool has_err;

	jso = json_tokener_parse(json_str);
	if (!jso)
		return NULL;
	list = l_queue_new();

	if (!json_object_object_get_ex(jso, KNOT_JSON_FIELD_SENSOR_IDS,
			&json_array)) {
		l_queue_destroy(list, l_free);
		return NULL;
	}

	has_err = false;
	for (i = 0; i < json_object_array_length(json_array); i++) {

		jobjentry = json_object_array_get_idx(json_array, i);
		if (!jobjentry) {
			has_err = true;
			break;
		}

		if (json_object_get_type(jobjentry) != json_type_int) {
			has_err = true;
			break;
		}

		sensor_id = json_object_get_int(jobjentry);

		if (!l_queue_push_tail(list,
				l_memdup(&sensor_id, sizeof(sensor_id)))) {
			has_err = true;
			break;
		}
	}

	if (has_err) {
		l_queue_destroy(list, l_free);
		return NULL;
	}

	return list;
}

char *parser_sensorid_to_json(const char *key, struct l_queue *list)
{
	char *json_str;
	int *id;
	json_object *ajobj;
	json_object *entry;
	json_object *setdatajobj;

	ajobj = json_object_new_array();

	for (id = l_queue_pop_head(list); id; id = l_queue_pop_head(list)) {
		entry = json_object_new_object();
		json_object_object_add(entry, KNOT_JSON_FIELD_SENSOR_ID,
				       json_object_new_int(*id));
		json_object_array_add(ajobj, json_object_get(entry));
	}

	setdatajobj = json_object_new_object();
	json_object_object_add(setdatajobj, key, ajobj);
	json_str = l_strdup(json_object_to_json_string(setdatajobj));
	json_object_put(setdatajobj);

	return json_str;
}

char *parser_device_json_create(const char *device_id,
			const char *device_name)
{
	char *json_str;
	json_object *device;

	device = json_object_new_object();
	if (!device)
		return NULL;

	json_object_object_add(device, KNOT_JSON_FIELD_DEVICE_NAME,
			json_object_new_string(device_name));
	json_object_object_add(device, KNOT_JSON_FIELD_DEVICE_ID,
			json_object_new_string(device_id));

	/*
	 * Returned JSON object is in the following format:
	 *
	 * { "id": "fbe64efa6c7f717e",
	 *   "name": "KNoT Thing"
	 * }
	 */
	json_str = l_strdup(json_object_to_json_string(device));
	json_object_put(device);

	return json_str;
}

char *parser_auth_json_create(const char *device_id,
			const char *device_token)
{
	char *json_str;
	json_object *auth;

	auth = json_object_new_object();
	if (!auth)
		return NULL;

	json_object_object_add(auth, KNOT_JSON_FIELD_DEVICE_ID,
			       json_object_new_string(device_id));
	json_object_object_add(auth, KNOT_JSON_FIELD_DEVICE_TOKEN,
			       json_object_new_string(device_token));


	/*
	 * Returned JSON object is in the following format:
	 *
	 * { "id": "fbe64efa6c7f717e",
	 *   "token": "0c20c12e2ac058d0513d81dc58e33b2f9ff8c83d"
	 * }
	 */
	json_str = l_strdup(json_object_to_json_string(auth));
	json_object_put(auth);

	return json_str;
}

char *parser_unregister_json_create(const char *device_id)
{
	char *json_str;
	json_object *unreg;

	unreg = json_object_new_object();
	if (!unreg)
		return NULL;

	json_object_object_add(unreg, KNOT_JSON_FIELD_DEVICE_ID,
			       json_object_new_string(device_id));

	/*
	 * Returned JSON object is in the following format:
	 *
	 * { "id": "fbe64efa6c7f717e" }
	 */
	json_str = l_strdup(json_object_to_json_string(unreg));
	json_object_put(unreg);

	return json_str;
}

char *parser_get_key_str_from_json_str(const char *json_str,
				       const char *key)
{
	char *str_key;
	json_object *jso;
	json_object *jobjkey;

	jso = json_tokener_parse(json_str);
	if (!jso)
		return NULL;

	if (!json_object_object_get_ex(jso, key, &jobjkey))
		return NULL;

	if (json_object_get_type(jobjkey) != json_type_string)
		return NULL;

	str_key = l_strdup(json_object_get_string(jobjkey));
	json_object_put(jso);

	return str_key;
}

bool parser_is_key_str_or_null(const char *json_str, const char *key)
{
	json_object *jso;
	json_object *jobjkey;
	enum json_type type;

	jso = json_tokener_parse(json_str);
	if (!jso)
		return false;

	if (!json_object_object_get_ex(jso, key, &jobjkey))
		return false;

	type = json_object_get_type(jobjkey);
	json_object_put(jso);

	return type == json_type_string || type == json_type_null;
}
