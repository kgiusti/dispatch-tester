#!/bin/bash
#
gcc -o ma-test -I/opt/kgiusti/include -L/opt/kgiusti/lib64 -L/opt/kgiusti/lib/qpid-dispatch -O2 -g -Wall ./message-annotation.c -lqpid-proton -lqpid-dispatch -I.
