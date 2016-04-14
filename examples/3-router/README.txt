A 3 router mesh configuration
==============================

Topology and Public addressing
------------------------------

     +--------------+            +------------------+
     |RouterA       |----------->|RouterB           |
     |127.0.0.1:5672|            | 127.0.0.1:8888   |
     +--------------+            +------------------+
         |                           ^
         |                           |
         V                           |
     +--------------+                |
     |RouterC       |                |
     |127.0.0.1:9999|<---------------+
     +--------------+

Arrow directionality shows connector-->listener

Internal (inter-router) addressing
----------------------------------

A->B:127.0.0.1:20200
A->C:127.0.0.1:20300
B->C:127.0.0.1:20300

B and C have a single inter-router listener on ports 20200 and 20300 respectively.
A has two inter-router connectors to contact B and C on those listener ports.
B also connects to C using C's inter-router listener.

Running the routers
-------------------

qdrouterd -c RouterA/etc/qpid-dispatch/qdrouterd.conf &
qdrouterd -c RouterB/etc/qpid-dispatch/qdrouterd.conf &
qdrouterd -c RouterC/etc/qpid-dispatch/qdrouterd.conf &

qdstat -n  (goes to router A)
qdstat -n -b localhost:8888  (goes to router B)
qdstat -n -b localhost:9999  (goes to router C)

** A Note about SASL configuration **

qdrouterd searches for its SASL configuration in the following order:

1) using the path specified in the container configuration item saslConfigPath
2) if saslConfigPath is *not* configured it checks the default locations [/etc/qpid-dispatch or /usr/local/etc/qpid-dispatch]
3) failing that it will use the system defaults!

The example sasl configuration in ./sasl/qdrouterd.conf configures a
simple ANONYMOUS mech.  There is no need to create a sasldb if this
configuration is used.

It is *recommended* to start with this SASL configuration.  You *MUST*
modify all three router configurations to set the correct path via the
saslConfigPath container configuration value.  See the qdrouterd.conf
files for details.

