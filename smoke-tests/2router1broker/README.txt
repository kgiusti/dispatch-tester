2 router - 1 broker smoke test

This test create containers for two routers each connected to a broker
(artemis) running in its own container.  The router configurations
define link routes and auto links that terminate on the broker.

The test creates message traffic using test-sender and test-receiver
from the qpid-dispatch project.  The test script assumes both these
executables are found in the current path, as well as the qdstat
router tool.

You must download the Artemis broker tarfile and provide it to the set-up.sh script.
The broker tarfile can be downloaded from

https://activemq.apache.org/components/artemis/download/

$ set-up.sh -p <proton tag> -d <dispatch-tag> $artemis-tarfile

The routers can be accessed via 127.0.0.1:8888 and 127.0.0.1:9999 and
the broker can be accessed via 127.0.0.1:7777.

The test is run by executing the test.sh script once set-up.sh
completes.  This script runs 1000 cycles of bursty traffic.

Once the tests are complete a snapshot is taken of each router's state
and stored in a file named qdstat-RouterA.txt and qdstat-RouterB.txt.

The containers are left running once test.sh completes.  The script
clean-up.sh can be run to destroy the containers.






