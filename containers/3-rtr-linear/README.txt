Two edge routers connected via 3 inter routers (linear)

TestRouterA <---> TestRouterB <---> TestRouterC

o) qdrouter-A.conf: TestRouterA with port 5672 open for clients
o) qdrouter-B.conf: TestRouterB with port 5673 open for clients
o) qdrouter-C.conf: TestRouterB with port 5674 open for clients


Example:
    $ ./receiver -c 500000 -a 127.0.0.1:5674 & sleep 4; ./sender -c 500000

Build:

    $ podman build --file ../Containerfile --tag kgiusti/router-a --build-arg config_file=qdrouterd-A.conf .
    $ podman build --file ../Containerfile --tag kgiusti/router-b --build-arg config_file=qdrouterd-B.conf .
    $ podman build --file ../Containerfile --tag kgiusti/router-c --build-arg config_file=qdrouterd-C.conf .
    $ podman run -d --name router-a --net=host kgiusti/router-a
    $ podman run -d --name router-b --net=host kgiusti/router-b
    $ podman run -d --name router-c --net=host kgiusti/router-c
