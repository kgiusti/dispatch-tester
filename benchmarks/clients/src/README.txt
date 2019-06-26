sender and receiver clients for benchmarking.

These clients are designed to send and receive as fast as possible.
The sender's rate is only limited by the amount of credit that has
been granted to it.

'credit stalls' - count the number of times the sender has stopped
sending due to running out of credit.  This is expected as the sender
will send until it runs out of credit.  What is interesting is how
much time the sender spends waiting for credit, and the average size
of the credit grant once credit becomes available.  For example, an
average stall of 1msec (totally made up btw) may be considered
acceptable if the average credit granted is close to the router's
linkCapacity (250 by default), but if the average credit grant is only
2 credits per 1msec, well, that's bad, ummkay?

Use the "-h" option for argument details.

Run the build.sh script to build the executables.  Expects
qpid-proton-c developer package(s) to be installed as well as libm.
You can use the BUILD_OPTS env variable to pass additional compiler
flags.  For example:

    BUILD_OPTS="-I/opt/kgiusti/include -L/opt/kgiusti/lib64" ./build.sh
