C_FLAGS = -O2 -g -Wall -I. -lqpid-proton -lm

BUILD_OPTS = -I/opt/kgiusti/include -L/opt/kgiusti/lib64

all: sender receiver server

clean:
	rm -f sender receiver server

sender: sender.c
	gcc $(BUILD_OPTS) $(C_FLAGS) -o sender sender.c

receiver: receiver.c
	gcc $(BUILD_OPTS) $(C_FLAGS) -o receiver receiver.c

server: server.c
	gcc $(BUILD_OPTS) $(C_FLAGS) -o server server.c
