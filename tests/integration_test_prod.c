#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <ell/ell.h>
#include <json-c/json.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <signal.h>

#include <amqp.h>
#include <amqp_tcp_socket.h>
#include <amqp_framing.h>

#include <knot/knot_protocol.h>
#include <knot/knot_types.h>
#include <knot/knot_cloud.h>

#include <assert.h>

#include "utils.h"

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

static void on_cloud_disconnected(void *user_data)
{
	l_info("Disconnected to Cloud");
}

static int test_register_device()
{
	l_info("TESTING RESGISTER DEVICE");

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

	l_info("TESTING AUTH DEVICE");

	return knot_cloud_auth_device(id, token);
}

static int test_update_schema()
{
	struct l_queue *schema_queue;

	schema_queue = l_queue_new();

	l_queue_push_tail(schema_queue, "SCHEMA DATA");

	l_info("TESTING UPDATE SCHEMA");

	return knot_cloud_update_schema("DEVICE_ID", schema_queue);
}

static int test_publish_data()
{
	const knot_value_type value;
	int rc;

	l_info("PUBLISHING DATA");

	rc = knot_cloud_publish_data("TEST_SENSOR", 12,
				     1, &value,
				     sizeof(value));

	return rc;
}

static int test_unregister_device()
{
	char id[KNOT_PROTOCOL_UUID_LEN + 1];

	l_info("TESTING UNREGISTER DEVICE");

	strcpy(id, "udjcnaisjdncoajsmdjancisjancjdisjanr");

	return knot_cloud_unregister_device(id);
}

static amqp_bytes_t consume_mensages()
{
	char const *hostname;
	int port, status;
	char const *exchange;
	char const *bindingkey;
	char *q_name;
	amqp_socket_t *socket = NULL;
	amqp_connection_state_t conn;
	amqp_rpc_reply_t res;
	amqp_envelope_t envelope;
	amqp_bytes_t msg;

	amqp_bytes_t queuename;

	queuename = amqp_cstring_bytes("connIn-messages");
	hostname = "localhost";
	port = 5672;
	exchange = "data.sent";
	bindingkey = "data.published";

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

	msg = envelope.message.body;

	amqp_destroy_envelope(&envelope);

	die_on_amqp_error(amqp_channel_close(conn, 1, AMQP_REPLY_SUCCESS),
			  "Closing channel");
	l_debug("Closing channel");
	die_on_amqp_error(amqp_connection_close(conn, AMQP_REPLY_SUCCESS),
			  "Closing connection");
	l_debug("Closing connection");
	die_on_error(amqp_destroy_connection(conn), "Ending connection");
	l_debug("Ending connection");

	amqp_bytes_free(queuename);

	// l_debug("%s", (char *)msg.bytes);

	return msg;
}

static bool on_recived_msg(const struct knot_cloud_msg *msg,
			   void *user_data)
{
	return true;
}

static void start_test_when_connected(void *user_data)
{
	int err;

	l_info("Connected to Cloud");

	//READ START
	knot_cloud_read_start("5b620bcd419afed7", on_recived_msg, NULL);

	//TEST REGISTER
	err = test_register_device();
	if (err < 0)
		l_error("CANT REGISTER DEVICE");
	else
		l_info("DEVICE REGISTERED");

	//TEST AUTH DEVICE
	err = test_auth_device();
	if (err < 0)
		l_error("CANT AUTH DEVICE");
	else
		l_info("DEVICE AUTHENTICATED");

	// //TEST UPDATE SCHEMA
	err = test_update_schema();
	if (err < 0)
		l_error("CANT UPDATE SCHEMA");
	else
		l_info("SCHEMA UPDATED");

	//TEST PUBLISH
	err = test_publish_data();
	if (err < 0)
		l_error("CANT PUBLISH DATA");
	else
		l_info("DATA PUBLISHED");

	//TEST UNREGISTER DEVICE
	err = test_unregister_device();
	if (err < 0)
		l_error("CANT UNREGISTER DEVICE");
	else
		l_info("DEVICE UNREGISTERED");

}

int main(int argc, char const *argv[])
{
	int rc;

	l_log_set_stderr();

	if (!l_main_init())
		return -1;

	l_info("\nKNOT CLOUD SDK INTEGRATION TEST\n");

	knot_cloud_set_log_priority("info");

	rc = knot_cloud_start("amqp://guest:guest@localhost:5672", "USER_TOKEN",
			      start_test_when_connected,
			      on_cloud_disconnected,
			      NULL);

	l_main_run_with_signal(signal_handler, NULL); //

	knot_cloud_stop();

	l_main_exit(); //

	return 0;
}
