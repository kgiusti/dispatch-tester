iperf3 --server --bind 127.0.0.1 --port 20002
iperf3 --client 127.0.0.1 --port 20001 --parallel 2 --json --logfile /tmp/plano-5prme8rm/output.json --time 10 --omit 5


rm -f *.log ; skrouterd -c ./skrouterd-tcp-ingress.conf & skrouterd -c ./skrouterd-tcp-egress.conf & skrouterd -c ./skrouterd-interior-in.conf & skrouterd -c ./skrouterd-interior-out.conf &


MAIN
[SUM]   0.00-10.00  sec  15.7 GBytes  13.5 Gbits/sec                  receiver


1xWINDOW, w/4
[SUM]   0.00-10.00  sec  16.2 GBytes  13.9 Gbits/sec                  receiver
2023-11-09 14:25:12.092329 -0500 TCP_ADAPTOR (info) [C5] window close count 19668
2023-11-09 14:25:12.092349 -0500 TCP_ADAPTOR (info) [C4] window close count 19615


1xWINDOW, w/8 ack
[SUM]   0.00-10.00  sec  16.1 GBytes  13.8 Gbits/sec                  receiver
2023-11-09 14:21:39.176309 -0500 TCP_ADAPTOR (info) [C10] window close count 19375
2023-11-09 14:21:39.176529 -0500 TCP_ADAPTOR (info) [C11] window close count 19332


2XWINDOW, w/4 ack

[SUM]   0.00-10.00  sec  16.3 GBytes  14.0 Gbits/sec                  receiver
2023-11-09 14:30:13.227160 -0500 TCP_ADAPTOR (info) [C4] window close count 8259
2023-11-09 14:30:13.227189 -0500 TCP_ADAPTOR (info) [C5] window close count 8271


2XWINDOW, w/8 ack
[SUM]   0.00-10.00  sec  16.4 GBytes  14.1 Gbits/sec                  receiver
2023-11-09 14:14:42.391054 -0500 TCP_ADAPTOR (info) [C4] window close count 7895
2023-11-09 14:14:42.391081 -0500 TCP_ADAPTOR (info) [C5] window close count 7899










