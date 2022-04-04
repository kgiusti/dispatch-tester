#!/bin/bash
#
while true; do skmanage CREATE -b 127.0.0.1:5673 --type io.skupper.router.httpConnector --name "httpConnector/0.0.0.0:8801" address=closest/http1-bridge port=8801 host=127.0.0.1; sleep 5; skmanage QUERY -b 127.0.0.1:5673 --type io.skupper.router.httpConnector; skmanage DELETE -b 127.0.0.1:5673 --type io.skupper.router.httpConnector --name "httpConnector/0.0.0.0:8801"; sleep 0.5 ; done
