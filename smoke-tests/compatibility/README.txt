

./setup.sh -p 0.35.0 -D 1.18.0 -d DISPATCH-1487-ma-compose-send -u https://github.com/kgiusti/dispatch/archive/refs/heads


Edge1 normal listener port 5672
Edge2 normal listener port 5673

Edge1 link route connector E1LinkRoute.# port 8888
Edge2 link route connector E2LinkRoute.# port 9999

~/work/dispatch/dispatch-tester/benchmarks/clients/src/server -a 0.0.0.0:8888 &
../../benchmarks/clients/src/throughput-sender -t E1LinkRoute.foo -a 127.0.0.1:5673 -c 10

~/work/dispatch/dispatch-tester/benchmarks/clients/src/server -a 0.0.0.0:9999 &
../../benchmarks/clients/src/throughput-sender -t E2LinkRoute.foo -a 127.0.0.1:5672 -c 10

../../benchmarks/clients/src/throughput-receiver -a 127.0.0.1:5672 -c 10000 &
../../benchmarks/clients/src/throughput-sender  -a 127.0.0.1:5673 -c 10000

../../benchmarks/clients/src/throughput-receiver -a 127.0.0.1:5673 -c 10000 &
../../benchmarks/clients/src/throughput-sender  -a 127.0.0.1:5672 -c 10000


