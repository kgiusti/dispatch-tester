#!/bin/bash
#
podman run --rm --name nginx-http1-00 -d -p 8800:80 nginx-perf:1
podman run --rm --name nginx-http1-01 -d -p 8801:80 nginx-perf:1

rm -f http1-listener-log.txt
skrouterd -c http1-listener.conf &
rm -f http1-connector-log.txt
skrouterd -c http1-connector.conf &

