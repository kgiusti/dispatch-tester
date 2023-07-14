#!/usr/bin/env bash
#

# Run httperf test that stresses connection setup/teardown
# Assumes an http server (nginx) is listening on port 8800 and will
# serve /index.html

# set -x

set -e

TEST_RUNS=3

NGINX_HOST="127.0.0.1"
NGINX_PORT="8800"

# see *.conf files:
ROUTER_HOST="127.0.0.1"
ROUTER_PORT=8000


rm -f skrouterd-ingress-log.txt
numactl --physcpubind=3 skrouterd -c skrouterd-ingress.conf &
ROUTER_PIDS="$! "

rm -f skrouterd-egress-log.txt
numactl --physcpubind=5 skrouterd -c skrouterd-egress.conf &
ROUTER_PIDS+="$! "

echo "Waiting servers to establish"
sleep 5

# On my local machine httperf starts to fail/hang when going direct to
# nginx (no routers) if the RATE exceeds 500/sec or CONNS exceeds 5000

HP_RATE=500
HP_CONNS=5000
HP_CALLS=1
HP_TIMEOUT=5

echo -e "\nBaseline:"
httperf --hog --server $NGINX_HOST --port $NGINX_PORT --uri /index.html --rate $HP_RATE --num-conns $HP_CONNS --num-calls $HP_CALLS --timeout $HP_TIMEOUT -v

echo -e "\nBegin load..."
for (( iteration=0 ; iteration<$TEST_RUNS ; iteration+=1 )) ; do
    httperf --hog --server $ROUTER_HOST --port $ROUTER_PORT --uri /index.html --rate $HP_RATE --num-conns $HP_CONNS --num-calls $HP_CALLS --timeout $HP_TIMEOUT -v
    sleep 3
done

echo =e "\n... test complete"

kill $ROUTER_PIDS
wait $ROUTER_PIDS

