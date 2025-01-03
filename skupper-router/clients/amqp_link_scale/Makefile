all: link-receiver link-sender
.PHONY: all

link-receiver: link-receiver.c
	gcc -Wall -O2 -I/opt/kgiusti/include -L/opt/kgiusti/lib64 -lqpid-proton -o link-receiver link-receiver.c

link-sender: link-sender.c
	gcc -Wall -O2 -I/opt/kgiusti/include -L/opt/kgiusti/lib64 -lqpid-proton -o link-sender link-sender.c

clean:
	rm -f link-receiver link-sender
.PHONY: clean

INSTALL_DIR ?= $(HOME)/.local/bin
install: all
	install -C -m 755 -t $(INSTALL_DIR) link-receiver link-sender
.PHONY: install
