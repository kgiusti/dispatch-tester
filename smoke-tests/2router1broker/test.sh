#!/bin/bash

#
# Expects test-sender and test-receiver are in your path.
# These tools are available in the tests subdir of the build directory
# of the qpid-dispatch git repo.

#set -x

ROUTER_A="127.0.0.1:8888"
ROUTER_B="127.0.0.1:9999"

PID=$$

function spawn_sender {
    test-sender -i "TestSender.$PID.$RANDOM" $@ &
    SENDER_PIDS+="$! "
    PID=$!
}

function spawn_receiver {
    test-receiver -i "TestReceiver.$PID.$RANDOM" $@ &
    RECEIVER_PIDS+="$! "
    PID=$!
}


MAX_CYCLES=20

for (( X=0 ; $X < $MAX_CYCLES ; X+=1 )); do

    echo "Starting cycle $X of $MAX_CYCLES..."
    echo "?" > RouterA.log
    echo "?" > RouterB.log

    RECEIVER_PIDS=
    spawn_receiver -c 0 -s multicast/test -a $ROUTER_A
    spawn_receiver -c 0 -s multicast/test -a $ROUTER_B
    spawn_receiver -c 0 -s closest/test   -a $ROUTER_A
    spawn_receiver -c 0 -s closest/test   -a $ROUTER_B
    spawn_receiver -c 0 -s balanced/test  -a $ROUTER_A
    spawn_receiver -c 0 -s balanced/test  -a $ROUTER_B
    spawn_receiver -c 0 -s topic.01       -a $ROUTER_A
    spawn_receiver -c 0 -s topic.01       -a $ROUTER_B
    spawn_receiver -c 0 -s queue.01       -a $ROUTER_A
    spawn_receiver -c 0 -s queue.01       -a $ROUTER_B
    spawn_receiver -c 0 -s linkroute.test -a $ROUTER_A
    spawn_receiver -c 0 -s linkroute.test -a $ROUTER_B

    echo "Receivers spawned..."
    sleep 3

    SENDER_PIDS=
    # spawn_sender -c 1000   -sx -t multicast/test -a $ROUTER_B
    # spawn_sender -c 10000  -sm -t closest/test   -a $ROUTER_B
    # spawn_sender -c 5000   -sl -t balanced/test  -a $ROUTER_B
    # spawn_sender -c 90000  -ss -t topic.01       -a $ROUTER_B
    # spawn_sender -c 4000   -sl -t queue.01       -a $ROUTER_B
    # spawn_sender -c 5000   -sm -t linkroute/test -a $ROUTER_B

    
    spawn_sender -c 500    -sx -t multicast/test -a $ROUTER_B
    spawn_sender -c 10000  -sm -t closest/test   -a $ROUTER_B
    spawn_sender -c 5000   -sl -t balanced/test  -a $ROUTER_B
    spawn_sender -c 9000   -ss -t topic.01       -a $ROUTER_B
    spawn_sender -c 4000   -sl -t queue.01       -a $ROUTER_B
    spawn_sender -c 5000   -sm -t linkroute.test -a $ROUTER_B

    spawn_sender -c 500    -sx -t multicast/test -a $ROUTER_A
    spawn_sender -c 10000  -sm -t closest/test   -a $ROUTER_A
    spawn_sender -c 5000   -sl -t balanced/test  -a $ROUTER_A
    spawn_sender -c 9000   -ss -t topic.01       -a $ROUTER_A
    spawn_sender -c 4000   -sl -t queue.01       -a $ROUTER_A
    spawn_sender -c 5000   -sm -t linkroute.test -a $ROUTER_A

    # send one unsettled
    spawn_sender -c 1 -t multicast/test      -a $ROUTER_B
    spawn_sender -c 1 -t closest/test        -a $ROUTER_B
    spawn_sender -c 1 -sl -t balanced/test   -a $ROUTER_B
    spawn_sender -c 1 -ss -t topic.01        -a $ROUTER_B
    spawn_sender -c 1  -sl -t queue.01       -a $ROUTER_B
    spawn_sender -c 1  -sm -t linkroute.test -a $ROUTER_B

    spawn_sender -c 1 -t multicast/test      -a $ROUTER_A
    spawn_sender -c 1 -t closest/test        -a $ROUTER_A
    spawn_sender -c 1 -sl -t balanced/test   -a $ROUTER_A
    spawn_sender -c 1 -ss -t topic.01        -a $ROUTER_A
    spawn_sender -c 1 -sl -t queue.01        -a $ROUTER_A
    spawn_sender -c 1 -sm -t linkroute.test  -a $ROUTER_A

    # send one unsettled, exit the process
    spawn_sender -c 1 -t multicast/test      -a $ROUTER_B  -E
    spawn_sender -c 1 -t closest/test        -a $ROUTER_B  -E
    spawn_sender -c 1 -sl -t balanced/test   -a $ROUTER_B  -E
    spawn_sender -c 1 -ss -t topic.01        -a $ROUTER_B  -E
    spawn_sender -c 1  -sl -t queue.01       -a $ROUTER_B  -E
    spawn_sender -c 1  -sm -t linkroute.test -a $ROUTER_B  -E

    spawn_sender -c 1 -t multicast/test      -a $ROUTER_A  -E
    spawn_sender -c 1 -t closest/test        -a $ROUTER_A  -E
    spawn_sender -c 1 -sl -t balanced/test   -a $ROUTER_A  -E
    spawn_sender -c 1 -ss -t topic.01        -a $ROUTER_A  -E
    spawn_sender -c 1 -sl -t queue.01        -a $ROUTER_A  -E
    spawn_sender -c 1 -sm -t linkroute.test  -a $ROUTER_A  -E

    # send presettled
    spawn_sender -c 1 -t multicast/test      -a $ROUTER_B  -u
    spawn_sender -c 1 -t closest/test        -a $ROUTER_B  -u
    spawn_sender -c 1 -sl -t balanced/test   -a $ROUTER_B  -u
    spawn_sender -c 1 -ss -t topic.01        -a $ROUTER_B  -u
    spawn_sender -c 1  -sl -t queue.01       -a $ROUTER_B  -u
    spawn_sender -c 1  -sm -t linkroute.test -a $ROUTER_B  -u

    spawn_sender -c 1 -t multicast/test      -a $ROUTER_A  -u
    spawn_sender -c 1 -t closest/test        -a $ROUTER_A  -u
    spawn_sender -c 1 -sl -t balanced/test   -a $ROUTER_A  -u
    spawn_sender -c 1 -ss -t topic.01        -a $ROUTER_A  -u
    spawn_sender -c 1 -sl -t queue.01        -a $ROUTER_A  -u
    spawn_sender -c 1 -sm -t linkroute.test  -a $ROUTER_A  -u


    # anonymous link
    spawn_sender -c 1 -t multicast/test      -a $ROUTER_B  -n
    spawn_sender -c 1 -t closest/test        -a $ROUTER_B  -n
    spawn_sender -c 1 -sl -t balanced/test   -a $ROUTER_B  -n
    spawn_sender -c 1 -ss -t topic.01        -a $ROUTER_B  -n
    spawn_sender -c 1  -sl -t queue.01       -a $ROUTER_B  -n
    spawn_sender -c 1  -sm -t linkroute.test -a $ROUTER_B  -n

    spawn_sender -c 1 -t multicast/test      -a $ROUTER_A  -n
    spawn_sender -c 1 -t closest/test        -a $ROUTER_A  -n
    spawn_sender -c 1 -sl -t balanced/test   -a $ROUTER_A  -n
    spawn_sender -c 1 -ss -t topic.01        -a $ROUTER_A  -n
    spawn_sender -c 1 -sl -t queue.01        -a $ROUTER_A  -n
    spawn_sender -c 1 -sm -t linkroute.test  -a $ROUTER_A  -n

    # add message annotations
    spawn_sender -c 1 -t multicast/test      -a $ROUTER_B  -M
    spawn_sender -c 1 -t closest/test        -a $ROUTER_B  -M
    spawn_sender -c 1 -sl -t balanced/test   -a $ROUTER_B  -M
    spawn_sender -c 1 -ss -t topic.01        -a $ROUTER_B  -M
    spawn_sender -c 1  -sl -t queue.01       -a $ROUTER_B  -M
    spawn_sender -c 1  -sm -t linkroute.test -a $ROUTER_B  -M

    spawn_sender -c 1 -t multicast/test      -a $ROUTER_A  -M
    spawn_sender -c 1 -t closest/test        -a $ROUTER_A  -M
    spawn_sender -c 1 -sl -t balanced/test   -a $ROUTER_A  -M
    spawn_sender -c 1 -ss -t topic.01        -a $ROUTER_A  -M
    spawn_sender -c 1 -sl -t queue.01        -a $ROUTER_A  -M
    spawn_sender -c 1 -sm -t linkroute.test  -a $ROUTER_A  -M

    # non-default priority
    spawn_sender -c 1 -t multicast/test      -a $ROUTER_B  -p 1
    spawn_sender -c 1 -t closest/test        -a $ROUTER_B  -p 2
    spawn_sender -c 1 -sl -t balanced/test   -a $ROUTER_B  -p 3
    spawn_sender -c 1 -ss -t topic.01        -a $ROUTER_B  -p 5
    spawn_sender -c 1  -sl -t queue.01       -a $ROUTER_B  -p 6
    spawn_sender -c 1  -sm -t linkroute.test -a $ROUTER_B  -p 7

    spawn_sender -c 1 -t multicast/test      -a $ROUTER_A  -p 8
    spawn_sender -c 1 -t closest/test        -a $ROUTER_A  -p 9
    spawn_sender -c 1 -sl -t balanced/test   -a $ROUTER_A  -p 0
    spawn_sender -c 1 -ss -t topic.01        -a $ROUTER_A  -p 1
    spawn_sender -c 1 -sl -t queue.01        -a $ROUTER_A  -p 2
    spawn_sender -c 1 -sm -t linkroute.test  -a $ROUTER_A  -p 3


    

    echo "Waiting for senders to exit..."
    #echo "SENDER_PIDS=[$SENDER_PIDS]"
    wait $SENDER_PIDS

    sleep 2
    echo "Waiting for receivers to exit..."
    sleep 2
    #echo "RECEIVER_PIDS=[$RECEIVER_PIDS]"
    kill $RECEIVER_PIDS
    wait $RECEIVER_PIDS
done
#set +x

echo "TEST COMPLETE!"

qdstat -g -b $ROUTER_A >  qdstat-RouterA.txt
qdstat -l -b $ROUTER_A >> qdstat-RouterA.txt
qdstat -m -b $ROUTER_A >> qdstat-RouterA.txt


qdstat -g -b $ROUTER_B >  qdstat-RouterB.txt
qdstat -l -b $ROUTER_B >> qdstat-RouterB.txt
qdstat -m -b $ROUTER_B >> qdstat-RouterB.txt

