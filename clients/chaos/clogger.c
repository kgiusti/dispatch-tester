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

bool stop = false;

uint64_t limit = 1;               // # messages to send
uint64_t count = 0;               // # sent
uint64_t acked = 0;               // # of received acks
uint64_t accepted = 0;
uint64_t not_accepted = 0;

bool use_anonymous = false;       // use anonymous link if true
bool presettle = false;           // true = send presettled
uint32_t body_length = 65565;     // # bytes in vbin32 payload

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

pn_delivery_t *delivery;   // current in-flight delivery
uint8_t out_data[1024];    // binary data for message payload (body data)
uint32_t  byte_count;      // number of body data bytes written out link


#define AMQP_MSG_HEADER     0x70
#define AMQP_MSG_PROPERTIES 0x73
#define AMQP_MSG_DATA       0x75

const uint8_t msg_header = {
    0x00,  // begin described type
    0x53,  // 1 byte ulong type
    0x70,  // HEADER section
    0x45,  // empty list
    0x00,  // begin described type
    0x53,  // 1 byte ulong type
    0x73,  // PROPERTIES section
    0x45,  // empty list
    0x00,  // begin described type
    0x53,  // 1 byte ulong type
    0x77,  // AMQP Value BODY section
    0xb0,  // Binary uint32 length
    // 4 bytes for length here
    // start of data...
};


bool send_message(pn_link_t *sender)
{
    static const uint8_t zero_block[1024] = {0};
    static uint32_t bytes_sent = 0;

    if (!delivery) {  // start a new delivery;
        static long tag = 0;  // a simple tag generator
        delivery = pn_delivery(sender,
                               pn_dtag((const char *)&tag, sizeof(tag)));
        ++tag;

        // send the message header
        ssize_t rc = pn_link_send(sender, msg_header, sizeof(msg_header));
        assert(rc == sizeof(msg_header));

        // add the vbin32 length (in network order!!!)
        uint32_t len = htonl(body_length);
        rc == pn_link_send(sender, &len, sizeof(len));
        assert(rc == sizeof(len));
    }

    while (bytes_sent < body_length) {
        uint32_t remaining = body_length - bytes_sent;
        size_t len = (remaining < sizeof(zero_block)) ? remaining : sizeof(zero_block);
        ssize_t rc = pn_link_send(sender, zero_block, len);
        assert(rc >= 0);
        bytes_sent += rc;
        if (rc < len)
            break;
    }

    return bytes_sent == body_length;  // true if done sending message
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
        pn_link_t *sender = pn_event_link(event);
        int credit = pn_link_credit(sender);

        while (credit > 0 && (limit == 0 || count < limit)) {
            --credit;
            ++count;
            pn_delivery_t *delivery;

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
    } break;

    case PN_DELIVERY: {
        static long last_tag = -1;
        pn_delivery_t *dlv = pn_event_delivery(event);
        if (pn_delivery_updated(dlv)) {
            uint64_t rs = pn_delivery_remote_state(dlv);

            pn_delivery_tag_t raw_tag = pn_delivery_tag(dlv);
            long this_tag;
            memcpy((char *)&this_tag, raw_tag.start, sizeof(this_tag));

            switch (rs) {
            case PN_RECEIVED:
                // This is not a terminal state - it is informational, and the
                // peer is still processing the message.
                break;
            case PN_ACCEPTED:
                ++acked;
                ++accepted;

                if (this_tag != last_tag + 1) {
                    fprintf(stderr, "!!!!!!!!!!!!!!!!!!    UNEXPECTED TAG: 0x%lu (0x%lu)\n   !!!!!!!!!!!!!!!!!!!!",
                            this_tag, last_tag + 1);
                    exit(-1);
                }
                last_tag = this_tag;

                pn_delivery_settle(dlv);
                if (!ack_start_ts) {
                    ack_start_ts = now_usec();
                }
                break;
            case PN_REJECTED:
            case PN_RELEASED:
            case PN_MODIFIED:
            default:
                ++acked;
                ++not_accepted;
                pn_delivery_settle(dlv);
                fprintf(stderr, "Message not accepted - code: 0x%lX\n", (unsigned long)rs);
                break;
            }

            if (limit && acked == limit) {
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
  printf("-s      \tBody size in bytes [%d]\n", body_size);
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
            if (sscanf(optarg, "%"PRIu32, &body_size) != 1)
                usage();
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
