/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>

#include "proton/reactor.h"
#include "proton/message.h"
#include "proton/connection.h"
#include "proton/session.h"
#include "proton/link.h"
#include "proton/delivery.h"
#include "proton/event.h"
#include "proton/handlers.h"

/* Message latency receiver for use with latency-sender */

#define MAX_SIZE (1048576 * 2)  // large enough to buffer max message from latency-sender
char in_buffer[MAX_SIZE];

bool stop = false;

uint64_t min_latency = UINT64_MAX;
uint64_t max_latency = 0;
uint64_t total_latency = 0;
uint64_t sum_of_squares = 0;
uint64_t start_ts;  // start timestamp

int  credit_window = 1000;
char *source_address = "benchmark";  // name of the source node to receive from
char *host_address = "127.0.0.1:5672";
char *container_name = "BenchReceiver";

pn_connection_t *pn_conn;
pn_session_t *pn_ssn;
pn_link_t *pn_link;
pn_reactor_t *reactor;
pn_message_t *in_message;       // holds the current received message

uint64_t count = 0;
uint64_t limit = 0;   // if > 0 stop after limit messages arrive


// microseconds per second
#define USECS_PER_SECOND 1000000

// return wallclock time in microseconds since Epoch
//
static uint64_t now_usec(void)
{
    struct timespec ts;
    int rc = clock_gettime(CLOCK_REALTIME, &ts);
    if (rc) {
        perror("clock_gettime failed");
        exit(errno);
    }

    return (USECS_PER_SECOND * (uint64_t)ts.tv_sec) + (ts.tv_nsec / 1000);
}


static void signal_handler(int signum)
{
    signal(SIGINT,  SIG_IGN);
    signal(SIGQUIT, SIG_IGN);

    switch (signum) {
    case SIGINT:
    case SIGQUIT:
        stop = true;
        if (reactor) pn_reactor_wakeup(reactor);
        break;
    default:
        break;
    }
}


// Called when reactor exits to clean up app_data
//
static void delete_handler(pn_handler_t *handler)
{
    if (in_message) {
        pn_message_free(in_message);
        in_message = NULL;
    }
}


/* Process each event posted by the reactor.
 */
static void event_handler(pn_handler_t *handler,
                          pn_event_t *event,
                          pn_event_type_t type)
{
    switch (type) {

    case PN_CONNECTION_INIT: {
        // Create and open all the endpoints needed to send a message
        //
        in_message = pn_message();
        pn_connection_open(pn_conn);
        pn_ssn = pn_session(pn_conn);
        pn_session_open(pn_ssn);
        pn_link = pn_receiver(pn_ssn, "MyReceiver");
        pn_terminus_set_address(pn_link_source(pn_link), source_address);
        pn_link_open(pn_link);
        // cannot receive without granting credit:
        pn_link_flow(pn_link, credit_window);
    } break;

    case PN_DELIVERY: {
        // A message has been received
        //
        if (limit && count == limit) break;

        pn_delivery_t *dlv = pn_event_delivery(event);
        if (pn_delivery_readable(dlv) && !pn_delivery_partial(dlv)) {
            // A full message has arrived
            uint64_t in_ts = now_usec();
            count += 1;
            if (pn_delivery_pending(dlv) > MAX_SIZE) {
                fprintf(stderr,
                        "Error: unable to buffer incoming message - to large! (max=%d bytes)\n",
                        MAX_SIZE);
                exit(-1);
            }

            char *ptr = &in_buffer[0];
            size_t avail = MAX_SIZE;
            ssize_t len;

            do {
                len = pn_link_recv(pn_delivery_link(dlv), ptr, avail);
                if (len > 0) {
                    ptr += len;
                    avail -= len;
                }
            } while (len > 0);

            if (len != PN_EOS) {
                fprintf(stderr,
                        "Error: pn_link_recv() failed, error: %zd\n",
                        len);
                exit(-1);
            }

            // now that the data has been read we can settle.  We want to do
            // this asap as the sender is waiting for the accept (and running a timer!)
            pn_delivery_update(dlv, PN_ACCEPTED);
            pn_delivery_settle(dlv);  // dlv is now freed

            // decode the raw data into the message instance
            pn_message_clear(in_message);
            int rc = pn_message_decode(in_message, in_buffer, MAX_SIZE - avail);
            if (rc != PN_OK) {
                fprintf(stderr,
                        "Error: pn_message_decode() failed, error: %d\n",
                        rc);
                exit(-1);
            }

            // extract the sender's timestamp
            // note: see latency-sender.c for the format of the message body
            pn_data_t *body = pn_message_body(in_message);
            pn_data_rewind(body);
            if (!pn_data_next(body) ||
                pn_data_get_list(body) < 1 ||
                !pn_data_enter(body) ||
                !pn_data_next(body) ||
                pn_data_type(body) != PN_ULONG) {

                fprintf(stderr,
                        "Error: cannot parse message body - invalid format\n");
                exit(-1);
            }

            uint64_t send_ts = pn_data_get_ulong(body);
            if (send_ts == 0 || send_ts > in_ts) {
                fprintf(stderr,
                        "Error: invalid transmit timestamp (clocks not synchronized?)\n");
                exit(-1);
            }

            uint64_t latency = in_ts - send_ts;
            if (latency < min_latency) min_latency = latency;
            if (latency > max_latency) max_latency = latency;
            total_latency += latency;
            sum_of_squares += (latency * latency);

            if (limit && count == limit) {
                stop = true;
                pn_reactor_wakeup(reactor);
            } else if (pn_link_credit(pn_link) <= credit_window/2) {
                // Grant enough credit to bring it up to CAPACITY:
                int grant = credit_window - pn_link_credit(pn_link);
                if (limit && (limit - count) < grant) {
                    // don't grant more than we want to receive
                    grant = limit - count;
                }
                if (grant) {
                    //fprintf(stdout, "Granting credit %d\n", grant);
                    pn_link_flow(pn_link, credit_window - pn_link_credit(pn_link));
                }
            }
        }
    } break;

    default:
        break;
    }
}

