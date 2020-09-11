#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <ell/ell.h>
#include <json-c/json.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <signal.h>
#include <pthread.h>
#include <errno.h>
#include <ctype.h>
#include <stdarg.h>

#include <amqp.h>
#include <amqp_tcp_socket.h>
#include <amqp_framing.h>
#include <stdint.h>

#include <knot/knot_protocol.h>
#include <knot/knot_types.h>
#include <knot/knot_cloud.h>

#include <assert.h>

// #include "utils.h"


//AMQP Configuration values:
#define DATA_EXCHANGE "data.sent"
#define KEY_DATA "data.published"

#define DEVICE_EXCHANGE "device"

#define QUEUE_CLOUD_NAME "connIn-messages"

#define MESSAGE_EXPIRATION_TIME_MS = "10000"

// Babeltower Events API - v2.0.0
#define EVENT_REGISTER "device.register"
#define KEY_REGISTERED "device.registered"

#define EVENT_UNREGISTER "device.unregister"
#define KEY_UNREGISTERED "device.unregistered"

#define EVENT_AUTH "device.auth"
#define KEY_AUTH "device.auth"

#define EVENT_LIST "device.cmd.list"
#define KEY_LIST_DEVICES "device.list"

#define EVENT_SCHEMA "device.schema.sent"
#define KEY_SCHEMA "device.schema.updated"


amqp_connection_state_t conn;
amqp_bytes_t queuename;

pthread_mutex_t lock;

static void on_cloud_disconnected(void *user_data){}

static void signal_handler(uint32_t signo, void *user_data)
{
	switch (signo)
	{
	case SIGINT:
	case SIGTERM:

		l_main_quit();
		break;
	}
}

static bool on_recived_msg(const struct knot_cloud_msg *msg,
			   void *user_data)
{
	return true;
}

#define SUMMARY_EVERY_US 1000000

void log_on_error(int x, char const *context)
{
	if (x < 0)
	{
		l_info( "%s: %s\n", context, amqp_error_string2(x));
		exit(1);
	}
}

void log_on_amqp_error(amqp_rpc_reply_t x, char const *context)
{
	switch (x.reply_type)
	{
	case AMQP_RESPONSE_NORMAL:
		return;

	case AMQP_RESPONSE_NONE:
		l_info( "%s: missing RPC reply type!\n", context);
		break;

	case AMQP_RESPONSE_LIBRARY_EXCEPTION:
		l_info( "%s: %s\n", context,
			amqp_error_string2(x.library_error));
		break;

	case AMQP_RESPONSE_SERVER_EXCEPTION:
		switch (x.reply.id)
		{
		case AMQP_CONNECTION_CLOSE_METHOD:
		{
			amqp_connection_close_t *m =
				(amqp_connection_close_t *)x.reply.decoded;
			l_info( "%s: server connection error %uh"
				", message: %.*s\n",
				context, m->reply_code, (int)m->reply_text.len,
				(char *)m->reply_text.bytes);
			break;
		}
		case AMQP_CHANNEL_CLOSE_METHOD:
		{
			amqp_channel_close_t *m =
				(amqp_channel_close_t *)x.reply.decoded;
			l_info( "%s: server channel error %uh, "
				"message: %.*s\n",
				context, m->reply_code, (int)m->reply_text.len,
				(char *)m->reply_text.bytes);
			break;
		}
		default:
			l_info( "%s: unknown server error, "
				"method id 0x%08X\n",
				context, x.reply.id);
			break;
		}
		break;
	}

	exit(1);
}

static char *stringify_bytes(amqp_bytes_t bytes)
{
	char *res = malloc(bytes.len * 4 + 1);
	uint8_t *data = bytes.bytes;
	char *p = res;
	size_t i;

	for (i = 0; i < bytes.len; i++)
	{
		if (data[i] >= 32 && data[i] != 127)
		{
			*p++ = data[i];
		}
		else
		{
			*p++ = '\\';
			*p++ = '0' + (data[i] >> 6);
			*p++ = '0' + (data[i] >> 3 & 0x7);
			*p++ = '0' + (data[i] & 0x7);
		}
	}
	*p = 0;

	return res;
}


static int verify_register_device(char *msg)
{
	if(strcmp(msg,
	   "{ \"name\": \"TESTDEVICE\", \"id\": \"5b620bcd419afed7\" }") != 0)
	{
		l_error("REGISTER DEVICE: ERR");

		return -EINVAL;
	}

	l_info("REGISTER DEVICE: OK");

	return 0;
}

