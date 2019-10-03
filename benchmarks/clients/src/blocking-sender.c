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

#define ADD_ANNOTATIONS 1

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
uint64_t not_accepted = 0;

bool pending_ack = false;         // true == waiting for ack

bool use_anonymous = false;       // use anonymous link if true
bool presettle = false;           // true = send presettled
bool add_timestamp = false;
bool add_annotations = false;
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
int64_t stop_ts;

int64_t ack_start_ts;
int64_t ack_stop_ts;

static struct timespec start_stall;
bool      stalled;
uint64_t  worse_stall;
uint64_t  total_stall;
int       stall_count;
uint64_t  sum_of_squares;
uint64_t  credit_grants;
int       grant_count;





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


// odd-length long string
const char big_string[] =
    "+"
    "0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789"
    "0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789"
    "0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789"
    "0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789"
    "0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789"
    "0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789"
    "0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789"
    "0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789"
    "0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789"
    "0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789";


static void add_message_annotations(pn_message_t *out_message)
{
    // just a bunch of dummy MA
    pn_data_t *annos = pn_message_annotations(out_message);
    pn_data_clear(annos);
    pn_data_put_map(annos);
    pn_data_enter(annos);

    pn_data_put_symbol(annos, pn_bytes(strlen("my-key"), "my-key"));
    pn_data_put_string(annos, pn_bytes(strlen("my-data"), "my-data"));

    pn_data_put_symbol(annos, pn_bytes(strlen("my-other-key"), "my-other-key"));
    pn_data_put_string(annos, pn_bytes(strlen("my-other-data"), "my-other-data"));

    // embedded map
    pn_data_put_symbol(annos, pn_bytes(strlen("my-map"), "my-map"));
    pn_data_put_map(annos);
    pn_data_enter(annos);
    pn_data_put_symbol(annos, pn_bytes(strlen("my-map-key1"), "my-map-key1"));
    pn_data_put_char(annos, 'X');
    pn_data_put_symbol(annos, pn_bytes(strlen("my-map-key2"), "my-map-key2"));
    pn_data_put_byte(annos, 0x12);
    pn_data_put_symbol(annos, pn_bytes(strlen("my-map-key3"), "my-map-key3"));
    pn_data_put_string(annos, pn_bytes(strlen("Are We Not Men?"), "Are We Not Men?"));
    pn_data_put_symbol(annos, pn_bytes(strlen("my-last-key"), "my-last-key"));
    pn_data_put_binary(annos, pn_bytes(sizeof(big_string), big_string));
    pn_data_exit(annos);

    pn_data_put_symbol(annos, pn_bytes(strlen("my-ulong"), "my-ulong"));
    pn_data_put_ulong(annos, 0xDEADBEEFCAFEBEEF);

    // embedded list
    pn_data_put_symbol(annos, pn_bytes(strlen("my-list"), "my-list"));
    pn_data_put_list(annos);
    pn_data_enter(annos);
    pn_data_put_string(annos, pn_bytes(sizeof(big_string), big_string));
    pn_data_put_double(annos, 3.1415);
    pn_data_put_short(annos, 1966);
    pn_data_exit(annos);

    pn_data_put_symbol(annos, pn_bytes(strlen("my-bool"), "my-bool"));
    pn_data_put_bool(annos, false);

    pn_data_exit(annos);
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

    if (add_annotations) {
        add_message_annotations(out_message);
    }

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


static bool send_msg(pn_link_t *sender)
{
    static long tag = 0;  // a simple tag generator
    int credit = pn_link_credit(sender);

    if (credit <= 0) {
        stalled = true;
        now_timespec(&start_stall);
        return false;
    }

    if (!pending_ack && (limit == 0 || count < limit)) {

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

        grant_count += 1;
        credit_grants += credit;

        if (!start_ts) start_ts = now_usec();

        ++count;
        ++tag;
        pn_delivery_t *delivery;
        delivery = pn_delivery(sender, pn_dtag((const char *)&tag, sizeof(tag)));

        if (add_timestamp) {
            generate_message(now_usec());
        }

        pn_link_send(sender, encode_buffer, encoded_data_size);
        pn_link_advance(sender);
        if (!presettle) {
            pending_ack = true;
        } else {
            pn_delivery_settle(delivery);
        }

        if (limit && count == limit) {
            stop_ts = now_usec();
            if (!pending_ack) {
                // not waiting for acks, so stop now
                stop = true;
                pn_reactor_wakeup(reactor);
            }
        }

        return true;
    }

    return false;
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
        send_msg(pn_event_link(event));
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
                pending_ack = false;
                ++acked;
                ++accepted;
                pn_delivery_settle(dlv);
                if (!ack_start_ts) {
                    ack_start_ts = now_usec();
                }
                break;
            case PN_REJECTED:
            case PN_RELEASED:
            case PN_MODIFIED:
            default:
                pending_ack = false;
                ++acked;
                ++not_accepted;
                pn_delivery_settle(dlv);
                if (!ack_start_ts) {
                    ack_start_ts = now_usec();
                }
                // fprintf(stderr, "Message not accepted - code: 0x%lX\n", (unsigned long)rs);
                break;
            }

            if (!limit || count < limit) {
                // send next message
                send_msg(pn_event_link(event));
            } else if (acked == limit) {
                // initiate clean shutdown of the endpoints
                stop = true;
                ack_stop_ts = now_usec();
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
  printf("-M      \tAdd dummy Message Annotations section [off]\n");
  exit(1);
}

int main(int argc, char** argv)
{
    int64_t  print_deadline = 0;

    /* command line options */
    opterr = 0;
    int c;
    while ((c = getopt(argc, argv, "ha:c:i:lns:t:uvM")) != -1) {
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
        case 'M': add_annotations = true; break;

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

    if (grant_count) {
        printf("  Avg credit grant: %.3f credits (%d grants total)\n",
               (double)credit_grants / (double)grant_count,
               grant_count);
    }

    if (not_accepted) {
        printf("Sent: %ld  Accepted: %ld Not Accepted: %ld\n", count, accepted, not_accepted);
        if (accepted + not_accepted != count) {
            printf("FAILURE! Sent: %ld  Acked: %ld\n", count, accepted + not_accepted);
        }
    }

    printf("  Start tx  -> stop tx:   %.3f msec\n"
           "  Start ack -> stop ack:  %.3f msec\n"
           "  Start tx  -> start ack: %.3f msec\n"
           "  Stop tx   -> stop ack:  %.3f msec\n"
           "  Start tx  -> stop ack:  %.3f msec\n",
           (stop_ts - start_ts) / 1000.0,
           (ack_stop_ts - ack_start_ts) / 1000.0,
           (ack_start_ts - start_ts) / 1000.0,
           (ack_stop_ts - stop_ts) / 1000.0,
           (ack_stop_ts - start_ts) / 1000.0);

    return 0;
}
