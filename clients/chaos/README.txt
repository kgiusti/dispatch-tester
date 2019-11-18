AMQP 1.0 Clients that do Very Bad Things

Assumes the following data model:

closest/mobile_1  - simple direct moble client address
multicast/topic_1 - multicast
closest/queue_1   - broker-based queue

address-loader
==============

Creates a large route table by creating subscription links with
associated mobile addresses.