static int verify_auth_device(char *msg)
{
	if(strcmp(msg,
	   "{ \"id\": \"5b620bcd419afed7\", \"token\": "
	   "\"41aa0229342ecf8eb686cde53f739c1c3da9c1c5\" }") != 0)
	{
		l_error("AUTH DEVICE: ERR");

		return -EINVAL;
	}

	l_info("AUTH DEVICE: OK");

	return 0;
}

static int verify_update_schema(char *msg)
{
	if(strcmp(msg,
	   "{ \"id\": \"DEVICE_ID\", \"schema\": [ { \"sensorId\": 72,"
	   " \"valueType\": 69, \"unit\": 77, \"typeId\": 8257, \"name\":"
	   " \"DATA\" } ] }") != 0)
	{
		l_error("UPDATE SCHEMA: ERR");

		return -EINVAL;
	}

	l_info("UPDATE SCHEMA: OK");

	return 0;
}

static int verify_publish_data(char *msg)
{
	if(strcmp(msg,
	   "{ \"id\": \"TEST_SENSOR\", \"data\": "
	   "[ { \"sensorId\": 12, \"value\": 1223 } ] }") != 0)
	{
		l_error("PUBLISH DATA: ERR");

		return -EINVAL;
	}
	l_info("PUBLISH DATA: OK");

	return 0;
}

static int verify_unregister_device(char * msg)
{
	if(strcmp(msg,
	   "{ \"id\": \"5b620bcd419afed7\" }") != 0)
	{
		l_error("UNREGISTER DEVICE: ERR");

		return -EINVAL;
	}

	l_info("UNREGISTER DEVICE: OK");

	return 0;
}

static int test_register_device()
{
	char id[KNOT_PROTOCOL_UUID_LEN + 1];
	char name[KNOT_PROTOCOL_DEVICE_NAME_LEN];

	strcpy(name, "TESTDEVICE");
	strcpy(id, "5b620bcd419afed7");

	return knot_cloud_register_device(id, name);
}

static int test_auth_device()
{
	char token[KNOT_PROTOCOL_TOKEN_LEN + 1];
	char id[KNOT_PROTOCOL_UUID_LEN + 1];

	strcpy(token, "41aa0229342ecf8eb686cde53f739c1c3da9c1c5");
	strcpy(id, "5b620bcd419afed7");



	return knot_cloud_auth_device(id, token);
}

static int test_update_schema()
{
	struct l_queue *schema_queue;

	schema_queue = l_queue_new();

	l_queue_push_tail(schema_queue, "SCHEMA DATA");

	return knot_cloud_update_schema("DEVICE_ID", schema_queue);
}

static int test_publish_data()
{
	knot_value_type value;
	int rc;
	value.val_i = 1223;

	rc = knot_cloud_publish_data("TEST_SENSOR", 12,
				     1, &value,
				     sizeof(value));

	return rc;
}

static int test_unregister_device()
{
	char id[KNOT_PROTOCOL_UUID_LEN + 1];

	strcpy(id, "5b620bcd419afed7");

	return knot_cloud_unregister_device(id);
}

