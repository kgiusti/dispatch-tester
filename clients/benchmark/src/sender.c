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

#define BOOL2STR(b) ((b)?"true":"false")

#define BODY_SIZE_SMALL  100
#define BODY_SIZE_MEDIUM 2000
#define BODY_SIZE_LARGE  60000  // NOTE: receiver.c max in buffer size = 64KB

char _payload[BODY_SIZE_LARGE] = {0};
pn_bytes_t body_data = {
    .size  = 0,
    .start = _payload,
};

bool stop = false;

uint64_t limit = 1;               // # messages to send
uint64_t count = 0;               // # sent
uint64_t acked = 0;               // # of received acks
uint64_t accepted = 0;

bool use_anonymous = false;       // use anonymous link if true
bool presettle = false;           // true = send presettled
bool add_timestamp = false;
int body_size = BODY_SIZE_SMALL;

// buffer for encoded message
char *encode_buffer = NULL;
size_t encode_buffer_size = 0;    // size of malloced memory
size_t encoded_data_size = 0;     // length of encoded content

char *target_address = "benchmark";
char *host_address = "127.0.0.1:5672";
char *container_name = "BenchSender";

pn_connection_t *pn_conn;
pn_session_t *pn_ssn;
pn_link_t *pn_link;
pn_reactor_t *reactor;
pn_message_t *out_message;

int64_t start_ts;

// microseconds per second
#define USECS_PER_SECOND 1000000

// return wallclock time in microseconds since Epoch
//
static int64_t now_usec(void)
{
    struct timespec ts;
    int rc = clock_gettime(CLOCK_REALTIME, &ts);
    if (rc) {
        perror("clock_gettime failed");
        exit(errno);
    }

    return (USECS_PER_SECOND * (int64_t)ts.tv_sec) + (ts.tv_nsec / 1000);
}

static void now_timespec(struct timespec *ts)
{
    int rc = clock_gettime(CLOCK_MONOTONIC, ts);
    if (rc) {
        perror("clock_gettime failed");
        exit(errno);
    }
}

static int64_t diff_timespec_usec(const struct timespec *start,
                                  const struct timespec *end)
{
    return (end->tv_sec - start->tv_sec) * USECS_PER_SECOND
        + ((((end->tv_nsec - start->tv_nsec)) + 500) / 1000);
}


void generate_message(int64_t now_usec)
{
    if (!out_message) {
        out_message = pn_message();
    }
    pn_message_set_address(out_message, target_address);

    pn_data_t *body = pn_message_body(out_message);
    pn_data_clear(body);

    pn_data_put_list(body);
    pn_data_enter(body);

    pn_data_put_long(body, now_usec);

    // block of 0s - body_size - long size bytes
    body_data.size = body_size - 8;
    pn_data_put_binary(body, body_data);

    pn_data_exit(body);

    // now encode it

    pn_data_rewind(pn_message_body(out_message));
    if (!encode_buffer) {
        encode_buffer_size = body_size + 512;
        encode_buffer = malloc(encode_buffer_size);
    }

    int rc = 0;
    size_t len = encode_buffer_size;
    do {
        rc = pn_message_encode(out_message, encode_buffer, &len);
        if (rc == PN_OVERFLOW) {
            free(encode_buffer);
            encode_buffer_size *= 2;
            encode_buffer = malloc(encode_buffer_size);
            len = encode_buffer_size;
        }
    } while (rc == PN_OVERFLOW);

    if (rc) {
        perror("buffer encode failed");
        exit(-1);
    }

    encoded_data_size = len;
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


static void delete_handler(pn_handler_t *handler)
{
    free(encode_buffer);
    pn_message_free(out_message);
}


static struct timespec start_stall;
bool      stalled;
uint64_t  worse_stall;
uint64_t  total_stall;
int       stall_count;
uint64_t  sum_of_squares;

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
        pn_connection_open(pn_conn);
        pn_session_t *pn_ssn = pn_session(pn_conn);
        pn_session_open(pn_ssn);
        pn_link_t *pn_link = pn_sender(pn_ssn, "MySender");
        if (!use_anonymous) {
            pn_terminus_set_address(pn_link_target(pn_link), target_address);
        }
        pn_link_open(pn_link);

        acked = count;
        generate_message(now_usec());

    } break;

    case PN_LINK_FLOW: {
        // the remote has given us some credit, now we can send messages
        //
        if (stalled) {
            struct timespec end;
            now_timespec(&end);
            int64_t diff = diff_timespec_usec(&start_stall, &end);
            if (diff > 0) {
                stall_count += 1;
                total_stall += (uint64_t)diff;
                sum_of_squares += (uint64_t)(diff * diff);
                if (diff > worse_stall) worse_stall = (uint64_t)diff;
            }
            stalled = false;
        }
        static long tag = 0;  // a simple tag generator
        pn_link_t *sender = pn_event_link(event);
        int credit = pn_link_credit(sender);
        if (credit && !start_ts) start_ts = now_usec();
        while (credit > 0 && (limit == 0 || count < limit)) {
            --credit;
            ++count;
            ++tag;
            pn_delivery_t *delivery;
            delivery = pn_delivery(sender,
                                   pn_dtag((const char *)&tag, sizeof(tag)));
            if (add_timestamp) {
                generate_message(now_usec());
            }

            pn_link_send(sender, encode_buffer, encoded_data_size);
            pn_link_advance(sender);
            if (presettle) {
                pn_delivery_settle(delivery);
                if (limit && count == limit) {
                    // no need to wait for acks
                    stop = true;
                    pn_reactor_wakeup(reactor);
                }
            }
        }

        if (credit == 0) {
            stalled = true;
            now_timespec(&start_stall);
        }

    } break;

    case PN_DELIVERY: {
        pn_delivery_t *dlv = pn_event_delivery(event);
        if (pn_delivery_updated(dlv)) {
            uint64_t rs = pn_delivery_remote_state(dlv);
            switch (rs) {
            case PN_RECEIVED:
                // This is not a terminal state - it is informational, and the
                // peer is still processing the message.
                break;
            case PN_ACCEPTED:
                ++acked;
                ++accepted;
                pn_delivery_settle(dlv);
                break;
            case PN_REJECTED:
            case PN_RELEASED:
            case PN_MODIFIED:
            default:
                ++acked;
                pn_delivery_settle(dlv);
                fprintf(stderr, "Message not accepted - code:%lu\n", (unsigned long)rs);
                break;
            }

            if (limit && acked == limit) {
                // initiate clean shutdown of the endpoints
                stop = true;
                pn_reactor_wakeup(reactor);
            }
        }
    } break;

    default:
        break;
    }
}

