gcc -g -Os -Wall sender.c -o sender -lqpid-proton
gcc -g -Os -Wall receiver.c -o receiver -lqpid-proton
gcc -g -Os -Wall punisher.c -o punisher -lqpid-proton

