#!/usr/bin/python3
#
#  This file is part of the KNOT Project
#
#  Copyright (c) 2019, CESAR. All rights reserved.
#
#   This library is free software; you can redistribute it and/or
#   modify it under the terms of the GNU Lesser General Public
#   License as published by the Free Software Foundation; either
#   version 2.1 of the License, or (at your option) any later version.
#
#   This library is distributed in the hope that it will be useful,
#   but WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
#   Lesser General Public License for more details.
#
#   You should have received a copy of the GNU Lesser General Public
#   License along with this library; if not, write to the Free Software
#   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

import pika
import logging
import json
import argparse
import secrets

#AMQP Configuration values:
data_exchange = 'data.sent'
device_exchange = 'device'

QUEUE_CLOUD_NAME = 'connIn-messages'

MESSAGE_EXPIRATION_TIME_MS = '10000'
# Babeltower Events API
EVENT_REGISTER = 'device.register'
KEY_REGISTERED = 'device.registered'

EVENT_UNREGISTER = 'device.unregister'
KEY_UNREGISTERED = 'device.unregistered'

EVENT_AUTH = 'device.auth'
KEY_AUTH = 'device.auth'

EVENT_LIST = 'device.cmd.list'
KEY_LIST_DEVICES = 'device.list'

EVENT_SCHEMA = 'device.schema.sent'
KEY_SCHEMA = 'device.schema.updated'

KEY_DATA = 'data.published'

#Mapper object -> Maps EVENT (Routing Key) to KEY (Response)
mapper = {
    EVENT_REGISTER: KEY_REGISTERED,
    EVENT_UNREGISTER: KEY_UNREGISTERED,
    EVENT_AUTH: KEY_AUTH,
    EVENT_LIST: KEY_LIST_DEVICES,
    EVENT_SCHEMA: KEY_SCHEMA,
    data_exchange: KEY_DATA # Thing data has it own fanout exchange, so it doesn't need a Routing Key
}

logging.basicConfig(
    format='%(asctime)s %(message)s',
    level=logging.INFO,
    datefmt='%Y-%m-%d %H:%M:%S')

def __on_msg_received(args, channel, method, properties, body):
    logging.info("%r:%r" % (method.routing_key, body))
    message = json.loads(body)
    message['error'] = 'error mocked' if args.with_side_effect else None

    if method.exchange == data_exchange: # ONLINE state
        channel.basic_publish(
            exchange=data_exchange,
            routing_key=KEY_DATA,
            body=json.dumps(message),

        )
        logging.info(" [x] Sent %r" % (message))
        return None

    elif method.routing_key == EVENT_REGISTER: # REGISTER state
        message['token'] = secrets.token_hex(20)
        del message['name']

    elif method.routing_key == EVENT_AUTH: # AUTH state
        del message['token']
        channel.basic_publish(
            exchange=device_exchange,
            routing_key=properties.reply_to,
            body=json.dumps(message),

        )
        logging.info(" [x] Sent %r" % (message))
        return None

    elif method.routing_key == EVENT_LIST:
        message['devices'] = [
        {
            'id': secrets.token_hex(8),
            'name': 'test',
            'schema': [{
                "sensor_id": 0,
                "value_type": 3,
                "unit": 0,
                "type_id": 65521,
                "name": "LED"
            }]
        }, {
            'id': secrets.token_hex(8),
            'name': 'test2',
            'schema': [{
                "sensor_id": 0,
                "value_type": 3,
                "unit": 0,
                "type_id": 65521,
                "name": "LED"
            }]
            }
        ]
        channel.basic_publish(
            exchange=device_exchange,
            routing_key=properties.reply_to,
            body=json.dumps(message)
        )
        logging.info(" [x] Sent %r" % (message))
        return None

    elif method.routing_key == EVENT_SCHEMA: # SCHEMA state
        del message['schema']

    channel.basic_publish(
        exchange=device_exchange,
        routing_key=mapper[method.routing_key],
        body=json.dumps(message)
    )
    logging.info(" [x] Sent %r" % (message))

# Starts AMQP connection
def __amqp_start():
    connection = pika.BlockingConnection(
        pika.ConnectionParameters(host='localhost'))
    channel = connection.channel()
    # Device exchange
    channel.exchange_declare(exchange=device_exchange, durable=True,
                             exchange_type='direct')
    # Data receiving exchange
    channel.exchange_declare(exchange=data_exchange, durable=True,
                             exchange_type='fanout')

    return channel

# Parser sub-commands
def msg_consume(args):
    channel = __amqp_start()
    result = channel.queue_declare(
        QUEUE_CLOUD_NAME, exclusive=False, durable=True)
    queue_name = result.method.queue
    # Binding EVENTS to 'device' exchange
    channel.queue_bind(
        exchange=device_exchange, queue=queue_name, routing_key=EVENT_REGISTER)
    channel.queue_bind(
        exchange=device_exchange, queue=queue_name, routing_key=EVENT_UNREGISTER)
    channel.queue_bind(
        exchange=device_exchange, queue=queue_name, routing_key=EVENT_AUTH)
    channel.queue_bind(
        exchange=device_exchange, queue=queue_name, routing_key=EVENT_SCHEMA)

    # Binding EVENT to 'data.sent' exchange
    channel.queue_bind(
        exchange=data_exchange, queue=queue_name)

    def __wrapper_msg_received(ch, mth, props, body):
        __on_msg_received(args, ch, mth, props, body)

    # Starting to consume queue
    channel.basic_consume(queue=queue_name,
                          on_message_callback=__wrapper_msg_received,
                          auto_ack=True)

    logging.info('Listening to messages')
    channel.start_consuming()

def no_command(args):
    parser.print_help()
    exit(1)

# Setting-Up CLI Enviroment
parser = argparse.ArgumentParser(description='Mock KNoT Fog Connector')
parser.set_defaults(func=no_command)
subparsers = parser.add_subparsers(help='sub-command help', dest='subcommand')

parser_listen = subparsers.add_parser('listen', help='Listen to messages \
    from client KNoT daemon', formatter_class=argparse.RawTextHelpFormatter)
parser_listen.add_argument('-s', '--with-side-effect', action='store_true',
                           help='Send messages with error')
# Consuming Mensages
parser_listen.set_defaults(func=msg_consume)

options = parser.parse_args()
options.func(options)
