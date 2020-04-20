C_FLAGS = -O2 -g -Wall -I. -lqpid-proton -lm

BUILD_OPTS = -I/opt/kgiusti/include -L/opt/kgiusti/lib64

all: sender receiver server blocking-sender latency-sender latency-receiver throughput-sender throughput-receiver

clean:
	rm -f sender receiver server blocking-sender latency-sender latency-receiver throughput-sender throughput-receiver

sender: sender.c
	gcc $(BUILD_OPTS) $(C_FLAGS) -o sender sender.c

receiver: receiver.c
	gcc $(BUILD_OPTS) $(C_FLAGS) -o receiver receiver.c

server: server.c
	gcc $(BUILD_OPTS) $(C_FLAGS) -o server server.c

blocking-sender: blocking-sender.c
	gcc $(BUILD_OPTS) $(C_FLAGS) -o blocking-sender blocking-sender.c

latency-sender: latency-sender.c
	gcc $(BUILD_OPTS) $(C_FLAGS) -o latency-sender latency-sender.c

latency-receiver: latency-receiver.c
	gcc $(BUILD_OPTS) $(C_FLAGS) -o latency-receiver latency-receiver.c

throughput-sender: throughput-sender.c
	gcc $(BUILD_OPTS) $(C_FLAGS) -o throughput-sender throughput-sender.c

throughput-receiver: throughput-receiver.c
	gcc $(BUILD_OPTS) $(C_FLAGS) -o throughput-receiver throughput-receiver.c

