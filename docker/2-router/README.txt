Oslo.Messaging 2 router smoke test
----------------------------------

This test instantiates two dispatch routers in containers and runs a
long progression of Oslo.Messaging RPC and Notification message
transfers across them.

To run the tests:

0) source ./set-up.sh - this will build and deploy two routers.  You
can control the URL and the branch/tag/SHA1 used to check out dispatch
and the proton sources - see set-up.sh for details.

1) Once the routers have come up, create a python virtual environment
using virtualenv.  Activate the environment, and install pyngus,
oslo.messaging, and ombt packages.  Example:

$ virtualenv venv
$ source ./venv/bin/activate
$ pip install pyngus oslo.messaging ombt

2) run the smoke test script found in the tests directory (from within
the python virtual environment of course).  Example:

(venv) $ source ./tests/test_smoke_01.sh

You can run qdstat commands against the routers by specifying the '-b'
option with the proper port number (see the router*-qdrouterd.conf
files).  Example:

$ qdstat -b localhost:15672 -m

Or exec directly into a container and query over the mesh:

$ docker exec -it  Router1-2router /bin/bash
# qdstat -b 127.0.0.1:15672 -m
# qdstat -b 127.0.0.1:15672 -r Router.2 -m

3) Source the clean-up.sh script to destroy the router containers when
finished.

Note: the set-up.sh and clean-up.sh scripts run docker via the 'sudo'
command.  You'll need the necessary permissions to be able to run
'sudo'.
