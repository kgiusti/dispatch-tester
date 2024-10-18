Out of Memory Tester

Two AMQP clients that attempt to overload a routers memory.

oom-receiver - creates N receiving links that grant credit but do not
consume incoming message data. All links are stalled.

oom-sender - creates N sending links then streams a message with a
body of 2^31-1 octets over each link.

These clients will run until they are explicitly ^C (SIGQUIT).

Example:  assuming an skrouterd with an AMQP listener on port 5672:

./oom-receiver -l 500 &
./oom-sender -l 500

Warning: be careful! The resulting memory load on the skrouter may
cause your system undesired heartache.
