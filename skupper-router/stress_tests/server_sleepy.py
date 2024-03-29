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
# Slow consuming HTTP server
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

def handle_requests(conn, delay):
    # print("Handle request")

    while True:
        # drain the request headers
        conn.setblocking(True)
        while True:
            # read all the headers
            line = readline(conn)
            # print(f"Received {line.decode()}")
            if line == b'':
                # remote closed the socket
                conn.close()
                # print("Connection closed")
                return
            if line == b'\r\n':
                break

        # drain the body (if it exists). Use a long timeout to simulate a slow
        # response
        conn.settimeout(delay)
        try:
            while True:
                rc = conn.recv(4096)
                # print(f"Received {rc.decode()}")
                if rc == b'':
                    # remote closed the socket
                    conn.close()
                    # print("Connection closed")
                    return
        except socket.timeout:
            pass
        # print("Send response")
        conn.sendall(b"HTTP/1.1 404 Not Found\r\n")
        conn.sendall(b"content-length: 0\r\n\r\n")

def main(argv):
    if len(argv) < 3 or len(argv) > 4:
        print(f"Usage: {argv[0]} host port [delay]")
        return 1
    delay = 5.0
    if len(argv) == 4:
        delay = float(argv[3])

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        listener.bind((argv[1], int(argv[2])))
        listener.setblocking(True)
        listener.listen(100)

        try:
            while True:
                conn, _ = listener.accept()
                handle_requests(conn, delay)
        except KeyboardInterrupt:
            print("Done")
            return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv))
