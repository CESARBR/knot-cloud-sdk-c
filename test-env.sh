>test/integration/out.txt
gcc -o integration_test test/integration/integration_test.c \
-lpthread -lrabbitmq -lknotcloudsdkc -lknotprotocol -lell
./integration_test >> test/integration/out.txt 2>&1
diff test/integration/expected.txt test/integration/out.txt
