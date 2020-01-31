C_FLAGS = -O2 -g -Wall -I. -lqpid-proton -lm

BUILD_OPTS = -I/opt/kgiusti/include -L/opt/kgiusti/lib64

all: clogger

clean:
	rm -f clogger

clogger: clogger.c
	gcc $(BUILD_OPTS) $(C_FLAGS) -o clogger clogger.c

