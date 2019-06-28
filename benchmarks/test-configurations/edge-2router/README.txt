Edge configuration - two edge routers connected by two-hop interior
routers.

Clients can connect to the edges via port 5672 and 5673.

Example:
    $ ./receiver -c 500000 -a 127.0.0.1:5673 & sleep 3; ./sender -c 500000
