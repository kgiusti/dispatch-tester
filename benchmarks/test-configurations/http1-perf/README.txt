# To build the nginx container see the README
# in the nginx subdirectory

# fire up qdrouterd:

numactl --physcpubind=1,5,2,6 qdrouterd -c qdrouterd.conf &

# use 'hey' (https://github.com/rakyll/hey.git) to generate
# traffic to the router's listener port 8000.
# example: two client connections running for 20 seconds:
#
numactl --physcpubind=3,7 hey/bin/hey_linux_amd64 -c 2 -z 20s http://127.0.0.1:8000/t1K.html
numactl --physcpubind=3,7 hey/bin/hey_linux_amd64 -c 2 -z 20s http://127.0.0.1:8000/t10K.html
numactl --physcpubind=3,7 hey/bin/hey_linux_amd64 -c 2 -z 20s http://127.0.0.1:8000/t100K.html
numactl --physcpubind=3,7 hey/bin/hey_linux_amd64 -c 2 -z 20s http://127.0.0.1:8000/t1M.html

numactl --physcpubind=3,7 hey/bin/hey_linux_amd64 -c 2 -z 20s https://localhost:8100/t1K.html

# Better load testing tool is 'wrk' (https://github.com/wg/wrk.git)
# example:  two client connections using two threads running for 20 seconds:
#
wrk -c 2 -t 2 -d20s http://127.0.0.1:8000/t1M.html
