Mimic a router front end to a broker, but use a router running
standalone as a broker.  This can be used if qpidd or artemis is not
available for testing.

fake-qdrouterd.conf configures a standalone router listening on port
10000 (FakeRouter).

qdrouterd.conf configures a router to use FakeRouter as a waypoint for
the "benchmark" address.
