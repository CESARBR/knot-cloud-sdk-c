# knot-cloud-sdk-c

KNoT Cloud SDK in C is part of the KNoT project.
It is a client-side library that provides an AMQP abstraction to the KNoT Cloud
for C applications.


## Dependencies
Build:
- build-essential
- automake v1.16.1
- autoconf v2.69
- libtool v2.4.6-11
- pkg-config v0.29.1
- ell v0.18
- json-c v0.14-20200419
- rabbitmq-c v0.10.0
- knot-protocol 891d01d

*Other versions might work, but aren't officially supported*


## How to install dependencies:

`$ sudo apt-get install automake autoconf libtool`

### Install libell

The Embedded Linux Library (ELL) provides core, low-level functionality for
system daemons.
To install libell, you have to follow the instructions below:

1. `git clone git://git.kernel.org/pub/scm/libs/ell/ell.git`
2. `git checkout 0.18` to checkout to version 0.18
3. Follow instructions on `INSTALL` file

### Install json-c

json-c provides helpers functions to manipulate JSON datas.
To install json-c lib, you have to follow the instructions below:

1. `git clone https://github.com/json-c/json-c.git && cd json-c`
2. `git checkout json-c-0.14-20200419 && cd ..`
3. Follow instructions on `README.md` file

### Install rabbitmq-c

rabbitmq-c is a C-language AMQP client library for use with v2.0+ of the
[RabbitMQ](http://www.rabbitmq.com/) broker.
After install cmake, install rabbitmq-c. You have to follow the instructions
below to install it:

1. `git clone https://github.com/alanxz/rabbitmq-c`
2. `git checkout v0.10.0` to checkout to version 0.10.0
3. Follow instructions on `README.md` file

### Install KNoT Protocol

KNOT Application layer protocol library provides the application layer messages
definition for exchanging messages between KNoT Nodes (KNoT Thing Devices),
KNoT Gateway and KNoT Apps.
To install KNoT Protocol, you have to follow the instructions below:

1. `git clone git@github.com:CESARBR/knot-protocol-source.git`
2. `git checkout ead9e66` to checkout to a hash on devel branch.
3. Follow instructions on `README` file


## How to build:

1. `$ ./bootstrap-configure`
2. `$ make`


## How to install:

1. `$ make install`

## Test

In case you want to create a test enviroment for your thingd, we provide a
emulation of the cloud enviroment.
To run this emulation you must:

Run a RabbitMQ docker image:

`docker run -d --hostname rabbitmqhost --name rabbitmq -p 15672:15672 -p 5672:5672 rabbitmq:3-management-alpine`

Execute the mock-cloud.py as a listener:

`python3 test/mock-cloud.py listen`

## License

All KNoT Cloud SDK in C files are under LGPL v2.1 license, you can check
`COPYING` file for details.
