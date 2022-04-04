#!/bin/bash
#
podman run --rm --name nginx-http1-00 -d -p 8800:80 nginx-perf:1
podman run --rm --name nginx-http1-01 -d -p 8801:80 nginx-perf:1

rm -f tcp-listener-log.txt
skrouterd -c tcp-listener.conf &
rm -f tcp-connector-log.txt
skrouterd -c tcp-connector.conf &

