2 router - 1 broker smoke test

This test create two routers each connected to a broker (artemis).
The router configurations define link routes and auto links that
terminate on the broker.

The test creates message traffic using test-sender and test-receiver
from the qpid-dispatch project.  The test script assumes both these
executables are found in the current path.

You must download the Artemis broker tarfile and provide it to the set-up.sh script.
The broker tarfile can be downloaded from

https://activemq.apache.org/components/artemis/download/

$ set-up.sh -p <proton tag> -d <dispatch-tag> $artemis-tarfile

The routers can be accessed via 127.0.0.1:8888 and 127.0.0.1:9999 and
the broker can be accessed via 127.0.0.1:7777.

At the end of the test a qdstat snapshot is taken of both routers.


