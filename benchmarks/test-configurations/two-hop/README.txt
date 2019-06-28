Two routers connected via one inter router connection

o) qdrouter-A.conf: TestRouterA with port 5672 open for clients
o) qdrouter-B.conf: TestRouterB with port 5673 open for clients

Example:
    $ ./receiver -c 500000 -a 127.0.0.1:5673 & sleep 3; ./sender -c 500000