static void *consume_mensages()
{
	char const *hostname;
	int port, status;
	amqp_socket_t *socket = NULL;
	amqp_rpc_reply_t res;
	amqp_envelope_t envelope;
	amqp_bytes_t msg;

	int quantity_check;
	char *routing_key_test;
	char *exchange_test;
	char *msg_str;

	pthread_detach(pthread_self());
	pthread_mutex_lock(&lock);

	queuename = amqp_cstring_bytes(QUEUE_CLOUD_NAME);
	hostname = "localhost";
	port = 5672;

	conn = amqp_new_connection();

	socket = amqp_tcp_socket_new(conn);
	if (!socket)
	{
		l_error("CREATING TCP SOCKET");
	}

	status = amqp_socket_open(socket, hostname, port);
	if (status)
	{
		l_error("OPENING TCP SOCKET");
	}

	log_on_amqp_error(amqp_login(conn, "/", 0, 131072, 0, AMQP_SASL_METHOD_PLAIN,
				     "guest", "guest"),
			  "LOGGING IN");
	amqp_channel_open(conn, 1);
	log_on_amqp_error(amqp_get_rpc_reply(conn), "OPENING CHANNEL");

	{
		amqp_queue_declare_ok_t *r = amqp_queue_declare(
		    conn, 1, amqp_empty_bytes, 0, 0, 0, 1, amqp_empty_table);
		log_on_amqp_error(amqp_get_rpc_reply(conn), "DECLARING QUEUE");
		queuename = amqp_bytes_malloc_dup(r->queue);
	}

	amqp_queue_bind(conn, 1, queuename, amqp_cstring_bytes(DEVICE_EXCHANGE),
			amqp_cstring_bytes(EVENT_REGISTER), amqp_empty_table);

	amqp_queue_bind(conn, 1, queuename, amqp_cstring_bytes(DEVICE_EXCHANGE),
			amqp_cstring_bytes(EVENT_AUTH), amqp_empty_table);

	amqp_queue_bind(conn, 1, queuename, amqp_cstring_bytes(DEVICE_EXCHANGE),
			amqp_cstring_bytes(EVENT_SCHEMA), amqp_empty_table);

	amqp_queue_bind(conn, 1, queuename, amqp_cstring_bytes(DATA_EXCHANGE),
			amqp_cstring_bytes(KEY_DATA), amqp_empty_table);

	amqp_queue_bind(conn, 1, queuename, amqp_cstring_bytes(DEVICE_EXCHANGE),
			amqp_cstring_bytes(EVENT_UNREGISTER), amqp_empty_table);

	log_on_amqp_error(amqp_get_rpc_reply(conn), "Binding queue");
	amqp_basic_consume(conn, 1, queuename, amqp_empty_bytes, 0, 1, 0,
			   amqp_empty_table);

	pthread_mutex_unlock(&lock);

	log_on_amqp_error(amqp_get_rpc_reply(conn), "Consuming");

	amqp_maybe_release_buffers(conn);

	quantity_check = 0;

	while (quantity_check != 5)
	{
		res = amqp_consume_message(conn, &envelope, NULL, 0);
		routing_key_test = stringify_bytes(envelope.routing_key);
		exchange_test = stringify_bytes(envelope.exchange);
		msg_str = stringify_bytes(envelope.message.body);

		if(strcmp(routing_key_test,EVENT_REGISTER) == 0)
		{
			verify_register_device(msg_str);
			quantity_check++;
		}
		else if(strcmp(routing_key_test,EVENT_AUTH) == 0)
		{
			verify_auth_device(msg_str);
			quantity_check++;
		}
		else if(strcmp(routing_key_test,EVENT_SCHEMA) == 0)
		{
			verify_update_schema(msg_str);
			quantity_check++;
		}
		else if(strcmp(exchange_test,DATA_EXCHANGE) == 0)
		{
			verify_publish_data(msg_str);
			quantity_check++;
		}
		else if(strcmp(routing_key_test,EVENT_UNREGISTER) == 0)
		{
			verify_unregister_device(msg_str);
			quantity_check++;
		}

		free(routing_key_test);
		free(exchange_test);
		free(msg_str);
		amqp_destroy_envelope(&envelope);
	}

	log_on_amqp_error(amqp_channel_close(conn, 1, AMQP_REPLY_SUCCESS),
			  "Closing channel");
	log_on_amqp_error(amqp_connection_close(conn, AMQP_REPLY_SUCCESS),
			  "Closing connection");
	log_on_error(amqp_destroy_connection(conn), "Ending connection");

	amqp_bytes_free(queuename);

	l_main_quit();

	pthread_exit(NULL);
}

static void start_consuming()
{
	pthread_t ptid;

	pthread_mutex_init(&lock, NULL);
	pthread_create(&ptid, NULL, &consume_mensages, NULL);

	l_main_quit();
}

static void start_test_when_connected(void *user_data)
{
	int err;

	start_consuming();

	//READ START
	knot_cloud_read_start("5b620bcd419afed7", on_recived_msg, NULL);

	//TEST REGISTER
	err = test_register_device();
	if (err < 0)
		l_error("SEND: REGISTER DEVICE");

	//TEST AUTH DEVICE
	err = test_auth_device();
	if (err < 0)
		l_error("SEND: AUTH DEVICE");

	// //TEST UPDATE SCHEMA
	err = test_update_schema();
	if (err < 0)
		l_error("SEND: UPDATE SCHEMA");

	//TEST PUBLISH
	err = test_publish_data();
	if (err < 0)
		l_error("SEND: PUBLISH DATA");

	//TEST UNREGISTER DEVICE
	err = test_unregister_device();
	if (err < 0)
		l_error("SEND: UNREGISTER DEVICE");

	pthread_mutex_destroy(&lock);
}

int main(int argc, char const *argv[])
{
	int rc;

	l_log_set_stderr();

	if (!l_main_init())
		return -1;

	knot_cloud_set_log_priority("info");

	rc = knot_cloud_start("amqp://guest:guest@localhost:5672", "USER_TOKEN",
			      start_test_when_connected,
			      on_cloud_disconnected,
			      NULL);

	l_main_run_with_signal(signal_handler, NULL);

	knot_cloud_stop();

	l_main_exit();

	return 0;
}