static void usage(void)
{
  printf("Usage: receiver <options>\n");
  printf("-a      \tThe host address [%s]\n", host_address);
  printf("-c      \tExit after N messages arrive (0 == run forever) [%"PRIu64"]\n", limit);
  printf("-i      \tContainer name [%s]\n", container_name);
  printf("-s      \tSource address [%s]\n", source_address);
  printf("-w      \tCredit window [%d]\n", credit_window);
  exit(1);
}


int main(int argc, char** argv)
{
    /* create a handler for the connection's events.
     */
    pn_handler_t *handler = pn_handler_new(event_handler, 0, delete_handler);
    pn_handler_add(handler, pn_handshaker());

    /* command line options */
    opterr = 0;
    int c;
    while((c = getopt(argc, argv, "i:a:s:hw:c:")) != -1) {
        switch(c) {
        case 'h': usage(); break;
        case 'a': host_address = optarg; break;
        case 'c':
            if (sscanf(optarg, "%"PRIu64, &limit) != 1)
                usage();
            break;
        case 'i': container_name = optarg; break;
        case 's': source_address = optarg; break;
        case 'w':
            if (sscanf(optarg, "%d", &credit_window) != 1 || credit_window <= 0)
                usage();
            break;

        default:
            usage();
            break;
        }
    }

    signal(SIGQUIT, signal_handler);
    signal(SIGINT,  signal_handler);

    reactor = pn_reactor();
    pn_conn = pn_reactor_connection(reactor, handler);

    // the container name should be unique for each client
    pn_connection_set_container(pn_conn, container_name);
    pn_connection_set_hostname(pn_conn, host_address);

    // periodic wakeup to print current stats
    pn_reactor_set_timeout(reactor, 10000);

    pn_reactor_start(reactor);

    while (pn_reactor_process(reactor)) {
        if (stop) {
            // close the endpoints this will cause pn_reactor_process() to
            // eventually break the loop
            if (pn_link) pn_link_close(pn_link);
            if (pn_ssn) pn_session_close(pn_ssn);
            pn_connection_close(pn_conn);
        }
    }


    fprintf(stdout, "RX: Received: %"PRIu64" messages\n", count);
    if (count) {
        uint64_t lmean = total_latency / count;
        uint64_t isos = sum_of_squares / count;
        isos = isos - (lmean * lmean);
        double std_dev = sqrt((double)isos);
        fprintf(stdout,
                "RX:  Latency:  Avg: %.3f msec Max: %.3f msec Min: %.3f, Std deviation: %.3f msec\n",
               (double)lmean / 1000.0,
               (double)max_latency / 1000.0,
               (double)min_latency / 1000.0,
               std_dev / 1000.0);
    }

    return 0;
}
