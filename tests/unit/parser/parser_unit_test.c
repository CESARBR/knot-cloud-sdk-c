#include <ell/ell.h>
#include <json-c/json.h>
#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>

#include <knot/knot_protocol.h>

#include "../src/parser.h"

#define DEVICE_ID "fbe64efa6c7f717e"

static int test_config_create_object(void)
{
	struct l_queue *queue;
	const char *json_str;
	int err;

	queue = parser_config_to_list("{ \"id\": \"fbe64efa6c7f717e\", "
	"\"config\": [{\"sensorId\": 1,\"schema\": {\"typeId\": 3,\"unit"
	"\": 0,\"valueType\": 1,\"name\": \"Door lock\"},\"event\": {\"c"
	"hange\": true,\"timeSec\": 10,\"lowerThreshold\": 1000,\"upperT"
	"hreshold\": 3000}}]}");

	json_str = parser_config_create_object(DEVICE_ID, queue);

	err = strcmp("{ \"id\": \"fbe64efa6c7f717e\", \"config\": [ { \"sens"
	"orId\": 1, \"schema\": { \"valueType\": 1, \"unit\": 0, \"typeId"
	"\": 3, \"name\": \"Door lock\" }, \"event\": { \"change\": true,"
	" \"timeSec\": 10, \"lowerThreshold\": 1000, \"upperThreshold\": 3"
	"000 } } ] }", json_str);

	if(err != 0) {
		l_queue_destroy(queue, l_free);
		l_free((char*)json_str);

		return -EINVAL;
	}
	l_queue_destroy(queue, l_free);
	l_free((char*)json_str);

	return 0;
}

static int test_data_create_object(void)
{
	knot_value_type value;
	value.val_i = 1223;

	const char *json_str;
	int err;

	json_str = parser_data_create_object(DEVICE_ID, 12, 1, &value,
					     sizeof(value));

	err = strcmp("{ \"id\": \"fbe64efa6c7f717e\", \"data\": [ { \"senso"
		     "rId\": 12, \"value\": 1223 } ] }", json_str);

	if(err != 0 ) {
		l_free((char*)json_str);

		return -EINVAL;
	}

	l_free((char*)json_str);

	return 0;

}

static int test_sensorid_to_json(void)
{
	int id_1;
	int id_2;
	int id_3;
	int err;
	struct l_queue *queue;
	char *json_str;

	id_1 = 12;
	id_2 = 122;
	id_3 = 10;

	queue = l_queue_new();

	l_queue_push_tail(queue, &id_1);
	l_queue_push_tail(queue, &id_2);
	l_queue_push_tail(queue, &id_3);

	json_str = parser_sensorid_to_json("Sensor_Test", queue);

	err = strcmp("{ \"Sensor_Test\": [ { \"sensorId\": 12 }, { \"senso"
		     "rId\": 122 }, { \"sensorId\": 10 } ] }", json_str);

	l_free((char*)json_str);

	if(err != 0) {
		l_queue_destroy(queue, l_free);

		return -EINVAL;
	}

	l_queue_destroy(queue, l_free);

	return 0;
}

static int test_device_json_create(void)
{
	char *json_str;
	int err;

	json_str = parser_device_json_create(DEVICE_ID,
					     "test_device_name");

	err = strcmp("{ \"name\": \"test_device_name\", \"id\": "
		     "\"fbe64efa6c7f717e\" }", json_str);

	if(err != 0 ) {
		l_free((char*)json_str);

		return -EINVAL;
	}

	l_free((char*)json_str);

	return 0;
}

static int test_auth_json_create(void)
{
	char *json_str;
	int err;

	json_str = parser_auth_json_create(DEVICE_ID,
					   "test_device_token");

	err = strcmp("{ \"id\": \"fbe64efa6c7f717e\", \"token\":"
		     " \"test_device_token\" }", json_str);

	if(err != 0 ) {
		l_free((char*)json_str);

		return -EINVAL;
	}

	l_free((char*)json_str);

	return 0;

}

