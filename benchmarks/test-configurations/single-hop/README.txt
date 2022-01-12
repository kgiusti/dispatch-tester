Standalone router configuration.

Clients connect to port 5672.  Example:

    $ ./receiver -c 500000 & sleep 3; ./sender -c 500000

link route:

    $ throughput-receiver -c 3000000 -S -a 0.0.0.0:9999 -s benchmark &
    $ sleep 6; throughput-sender -c 3000000 -t benchmark
    
