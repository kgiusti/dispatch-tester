#!/bin/bash
#
while true; do CPIDS="" ; curl --max-time 60000 127.0.0.1:8000/t10M.html > /dev/null & CPIDS+="$! "; curl --max-time 60000 127.0.0.1:8000/t10M.html > /dev/null & wait $! $CPID ; done