static int test_unregister_json_create(void)
{
	char *json_str;
	int err;

	json_str = parser_unregister_json_create(DEVICE_ID);

	err = strcmp("{ \"id\": \"fbe64efa6c7f717e\" }", json_str);

	if(err != 0 ) {
		l_free((char*)json_str);

		return -EINVAL;
	}

	l_free((char*)json_str);

	return 0;
}

static int test_get_key_str_from_json_str(void)
{
	char *value_str;
	int err;

	value_str = parser_get_key_str_from_json_str("{ \"id\": "
					"\"fbe64efa6c7f717e\", \"token\":"
					" \"test_device_token\" }", "id");

	err = strcmp(DEVICE_ID, value_str);

	if(err != 0 ) {
		l_free((char*)value_str);

		return -EINVAL;
	}

	l_free((char*)value_str);

	return 0;
}

static int test_is_key_str_or_null(void)
{
	bool res;

	res = parser_is_key_str_or_null("{ \"TEST_KEY\": "
					"\"fbe64efa6c7f717e\", \"token\":"
					" \"test_device_token\" }", "TEST_KEY");

	if(!res)
		return -EINVAL;

	return 0;
}

static int test_update_to_list(void)
{
	struct l_queue *queue;
	knot_msg_data *msg;
	char *json_str;

	queue = parser_update_to_list("{\"id\": \"fbe64efa6c7f717e\",\"data\":"
				      " [{\"sensorId\": 1,\"value\": false},"
				      "{\"sensorId\": 2,\"value\": 1000}]}");

	msg = l_queue_pop_head(queue);

	if(msg->sensor_id != 1 || msg->payload.val_i != 0)
		goto error;
	l_free(msg);

	msg = l_queue_pop_head(queue);

	if(msg->sensor_id != 2 || msg->payload.val_i != 1000)
		goto error;

	l_queue_destroy(queue, l_free);
	l_free(msg);

	return 0;

error:
	l_queue_destroy(queue, l_free);
	l_free(msg);

	return -EINVAL;
}

static int test_request_to_list(void)
{
	struct l_queue *queue;
	int *sensor_id;

	queue = parser_request_to_list("{\"id\": \"fbe64efa6c7f717e\","
				       "\"sensorIds\":[12,23,44]}");

	sensor_id = l_queue_pop_head(queue);

	if(sensor_id == NULL)
		goto error;

	if(*sensor_id != 12)
		goto error;

	l_free(sensor_id);
	sensor_id = l_queue_pop_head(queue);

	if(sensor_id == NULL)
		goto error;

	if(*sensor_id != 23)
		goto error;

	l_free(sensor_id);
	sensor_id = l_queue_pop_head(queue);

	if(sensor_id == NULL)
		goto error;

	if(*sensor_id != 44)
		goto error;

	l_queue_destroy(queue, l_free);
	l_free(sensor_id);

	return 0;

error:
	l_queue_destroy(queue, l_free);
	l_free(sensor_id);

	return -EINVAL;
}

static void *check_json_integrity(const char *id, const char *name,
				  struct l_queue *config)
{
	int *res_p;
	int res;

	res = 0;
	res_p = &res;

	if(strcmp(name, "test") != 0)
		goto error;

	if(strcmp(id, DEVICE_ID) != 0)
		goto error;

	const char *json_str = parser_config_create_object(DEVICE_ID,config);

	if(strcmp(json_str,"{ \"id\": \"fbe64efa6c7f717e\", \"config\": [ "
	"{ \"sensorId\": 1, \"schema\": { \"valueType\": 1, \"unit\": 0, \""
	"typeId\": 3, \"name\": \"Door lock\" }, \"event\": { \"change\": "
	"true, \"timeSec\": 10, \"lowerThreshold\": 1000, \"upperThreshold"
	"\": 3000 } } ] }") != 0 ){

		l_queue_destroy(config, l_free);
		l_free((char*) json_str);
		res = -EINVAL;

		return l_memdup(res_p, sizeof(int));
	}

