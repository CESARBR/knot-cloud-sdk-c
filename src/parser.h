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

/* JSON Fields (keys) related to KNOT messages */
#define KNOT_JSON_FIELD_DEVICE_NAME	"name"
#define KNOT_JSON_FIELD_DEVICE_ID	"id"
#define KNOT_JSON_FIELD_DEVICE_TOKEN	"token"
#define KNOT_JSON_FIELD_CONFIG		"config"
#define KNOT_JSON_FIELD_DATA		"data"
#define KNOT_JSON_FIELD_DEVICES		"devices"
#define KNOT_JSON_FIELD_SENSOR_ID	"sensorId"
#define KNOT_JSON_FIELD_SCHEMA		"schema"
#define KNOT_JSON_FIELD_EVENT		"event"
#define KNOT_JSON_FIELD_SENSOR_IDS	"sensorIds"
#define KNOT_JSON_FIELD_VALUE		"value"
#define KNOT_JSON_FIELD_VALUE_TYPE	"valueType"
#define KNOT_JSON_FIELD_UNIT		"unit"
#define KNOT_JSON_FIELD_TYPE_ID		"typeId"
#define KNOT_JSON_FIELD_ERROR		"error"
#define KNOT_JSON_FIELD_CHANGE		"change"
#define KNOT_JSON_FIELD_TIME_SEC	"timeSec"
#define KNOT_JSON_FIELD_LOWER_THRESHOLD	"lowerThreshold"
#define KNOT_JSON_FIELD_UPPER_THRESHOLD	"upperThreshold"

typedef void *(create_device_item_cb) (const char *id, const char *name,
				       struct l_queue *schema);

char *parser_config_create_object(const char *device_id,
					 struct l_queue *config_list);
struct l_queue *parser_update_to_list(const char *json_str);
char *parser_data_create_object(const char *device_id, uint8_t sensor_id,
				uint8_t value_type,
				const knot_value_type *value,
				uint8_t kval_len);
struct l_queue *parser_config_to_list(const char *json_str);
struct l_queue *parser_queue_from_json_array(const char *json_str,
					     create_device_item_cb item_cb);
struct l_queue *parser_request_to_list(const char *json_str);
char *parser_sensorid_to_json(const char *key, struct l_queue *list);
char *parser_device_json_create(const char *device_id,
				       const char *device_name);
char *parser_auth_json_create(const char *device_id,
				     const char *device_token);
char *parser_unregister_json_create(const char *device_id);
char *parser_get_key_str_from_json_str(const char *json_str,
				       const char *key);
bool parser_is_key_str_or_null(const char *json_str, const char *key);
