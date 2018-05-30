#!/bin/bash
#

set -x
PATH="$PATH:/home/kgiusti/work/github/ombt"

ombt2 --url amqp://localhost:15672 rpc-server --daemon
ombt2 --url amqp://localhost:15672 rpc-server --daemon

ombt2 --url amqp://localhost:25672 rpc-client --daemon
ombt2 --url amqp://localhost:25672 rpc-client --daemon
ombt2 --url amqp://localhost:25672 rpc-client --daemon
ombt2 --url amqp://localhost:25672 rpc-client --daemon

ombt2 --url amqp://localhost:25672 listener --daemon
ombt2 --url amqp://localhost:25672 listener --daemon

ombt2 --url amqp://localhost:15672 notifier --daemon
ombt2 --url amqp://localhost:15672 notifier --daemon
ombt2 --url amqp://localhost:15672 notifier --daemon


for (( len=1024 ; len<=65536 ; len=len+1023 )) ; do

    ombt2 --url amqp://localhost:15672 controller notify --events 100 --pause 0.10 --length $len
    ombt2 --url amqp://localhost:15672 controller rpc-call --calls 100 --pause 0.10 --length $len
    ombt2 --url amqp://localhost:15672 controller rpc-cast --calls 100 --pause 0.10 --length $len
    ombt2 --url amqp://localhost:15672 controller rpc-fanout --calls 100 --pause 0.10 --length $len

    ombt2 --url amqp://localhost:25672 controller notify --events 100 --pause 0.10 --length $len
    ombt2 --url amqp://localhost:25672 controller rpc-call --calls 100 --pause 0.10 --length $len
    ombt2 --url amqp://localhost:25672 controller rpc-cast --calls 100 --pause 0.10 --length $len
    ombt2 --url amqp://localhost:25672 controller rpc-fanout --calls 100 --pause 0.10 --length $len

done


ombt2 --url amqp://localhost:15672 controller shutdown
set +x