	l_queue_destroy(config, l_free);
	l_free((char*) json_str);

	return l_memdup(res_p, sizeof(int));

error:
	l_queue_destroy(config, l_free);
	res = -EINVAL;

	return l_memdup(res_p, sizeof(int));
}

static int test_queue_from_json_array(void)
{
	struct l_queue *queue;
	int *err;

	queue = parser_queue_from_json_array("{\"devices\": [{\"id\" : \""
		"fbe64efa6c7f717e\", \"name\" : \"test\", \"config\": {"
		" \"id\": \"fbe64efa6c7f717e\", \"config\": [{\"sensorI"
		"d\": 1,\"schema\": {\"typeId\": 3,\"unit\": 0,\"valueT"
		"ype\": 1,\"name\": \"Door lock\"},\"event\": {\"change"
		"\": true,\"timeSec\": 10,\"lowerThreshold\": 1000,\"up"
		"perThreshold\": 3000}}]}}]}", check_json_integrity);

	err = l_queue_pop_head(queue);

	if(err == NULL)
		goto error;

	if(*err < 0)
		goto error;

	l_queue_destroy(queue, l_free);
	l_free(err);

	return 0;

error:
	l_queue_destroy(queue, l_free);
	l_free(err);

	return -EINVAL;
}

void start_test(void)
{
	int err;

	err = test_config_create_object();
	if (err < 0)
		l_error("CONFIG_CREATE_OBJECT & CONFIG_TO_LIST: ERR");
	else
		l_info("CONFIG_CREATE_OBJECT & CONFIG_TO_LIST: OK");

	err = test_data_create_object();
	if (err < 0)
		l_error("DATA_CREATE_OBJECT: ERR");
	else
		l_info("DATA_CREATE_OBJECT: OK");

	err = test_sensorid_to_json();
	if (err < 0)
		l_error("SENSORID_TO_JSON: ERR");
	else
		l_info("SENSORID_TO_JSON: OK");

	err = test_device_json_create();
	if (err < 0)
		l_error("DEVICE_JSON_CREATE: ERR");
	else
		l_info("DEVICE_JSON_CREATE: OK");

	err = test_auth_json_create();
	if (err < 0)
		l_error("AUTH_JSON_CREATE: ERR");
	else
		l_info("AUTH_JSON_CREATE: OK");

	err = test_unregister_json_create();
	if (err < 0)
		l_error("UNREGISTER_JSON_CREATE: ERR");
	else
		l_info("UNREGISTER_JSON_CREATE: OK");

	err = test_get_key_str_from_json_str();
	if (err < 0)
		l_error("GET_KEY_STR_FROM_JSON_STR: ERR");
	else
		l_info("GET_KEY_STR_FROM_JSON_STR: OK");

	err = test_is_key_str_or_null();
	if (err < 0)
		l_error("IS_KEY_STR_OR_NULL: ERR");
	else
		l_info("IS_KEY_STR_OR_NULL: OK");

	err = test_update_to_list();
	if (err < 0)
		l_error("UPDATE_TO_LIST: ERR");
	else
		l_info("UPDATE_TO_LIST: OK");

	err = test_request_to_list();
	if (err < 0)
		l_error("REQUEST_TO_LIST: ERR");
	else
		l_info("REQUEST_TO_LIST: OK");

	err = test_queue_from_json_array();
	if (err < 0)
		l_error("QUEUE_FROM_JSON_ARRAY: ERR");
	else
		l_info("QUEUE_FROM_JSON_ARRAY: OK");
}

int main(int argc, char const *argv[])
{
	l_log_set_stderr();

	start_test();

	return 0;
}
