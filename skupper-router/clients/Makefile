all: spout-client drain-server
.PHONY: all

drain-server: drain-server.c
	gcc -Wall -O2 -o drain-server drain-server.c

spout-client: spout-client.c
	gcc -Wall -O2 -o spout-client spout-client.c

clean:
	rm -f spout-client drain-server
.PHONY: clean

INSTALL_DIR ?= $(HOME)/.local/bin
install: all
	install -C -m 755 -t $(INSTALL_DIR) spout-client drain-server
.PHONY: install
