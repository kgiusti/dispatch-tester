all: spout-client drain-server amqp-tcp-bridge amqp-sessions session-loader link-loader
.PHONY: all

drain-server: drain-server.c
	gcc -Wall -O2 -o drain-server drain-server.c

spout-client: spout-client.c
	gcc -Wall -O2 -o spout-client spout-client.c

amqp-tcp-bridge: amqp-tcp-bridge.c
	gcc -UNDEBUG -Wall -I/opt/kgiusti/include -L/opt/kgiusti/lib64 -lqpid-proton -g -Og -o amqp-tcp-bridge amqp-tcp-bridge.c

amqp-sessions: amqp-sessions.c
	gcc -Wall -O2 -I/opt/kgiusti/include -L/opt/kgiusti/lib64 -lqpid-proton -o amqp-sessions amqp-sessions.c

session-loader: session-loader.c
	gcc -Wall -Og -fsanitize=address,undefined -I/opt/kgiusti/include -L/opt/kgiusti/lib64 -lqpid-proton -o session-loader session-loader.c

link-loader: link-loader.c
	gcc -Wall -g -Og -I/opt/kgiusti/include -L/opt/kgiusti/lib64 -lqpid-proton -o link-loader link-loader.c

clean:
	rm -f spout-client drain-server amqp-tcp-bridge amqp-sessions session-loader link-loader
.PHONY: clean

INSTALL_DIR ?= $(HOME)/.local/bin
install: all
	install -C -m 755 -t $(INSTALL_DIR) spout-client drain-server
.PHONY: install
