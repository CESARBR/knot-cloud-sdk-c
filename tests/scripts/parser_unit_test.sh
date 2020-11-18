#!/usr/bin/bash

TEST_OUTPUT="tests/unit/parser/out.txt"
TEST_EXPECTED="tests/unit/parser/expected.txt"
TEST_PATH="tests/unit/parser/parser_unit_test.c"
TEST_EXEC="parser_unit_test.o"
PARSER_PATH="src/parser.c"

>"$TEST_OUTPUT"

gcc -Isrc/ -o "$TEST_EXEC" "$PARSER_PATH" "$TEST_PATH" -lell -ldl -ljson-c -lknotcloudsdkc

./"$TEST_EXEC" >> "$TEST_OUTPUT" 2>&1

diff "$TEST_EXPECTED" "$TEST_OUTPUT"
