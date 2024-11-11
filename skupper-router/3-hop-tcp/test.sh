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

# Run iperf3 client with a variable number of TCP streams

STREAMS=${1:-2}
DURATION=${2:-7}
RPIDS=`pidof skrouterd | tr " " ","`
set -x

# Uncomment ## lines for flamegraph recording
##perf record --freq 997 --call-graph fp --pid $RPIDS sleep $DURATION &
##PERF_PID=$!

numactl --physcpubind=0 iperf3 --client 127.0.0.1 --port 20001 --parallel $STREAMS --time $DURATION --omit 2

##wait $PERF_PID
##perf script report flamegraph
