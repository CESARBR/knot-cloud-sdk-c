gcc -o integration_test_prod utils.c integration_test_prod.c -lrabbitmq -lknotcloudsdkc -lknotprotocol -lell
valgrind --leak-check=full  --track-origins=yes --track-fds=yes ./integration_test_prod
#./integration_test_prod
