#!/usr/bin/env bash
#

# set -x

TEST_RUNS=3

iperf3 -s -p 5002 &
SERVER_PIDS="$! "
iperf3 -s -p 5003 &
SERVER_PIDS+="$! "
iperf3 -s -p 5004 &
SERVER_PIDS+="$! "

rm -f skrouterd-ingress-log.txt
numactl --physcpubind=3 skrouterd -c skrouterd-ingress.conf &
ROUTER_PIDS="$! "

rm -f skrouterd-egress-log.txt
numactl --physcpubind=5 skrouterd -c skrouterd-egress.conf &
ROUTER_PIDS+="$! "

echo "Waiting servers to establish"
sleep 10

echo "Begin load..."
for (( iteration=0 ; iteration<$TEST_RUNS ; iteration+=1 )) ; do
    TEST_PIDS=
    iperf3 -c 0.0.0.0 -p 6002 -t 10 -P 5 &
    TEST_PIDS+="$! "
    iperf3 -c 0.0.0.0 -p 6003 -t 10 -P 5 &
    TEST_PIDS+="$! "
    iperf3 -c 0.0.0.0 -p 6004 -t 10 -P 5 &
    TEST_PIDS+="$! "

    sleep 4

    kill -9 $TEST_PIDS
    wait $TEST_PIDS

    echo "KILLED"
    sleep 2
done

echo "... test complete"

kill $ROUTER_PIDS $SERVER_PIDS
wait $ROUTER_PIDS $SERVER_PIDS

