gcc -o integration_test_cons utils.c integration_test_cons.c -lrabbitmq -lknotcloudsdkc -lknotprotocol -lell
valgrind --leak-check=full  --track-origins=yes --track-fds=yes ./integration_test_cons
#./integration_test_cons
