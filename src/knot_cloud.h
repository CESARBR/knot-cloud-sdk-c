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

struct knot_cloud_device {
	char *id;
	char *uuid;
	char *name;
	bool online;
	struct l_queue *schema;
	struct l_timeout *unreg_timeout;
};

struct knot_cloud_msg {
	const char *device_id;
	const char *error;
	enum {
		UPDATE_MSG,
		REQUEST_MSG,
		REGISTER_MSG,
		UNREGISTER_MSG,
		AUTH_MSG,
		SCHEMA_MSG,
		LIST_MSG,
		MSG_TYPES_LENGTH
	} type;
	union {
		const char *token; // used when type is REGISTER
		struct l_queue *list; // used when type is UPDATE/REQUEST/LIST
	};
};

typedef bool (*knot_cloud_cb_t) (const struct knot_cloud_msg *msg,
				 void *user_data);
typedef void (*knot_cloud_connected_cb_t) (void *user_data);
typedef void (*knot_cloud_disconnected_cb_t) (void *user_data);

int knot_cloud_register_device(const char *id, const char *name);
int knot_cloud_unregister_device(const char *id);
int knot_cloud_auth_device(const char *id, const char *token);
int knot_cloud_update_schema(const char *id, struct l_queue *schema_list);
int knot_cloud_list_devices(void);
int knot_cloud_publish_data(const char *id, uint8_t sensor_id,
			    uint8_t value_type, const knot_value_type *value,
			    uint8_t kval_len);
int knot_cloud_read_start(const char *id, knot_cloud_cb_t read_handler_cb,
			  void *user_data);
int knot_cloud_start(char *url, char *user_token,
		     knot_cloud_connected_cb_t connected_cb,
		     knot_cloud_disconnected_cb_t disconnected_cb,
		     void *user_data);
void knot_cloud_stop(void);

