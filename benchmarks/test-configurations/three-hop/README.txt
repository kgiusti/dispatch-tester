Two edge routers connected via 3 inter routers (linear)

TestRouterA <---> TestRouterB <---> TestRouterC

o) qdrouter-A.conf: TestRouterA with port 5672 open for clients
o) qdrouter-B.conf: TestRouterB (no client access)
o) qdrouter-C.conf: TestRouterB with port 5673 open for clients


Example:
    $ ./receiver -c 500000 -a 127.0.0.1:5673 & sleep 3; ./sender -c 500000


