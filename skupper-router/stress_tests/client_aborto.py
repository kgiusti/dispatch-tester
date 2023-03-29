#!/usr/bin/env python3
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
# under the License
#

#
# Stress client that aborts pending requests
#

import socket
import sys

from time import sleep

def run_client(host, port, count):
    get_req = 'GET /t10M.html HTTP/1.1\r\n'
    get_req += f'Host: {host}:{port}\r\n'
    get_req += '\r\n'
    get_req = get_req.encode()

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as client:
        client.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        client.settimeout(1.0)
        while True:
            try:
                client.connect((host, port))
                break
            except ConnectionRefusedError:
                print("Conn refused... retrying")
                sleep(1)

        #print(f"Sending[{get_req.decode()}]")
        for _ in range(count):
            client.sendall(get_req)
        #print("SENT")
        try:
            _ = client.recv(1)
        except socket.timeout:
            pass
        #print(f"RECV={xx}")
        client.shutdown(socket.SHUT_RDWR)
        client.close()

def main(argv):
    count = 251
    if len(argv) < 3 or len(argv) > 4:
        print(f"Usage: {argv[0]} host port [count]")
        return 1
    if len(argv) == 4:
        count = int(argv[3])

    try:
        while True:
            run_client(argv[1], int(argv[2]), count)
    except KeyboardInterrupt:
        print("Done")
        return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv))

