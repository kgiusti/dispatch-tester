#!/usr/bin/env bash
#

set -x

TEST_RUNS=10
CLIENT_COUNT=20
REQ_COUNT=500

# fire up the routers

rm -f skrouterd-ingress-log.txt
skrouterd -c skrouterd-ingress.conf &
ROUTER_PIDS="$! "

rm -f skrouterd-interior-log.txt
skrouterd -c skrouterd-interior.conf &
ROUTER_PIDS+="$! "

rm -f skrouterd-egress-log.txt
skrouterd -c skrouterd-egress.conf &
ROUTER_PIDS+="$! "

# fire up the server

./slow-server 0.0.0.0 8800 &
SERVER_PID="$! "

echo "Waiting for routers to establish"
sleep 5

echo -e "PRE-TEST MEMORY PROFILE:\n" > ingress_results.txt
skstat -m >> ingress_results.txt
echo -e "PRE-TEST MEMORY PROFILE:\n" > interior_results.txt
skstat -m -r RouterTcpInterior >> interior_results.txt
echo -e "PRE-TEST MEMORY PROFILE:\n" > egress_results.txt
skstat -m -r RouterTcpEgress >> egress_results.txt

echo "Begin load..."
for (( iteration=0 ; iteration<$TEST_RUNS ; iteration+=1 )) ; do
    CLIENT_PIDS=
    for (( client=0 ; client<$CLIENT_COUNT ; client+=1 )) ; do
        ./client-blaster 127.0.0.1 8000 $REQ_COUNT &
        CLIENT_PIDS+="$! "
    done
    wait $CLIENT_PIDS
    echo "Iteration $iteration done"
done

echo "... test complete"

echo -e "\nPOST-TEST RESULTS:\n" >> ingress_results.txt
skstat -g >> ingress_results.txt
skstat -m >> ingress_results.txt
skmanage query --type tcpConnector >> ingress_results.txt
skmanage query --type tcpListener >> ingress_results.txt

echo -e "\nPOST-TEST RESULTS:\n" >> interior_results.txt
skstat -g -r RouterTcpInterior >> interior_results.txt
skstat -m -r RouterTcpInterior >> interior_results.txt

echo -e "\nPOST-TEST RESULTS:\n" >> egress_results.txt
skstat -g -r RouterTcpEgress >> egress_results.txt
skstat -m -r RouterTcpEgress >> egress_results.txt
skmanage query -r RouterTcpEgress --type tcpConnector >> egress_results.txt
skmanage query -r RouterTcpEgress --type tcpListener >> egress_results.txt

kill $ROUTER_PIDS $SERVER_PID
wait $ROUTER_PIDS $SERVER_PID

