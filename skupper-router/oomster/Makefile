
all: oom-sender oom-receiver
.PHONY: all

LIB_DIR ?=/opt/kgiusti/lib64
INCLUDE_DIR ?= /opt/kgiusti/include

oom-sender: oom-sender.c
	gcc -Wall -O2 -I $(INCLUDE_DIR) -L $(LIB_DIR) -lqpid-proton -o oom-sender oom-sender.c

oom-receiver: oom-receiver.c
	gcc -Wall -O2 -I $(INCLUDE_DIR) -L $(LIB_DIR) -lqpid-proton -o oom-receiver oom-receiver.c

clean:
	rm -f oom-sender oom-receiver
.PHONY: clean

