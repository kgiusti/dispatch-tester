set -x
gcc ${BUILD_OPTS} -g -Os -Wall sender.c -o sender -lqpid-proton -lm
gcc ${BUILD_OPTS} -g -Os -Wall receiver.c -o receiver -lqpid-proton -lm
set +x
