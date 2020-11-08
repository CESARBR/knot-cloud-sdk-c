#!/usr/bin/bash

DOCKER_NAME="rabbitmq-test-env"

start_docker_env() {
	CURR_TRY=0
	MAX_TRY=1000

	DOCKER_HOSTNAME="rabbitmqhost"
	RABBITMQ_URL="localhost:15672"
	DOCKER_IMAGE="rabbitmq:3-management-alpine"

	docker run -d --hostname "$DOCKER_HOSTNAME" --name "$DOCKER_NAME" \
		-p 15672:15672 -p 5672:5672 "$DOCKER_IMAGE" >>/dev/null 2>&1

	while [ "$(curl -LI localhost:15672 -o /dev/null -w '%{http_code}\n' -s)" == "000" ]; do
		CURR_TRY=$((CURR_TRY + 1))
		if [ $CURR_TRY -eq $MAX_TRY ]; then
			break
		fi
		sleep 1
	done

	if [ $CURR_TRY -eq $MAX_TRY ]; then
		printf '%s\n' "Could not start rabbitmq docker environment" >&2
		exit 1
	fi

	return 0
}

execute_test() {
	TEST_PATH="tests/integration/integration_test.c"
	TEST_LIBS="-lpthread -lrabbitmq -lknotcloudsdkc -lknotprotocol -lell"
	TEST_OUTPUT="tests/integration/out.txt"
	TEST_EXPECTED="tests/integration/expected.txt"

	>"$TEST_OUTPUT"

	gcc -o integration_test "$TEST_PATH" \
		-lpthread -lrabbitmq -lknotcloudsdkc -lknotprotocol -lell

	./integration_test >>"$TEST_OUTPUT" 2>&1

	diff "$TEST_EXPECTED" "$TEST_OUTPUT"
	error=$?
	if [ $error -ne 0 ]; then
		printf '%s\n' "TEST FAILED" >&2
		exit 1
	fi

	return 0
}

tear_down_docker_env() {
	docker container rm "$(docker container stop "$(docker ps -q \
	--filter "name="$DOCKER_NAME"")")" >>/dev/null 2>&1
}

run() {
	start_docker_env
	execute_test
	tear_down_docker_env

	return 0
}

run
