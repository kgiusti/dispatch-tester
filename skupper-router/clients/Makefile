all: spout-client drain-server

drain-server: drain-server.c
	gcc -Wall -o drain-server drain-server.c

spout-client: spout-client.c
	gcc -Wall -o spout-client spout-client.c

clean:
	rm -f spout-client drain-server

