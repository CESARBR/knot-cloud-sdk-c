#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <ell/ell.h>
#include <json-c/json.h>

#include <assert.h>
#include <signal.h>
#include <amqp.h>
#include <amqp_tcp_socket.h>
#include <amqp_framing.h>

#include "utils.h"

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

static void signal_handler(uint32_t signo, void *user_data)
{
	switch (signo)
	{
	case SIGINT:
	case SIGTERM:
		l_info("Terminate");
		l_main_quit();
		break;
	}
}

static char *stringify_bytes(amqp_bytes_t bytes)
{
	/* We will need up to 4 chars per byte, plus the terminating 0 */
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

static amqp_bytes_t consume_mensages(char const *exchange,char const *bindingkey)
{
	char const *hostname;
	int port, status;
	amqp_socket_t *socket = NULL;
	amqp_rpc_reply_t res;
	amqp_envelope_t envelope;
	amqp_bytes_t msg;


	queuename = amqp_cstring_bytes("connIn-messages");
	hostname = "localhost";
	port = 5672;

	conn = amqp_new_connection();

	socket = amqp_tcp_socket_new(conn);
	if (!socket)
	{
		l_info("creating TCP socket");
	}

	status = amqp_socket_open(socket, hostname, port);
	if (status)
	{
		l_info("opening TCP socket");
	}

	die_on_amqp_error(amqp_login(conn, "/", 0, 131072, 0, AMQP_SASL_METHOD_PLAIN,
				     "guest", "guest"),
			  "Logging in");
	amqp_channel_open(conn, 1);
	die_on_amqp_error(amqp_get_rpc_reply(conn), "Opening channel");

	{
		amqp_queue_declare_ok_t *r = amqp_queue_declare(
		    conn, 1, amqp_empty_bytes, 0, 0, 0, 1, amqp_empty_table);
		die_on_amqp_error(amqp_get_rpc_reply(conn), "Declaring queue");
		queuename = amqp_bytes_malloc_dup(r->queue);
		if (queuename.bytes == NULL)
		{
			l_info("Out of memory while copying queue name");
			// return NULL;
		}
	}

	amqp_queue_bind(conn, 1, queuename, amqp_cstring_bytes(exchange),
			amqp_cstring_bytes(bindingkey), amqp_empty_table);
	die_on_amqp_error(amqp_get_rpc_reply(conn), "Binding queue");

	amqp_basic_consume(conn, 1, queuename, amqp_empty_bytes, 0, 1, 0,
			   amqp_empty_table);
	die_on_amqp_error(amqp_get_rpc_reply(conn), "Consuming");

	amqp_maybe_release_buffers(conn);

	res = amqp_consume_message(conn, &envelope, NULL, 0);

	if (AMQP_RESPONSE_NORMAL != res.reply_type)
	{
		l_error("EROR");
		// return NULL;
	}

	// msg = envelope.message.body;

	msg.bytes = l_memdup(envelope.message.body.bytes, envelope.message.body.len);
	msg.len = envelope.message.body.len;

	die_on_amqp_error(amqp_channel_close(conn, 1, AMQP_REPLY_SUCCESS),
			  "Closing channel");
	l_debug("Closing channel");
	die_on_amqp_error(amqp_connection_close(conn, AMQP_REPLY_SUCCESS),
			  "Closing connection");
	l_debug("Closing connection");
	die_on_error(amqp_destroy_connection(conn), "Ending connection");
	l_debug("Ending connection");


	amqp_bytes_free(queuename);
	amqp_destroy_envelope(&envelope);


	// l_debug("%s", (char *)msg.bytes);

	return msg;
}

int verify_register_device()
{
	amqp_bytes_t res;
	char *msg;

	res = consume_mensages(DEVICE_EXCHANGE,EVENT_REGISTER);

	msg = stringify_bytes(res);

	if(strcmp(msg,
	   "{ \"name\": \"TESTDEVICE\", \"id\": \"5b620bcd419afed7\" }") != 0)
	{
		l_error("DATA DOESN'T MATCH EXPECTED");

		amqp_bytes_free(res);
		free(msg);

		return -EINVAL;
	}

	amqp_bytes_free(res);

	free(msg);

	return 0;
}

int verify_auth_device()
{
	amqp_bytes_t res;
	char *msg;

	res = consume_mensages(DEVICE_EXCHANGE,EVENT_AUTH);

	msg = stringify_bytes(res);

	if(strcmp(msg,
	   "{ \"id\": \"5b620bcd419afed7\", \"token\": "
	   "\"41aa0229342ecf8eb686cde53f739c1c3da9c1c5\" }") != 0)
	{
		l_error("DATA DOESN'T MATCH EXPECTED");

		amqp_bytes_free(res);
		free(msg);

		return -EINVAL;
	}

	amqp_bytes_free(res);

	free(msg);

	return 0;
}

int verify_update_schema()
{
	amqp_bytes_t res;
	char *msg;


	res = consume_mensages(DEVICE_EXCHANGE,EVENT_SCHEMA);

	msg = stringify_bytes(res);

	if(strcmp(msg,
	   "{ \"id\": \"DEVICE_ID\", \"schema\": [ { \"sensorId\": 72,"
	   " \"valueType\": 69, \"unit\": 77, \"typeId\": 8257, \"name\":"
	   " \"DATA\" } ] }") != 0)
	{
		l_error("DATA DOESN'T MATCH EXPECTED");

		amqp_bytes_free(res);
		free(msg);

		return -EINVAL;
	}

	amqp_bytes_free(res);

	free(msg);

	return 0;
}

int verify_publish_data()
{
	amqp_bytes_t res;
	char *msg;

	res = consume_mensages(DATA_EXCHANGE,KEY_DATA);


	msg = stringify_bytes(res);

	l_info("%s",msg);
	if(strcmp(msg,
	   "{ \"id\": \"TEST_SENSOR\", \"data\": "
	   "[ { \"sensorId\": 12, \"value\": -16778544 } ] }") != 0)
	{
		l_error("DATA DOESN'T MATCH EXPECTED");

		amqp_bytes_free(res);
		free(msg);

		return -EINVAL;
	}

	amqp_bytes_free(res);

	free(msg);

	return 0;
}

int verify_unregister_device()
{
	amqp_bytes_t res;
	char * msg;


	res = consume_mensages(DEVICE_EXCHANGE,EVENT_UNREGISTER);

	msg = stringify_bytes(res);

	if(strcmp(msg,
	   "{ \"id\": \"udjcnaisjdncoajsmdjancisjancjdisjanr\" }") != 0)
	{
		l_error("DATA DOESN'T MATCH EXPECTED");

		amqp_bytes_free(res);
		free(msg);

		return -EINVAL;
	}

	amqp_bytes_free(res);

	free(msg);

	return 0;
}

int verify_mensages_integrity()
{
	int err;

	l_info("VERIFY MENSSAGES INTEGRITY");

	// err = verify_register_device();
	// if(err<0)
	// 	l_info("REGISTER DEVICE : ERR");
	// else
	// 	l_info("REGISTER DEVICE : OK");

	// err = verify_auth_device();
	// if(err<0)
	// 	l_info("AUTH DEVICE : ERR");
	// else
	// 	l_info("AUTH DEVICE : OK");

	// err = verify_update_schema();
	// if(err<0)
	// 	l_info("UPDATE SCHEMA : ERR");
	// else
	// 	l_info("UPDATE SCHEMA : OK");

	err = verify_publish_data();
	if(err<0)
		l_info("PUBLISH DATA : ERR");
	else
		l_info("PUBLISH DATA : OK");

	err = verify_unregister_device();
	if(err<0)
		l_info("UNREGISTER DEVICE : ERR");
	else
		l_info("UNREGISTER DEVICE : OK");
}

int main(int argc, char const *argv[])
{
	int err;


	l_info("\nKNOT CLOUD SDK INTEGRATION TEST: CONSUMER\n");

	l_log_set_stderr();

	if (!l_main_init())
		return -1;

	err = verify_mensages_integrity();

	l_main_run_with_signal(signal_handler, NULL); //

	l_main_exit(); //

	return 0;
}
