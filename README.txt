A dispatch test setup hardcoded to my particular environment.

Creates a small dispatch network:

     +--------------+            +------------------+
     |RouterA       |----------->|QPIDD Broker      |
     |127.0.0.1:7777|            | 127.0.0.1:5672/1 |
     +--------------+            +------------------+
         |
         |
         V
     +--------------+
     |RouterB       |
     |127.0.0.1:8888|
     +--------------+

* A link route is configured to the broker for the address prefix "Broker."
* SASL EXTERNAL is configured
* SSL is configured, and is used for client authentication

See HOWTO.txt for commands used to run the tests

See the examples directory for various configurations.
