#!/usr/bin/env bash
#

# Repeatedly create and destroy a large number of TCP sessions

# set -x

set -e

TEST_RUNS=20

# see *.conf files:
ROUTER_HOST="127.0.0.1"
CLIENT_PORT=8000
SERVER_PORT=8800

MAX_CONNS=10000

# attempt to force open file maximum to the hard limit:
FMAX_LIMIT=$(ulimit -H -n)
ulimit -S -n $FMAX_LIMIT

rm -f skrouterd-ingress-log.txt
numactl --physcpubind=3 skrouterd -c skrouterd-ingress.conf &
ROUTER_PIDS="$! "

rm -f skrouterd-egress-log.txt
numactl --physcpubind=5 skrouterd -c skrouterd-egress.conf &
ROUTER_PIDS+="$! "

echo "Waiting servers to establish"
sleep 5

echo -e "\nBegin test..."
for (( iteration=0 ; iteration<$TEST_RUNS ; iteration+=1 )) ; do
    ./tcp-conn-scale $MAX_CONNS $ROUTER_HOST $CLIENT_PORT $SERVER_PORT
    sleep 5
done

echo -e "\n... test complete"

skstat -m -r RouterTcpIngress
skstat -m -r RouterTcpEgress

kill $ROUTER_PIDS
wait $ROUTER_PIDS

