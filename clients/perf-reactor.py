#!/usr/bin/env python
#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
#

import optparse
import logging
import sys
import time
import pdb
import proton
from proton import Message, Url
from proton.reactor import Reactor, AtLeastOnce
from proton.handlers import CHandshaker, CFlowController


class Perfy:

    def __init__(self, target, count):
        self.message = Message()
        self.target = target if target is not None else "examples"
        # Use the handlers property to add some default handshaking
        # behaviour.
        self.handlers = [CHandshaker(), CFlowController()]
        self.conn = None
        self.ssn = None
        self.sender = None
        self.receiver = None
        self.count = count
        self._sends = count
        self.start_time = None
        self.stop_time = None
        self.last_send_time = None
        self.total_ack_latency = 0.0
        self.total_tx_latency = 0.0

    def _send_message(self, link):
        now = time.time()
        self.message.body = {'tx-timestamp': now}
        self.last_send_time = now
        dlv = link.send(self.message)

    def on_delivery(self, event):
        now = time.time()
        link = event.link
        if link.is_sender:
            dlv = event.delivery
            if dlv.settled:
                self.total_ack_latency += now - self.last_send_time
                dlv.settle()
                if self._sends:
                    self._send_message(link)
                else:
                    self.stop_time = now
                    link.close()
                    duration = self.stop_time - self.start_time
                    thru = self.count / duration
                    permsg = duration / self.count
                    ack = self.total_ack_latency / self.count
                    lat = self.total_tx_latency / self.count
                    print("Stats:\n"
                          " TX Avg Calls/Sec: %f Per Call: %f Ack Latency %f\n"
                          " RX Latency: %f" % (thru, permsg, ack, lat))
        else:
            msg = Message()
            dlv = msg.recv(link)
            if dlv:
                dlv.update(dlv.ACCEPTED)
                dlv.settle()
                self.total_tx_latency += now - msg.body['tx-timestamp']
                self._sends -= 1
                if self._sends == 0:
                    link.close()


    def on_link_remote_close(self, event):
        link = event.link
        link.close()
        if link.is_sender:
            self.ssn.close()
            self.conn.close()

    def on_connection_init(self, event):
        self.conn = event.connection
        self.ssn = self.conn.session()
        self.receiver = self.ssn.receiver("Perfy-RX")
        self.receiver.source.address = self.target
        self.conn.open()
        self.ssn.open()
        self.receiver.open()

    def on_link_remote_open(self, event):
        link = event.link
        if link.is_receiver:
            self.sender = self.ssn.sender("Perfy-TX")
            self.sender.target.address = self.target
            AtLeastOnce().apply(self.sender)
            self.sender.open()

    def on_link_flow(self, event):
        if self.start_time is None and self.sender.credit > 0:
            self.start_time = time.time()
            self._send_message(event.link)

    def on_transport_error(self, event):
        print event.transport.condition


class Program:

    def __init__(self, url, node, count):
        self.url = url
        self.node = node
        self.count = count

    def on_reactor_init(self, event):
        # You can use the connection method to create AMQP connections.

        # This connection's handler is the Send object. All the events
        # for this connection will go to the Send object instead of
        # going to the reactor. If you were to omit the Send object,
        # all the events would go to the reactor.
        event.reactor.connection_to_host(self.url.host, self.url.port,
                                         Perfy(self.node, self.count))



_usage = """Usage: %prog [options]"""
parser = optparse.OptionParser(usage=_usage)
parser.add_option("-a", dest="server", type="string",
                  default="amqp://0.0.0.0:5672",
                  help="The address of the server [amqp://0.0.0.0:5672]")
parser.add_option("--node", type='string', default='amq.topic',
                  help='Name of source/target node')
parser.add_option("--count", type='int', default=100,
                  help='Send N messages (send forever if N==0)')

opts, _ = parser.parse_args(args=sys.argv)
r = Reactor(Program(Url(opts.server), opts.node, opts.count))
r.run()
