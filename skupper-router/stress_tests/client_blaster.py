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
# Stress client that hammers the server with pipelined requests
#

import socket
import sys

from time import sleep

def readline(conn):
    data = b''
    while True:
        octet = conn.recv(1)
        if octet == b'':
            return data
        data += octet
        if octet == b'\n':
            return data

def get_response(conn):
    # print("get response")

    content_length = 0

    # drain the response headers
    conn.setblocking(True)
    while True:
        # read all the headers
        line = readline(conn)
        # print(f"Received header: '{line.decode()}'")
        if line == b'':
            # remote closed the socket
            # print("Connection closed")
            return False
        if line == b'\r\n':
            break
        line = line.decode().lower().strip()
        if line.startswith("content-length:"):
            content_length = int(line.split(':')[1])

    # drain message body
    while content_length > 0:
        data = conn.recv(content_length)
        # print(f"Received body: '{data.decode()}'")
        if data == b'':
            # print("Connection closed")
            return False
        content_length -= len(data)

    return True

def run_client(host, port, count):
    get_req = 'GET /index.html HTTP/1.1\r\n'
    get_req += f'Host: {host}:{port}\r\n'
    get_req += '\r\n'
    get_req = get_req.encode()
    print(f"Request size: {len(get_req)} octets")

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as client:
        client.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        client.setblocking(True)
        while True:
            try:
                client.connect((host, port))
                break
            except ConnectionRefusedError:
                print("Conn refused... retrying")
                sleep(1)

        print(f"Sending {count} requests")
        for req in range(count):
            client.sendall(get_req)
            print(f"Sent {req} request")
        print(f"All {count} requests sent")

        print(f"Getting {count} responses")
        for resp in range(count):
            if get_response(client) is False:
                # client socket closed
                break
            print(f"Got {resp} response")
        print(f"{count} responses received")

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

