iperf3 --server --bind 127.0.0.1 --port 20002 &
iperf3 --client 127.0.0.1 --port 20001 --parallel 2 --json --logfile /tmp/plano-5prme8rm/output.json --time 10 --omit 5


rm -f *.log; skrouterd -c ./skrouterd-tcp-ingress.conf & skrouterd -c ./skrouterd-tcp-egress.conf & skrouterd -c ./skrouterd-interior.conf &

MAIN
[SUM]   0.00-10.00  sec  18.0 GBytes  15.5 Gbits/sec                  receiver

cpu 184 egress
cpu 160 interior
cpu 118 ingress

WINDOW
[SUM]   0.00-10.00  sec  19.6 GBytes  16.8 Gbits/sec                  receiver

cpu 163
cpu 150
cpu 118


2xWINDOW
[SUM]   0.00-10.00  sec  20.0 GBytes  17.2 Gbits/sec                  receiver

