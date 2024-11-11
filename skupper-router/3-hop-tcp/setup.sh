#!/bin/bash
#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.

# Start routers and iperf3 server.
# You may have to adjust the numa CPU mapping for your system.
# Use teardown.sh to clean up these processes

rm -f *.log
numactl --physcpubind=1,9,2,10 skrouterd -c ./skrouterd-tcp-ingress.conf &
numactl --physcpubind=3,11,4,12 skrouterd -c ./skrouterd-tcp-egress.conf &
numactl --physcpubind=5,13,6,14 skrouterd -c ./skrouterd-interior.conf &
numactl --physcpubind=7 iperf3 --server --bind 127.0.0.1 --port 20002 &