static void usage(void)
{
  printf("Usage: sender <options>\n");
  printf("-a      \tThe host address [%s]\n", host_address);
  printf("-c      \t# of messages to send, 0 == nonstop [%"PRIu64"]\n", limit);
  printf("-i      \tContainer name [%s]\n", container_name);
  printf("-l      \tAdd timestamp [%s]\n", BOOL2STR(add_timestamp));
  printf("-n      \tUse an anonymous link [%s]\n", BOOL2STR(use_anonymous));
  printf("-s      \tBody size in bytes ('s'=%d 'm'=%d 'l'=%d) [%d]\n",
         BODY_SIZE_SMALL, BODY_SIZE_MEDIUM, BODY_SIZE_LARGE, body_size);
  printf("-t      \tTarget address [%s]\n", target_address);
  printf("-u      \tSend all messages presettled [%s]\n", BOOL2STR(presettle));
  printf("-v      \tPrint periodic status messages [off]\n");
  exit(1);
}

int main(int argc, char** argv)
{
    int64_t  print_deadline = 0;

    /* command line options */
    opterr = 0;
    int c;
    while ((c = getopt(argc, argv, "ha:c:i:lns:t:uv")) != -1) {
        switch(c) {
        case 'h': usage(); break;
        case 'a': host_address = optarg; break;
        case 'c':
            if (sscanf(optarg, "%"PRIu64, &limit) != 1)
                usage();
            break;
        case 'i': container_name = optarg; break;
        case 'l': add_timestamp = true; break;
        case 'n': use_anonymous = true; break;
        case 's':
            switch (optarg[0]) {
            case 's': body_size = BODY_SIZE_SMALL; break;
            case 'm': body_size = BODY_SIZE_MEDIUM; break;
            case 'l': body_size = BODY_SIZE_LARGE; break;
            default:
                usage();
            }
            break;
        case 't': target_address = optarg; break;
        case 'u': presettle = true; break;
        case 'v': print_deadline = now_usec() + (10 * USECS_PER_SECOND); break;

        default:
            usage();
            break;
        }
    }

    signal(SIGQUIT, signal_handler);
    signal(SIGINT,  signal_handler);

    pn_handler_t *handler = pn_handler_new(event_handler, 0, delete_handler);
    pn_handler_add(handler, pn_handshaker());

    reactor = pn_reactor();
    pn_conn = pn_reactor_connection(reactor, handler);

    // the container name should be unique for each client
    pn_connection_set_container(pn_conn, container_name);
    pn_connection_set_hostname(pn_conn, host_address);

    // wait up to 10 seconds for activity before returning from
    // pn_reactor_process()
    pn_reactor_set_timeout(reactor, 10000);

    pn_reactor_start(reactor);

    bool printed = false;
    while (pn_reactor_process(reactor)) {
        if (stop) {
            if (!printed && count) {
                int64_t now = now_usec();
                double duration = (double)(now - start_ts) / 1000000.0;
                if (duration == 0.0) duration = 0.0010;  // zero divide hack
                printf("  %.3f: msgs sent=%"PRIu64" msgs/sec=%12.3f\n",
                       duration, count, (double)count/duration);
                printed = true;
            }

            // close the endpoints this will cause pn_reactor_process() to
            // eventually break the loop
            if (pn_link) pn_link_close(pn_link);
            if (pn_ssn) pn_session_close(pn_ssn);
            pn_connection_close(pn_conn);
        } else if (print_deadline && now_usec() >= print_deadline) {
            printf("  -> sent: %"PRIu64" acked: %"PRIu64"  (%"PRIu64" accepted) capacity: %d\n",
                   count, acked, accepted, pn_link_credit(pn_link));
            print_deadline = now_usec() + (10 * USECS_PER_SECOND);
        }
    }

    if (stall_count) {
        uint64_t imean = total_stall / stall_count;
        uint64_t isos = sum_of_squares / stall_count;
        isos = isos - (imean * imean);
        double std_dev = sqrt((double)isos);
        printf("\n%d credit stalls occurred\n", stall_count);
        printf("   Avg stall %.3f msec Max %.3f msec (std dev %.3f msec)\n",
               (double)imean / 1000.0,
               (double)worse_stall / 1000.0,
               (double)std_dev / 1000.0);
    }

    return 0;
}
