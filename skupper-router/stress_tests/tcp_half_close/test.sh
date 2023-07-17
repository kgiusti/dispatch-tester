#!/usr/bin/env bash
#

# Test the idle half-close feature

# set -x

set -e

# see *.conf files:
ROUTER_HOST="127.0.0.1"
CLIENT_PORT=8000
SERVER_PORT=8800

# Enable 1/2 close idle timeout (default is 
export SKUPPER_ROUTER_ENABLE_1152=ON

rm -f skrouterd-ingress-log.txt
numactl --physcpubind=3 skrouterd -c skrouterd-ingress.conf &
ROUTER_PIDS="$! "

rm -f skrouterd-egress-log.txt
numactl --physcpubind=5 skrouterd -c skrouterd-egress.conf &
ROUTER_PIDS+="$! "

./server-idle $ROUTER_HOST $SERVER_PORT &
SERVER_PID="$! "

echo "Waiting servers to establish"
sleep 5

echo -e "\nBegin test..."
./client-half-close $ROUTER_HOST $CLIENT_PORT
echo -e "\n... test complete"

skstat -c -r RouterTcpIngress
skstat -c -r RouterTcpEgress

kill $SERVER_PID $ROUTER_PIDS
wait $SERVER_PID $ROUTER_PIDS

