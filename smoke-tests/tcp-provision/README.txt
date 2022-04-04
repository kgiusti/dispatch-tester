Test provisioning and deprovisioning an tcp connector while running
an HTTP client.

Uses ports 5672, 5673, 10000, 8000, 8800, 8801

skmanage QUERY -b 127.0.0.1:5673 --type io.skupper.router.tcpConnector
