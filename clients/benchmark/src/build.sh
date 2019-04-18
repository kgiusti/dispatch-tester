gcc ${BUILD_OPTS} -g -Os -Wall sender.c -o sender -lqpid-proton
gcc ${BUILD_OPTS} -g -Os -Wall receiver.c -o receiver -lqpid-proton

