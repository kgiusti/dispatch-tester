A router fronting access to the Artemis broker.

Clients connect to the router on port 5672.

broker.xml configures Artemis on port 10000 with a queue 'benchmark'
defined. Easiest things is to create a new artemis instance and then
copy this into the <artemis-instance-home>/etc

Notes for the Artemis Noob:

1) pull down the tar file from https://activemq.apache.org/components/artemis/download/

2) unpack in some directory

3) set $ARTEMIS_HOME to <download-dir>/apache-artemis-XX.XX.XX

4) make a temporary directory for a new broker instance:
   mkdir broker

5) Create a new broker:
   $ARTEMIS_HOME/bin/artemis create mybroker

6) copy broker.xml into mybroker/etc/ (overwrites default config)

7) cd mybroker; ./bin/artemis run &

8) Profit!!!
