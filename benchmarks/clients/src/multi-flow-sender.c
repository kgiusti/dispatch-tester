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

/* An asynchronous sender for testing messaging throughput
 * This client starts a timer before sending the first message.  The timer is
 * stopped when the last acknowledgement arrives.  Througput is computed by
 * dividing this duration by the number of sent messages
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
#include "proton/transport.h"

const size_t buffer_size = 512;
const size_t max_frame = 16 * 1024;

#define BOOL2STR(b) ((b)?"true":"false")

#define BODY_SIZE_SMALL  buffer_size
#define BODY_SIZE_MEDIUM max_frame
#define BODY_SIZE_LARGE  (10 * max_frame)
#define BODY_SIZE_WUMBO  1048576

bool stop = false;

uint64_t limit = 1;               // # messages to send
uint64_t count = 0;               // # sent
uint64_t acked = 0;               // # of received acks
uint64_t accepted = 0;
uint64_t not_accepted = 0;

bool use_anonymous = false;       // use anonymous link if true
int body_size = BODY_SIZE_SMALL;

// buffer for encoded message
char *encode_buffer = NULL;
size_t encode_buffer_size = 0;    // size of malloced memory
size_t encoded_data_size = 0;     // length of encoded content

char *target_address = "test-throughput";
char *host_address = "127.0.0.1:5672";
char *container_name = "ThroughputSender";

pn_connection_t *pn_conn;
pn_session_t *pn_ssn;
pn_link_t *pn_link;
pn_reactor_t *reactor;
pn_message_t *out_message;

uint64_t start_ts;
uint64_t stop_ts;

typedef struct {
    uint32_t total_frames;
    uint32_t frame_counter;
    uint8_t  frame_buf[
} dlv_context_t;

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


void generate_message()
{
    if (!out_message) {
        out_message = pn_message();
    }
    pn_message_set_address(out_message, target_address);

    pn_data_t *body = pn_message_body(out_message);
    pn_data_clear(body);

    pn_data_put_list(body);
    pn_data_enter(body);

    // body is of 0s
    body_data.size = body_size;
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
        if (!stop_ts) stop_ts = now_usec();
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
        generate_message();

    } break;

    case PN_CONNECTION_BOUND: {
        pn_transport_t *tport = pn_event_transport(event);
        pn_transport_set_max_frame(tport, (uint32_t)max_frame);
    } break;

    case PN_LINK_FLOW: {
        // the remote has given us some credit, now we can send messages
        //
        static long tag = 0;  // a simple tag generator
        pn_link_t *sender = pn_event_link(event);
        //pn_session_t *_ssn = pn_link_session(sender);
        int credit = pn_link_credit(sender);

        if (credit < 1) break;

        
        if (!start_ts) start_ts = now_usec();
        while (credit-- > 0 && (limit == 0 || count < limit)) {
            ++count;
            pn_delivery(sender, pn_dtag((const char *)&tag, sizeof(tag)));
            ++tag;

            ssize_t rc = pn_link_send(sender, encode_buffer, encoded_data_size);
            if (rc != encoded_data_size) {
                fprintf(stderr,
                        "Error: pn_link_send() failed to write data.  Error: %zd\n",
                        rc);
                exit(-1);
            }

            pn_link_advance(sender);
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
            case PN_REJECTED:
            case PN_RELEASED:
            case PN_MODIFIED:
            default:
                ++acked;
                if (rs == PN_ACCEPTED)
                    ++accepted;
                else
                    ++not_accepted;
                pn_delivery_settle(dlv);
                break;
            }

            if (limit && acked == limit) {
                // initiate clean shutdown of the endpoints
                stop = true;
                if (!stop_ts) stop_ts = now_usec();
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
  printf("-n      \tUse an anonymous link [%s]\n", BOOL2STR(use_anonymous));
  printf("-s      \tBody size in bytes ('s'=%d 'm'=%d 'l'=%d 'x'=%d) [%d]\n",
         BODY_SIZE_SMALL, BODY_SIZE_MEDIUM, BODY_SIZE_LARGE, BODY_SIZE_WUMBO, body_size);
  printf("-t      \tTarget address [%s]\n", target_address);
  exit(1);
}


int main(int argc, char** argv)
{
    /* command line options */
    opterr = 0;
    int c;
    while ((c = getopt(argc, argv, "ha:c:i:ns:t:")) != -1) {
        switch(c) {
        case 'h': usage(); break;
        case 'a': host_address = optarg; break;
        case 'c':
            if (sscanf(optarg, "%"PRIu64, &limit) != 1)
                usage();
            break;
        case 'i': container_name = optarg; break;
        case 'n': use_anonymous = true; break;
        case 's':
            switch (optarg[0]) {
            case 's': body_size = BODY_SIZE_SMALL; break;
            case 'm': body_size = BODY_SIZE_MEDIUM; break;
            case 'l': body_size = BODY_SIZE_LARGE; break;
            case 'x': body_size = BODY_SIZE_WUMBO; break;
            default:
                usage();
            }
            break;
        case 't': target_address = optarg; break;

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

    while (pn_reactor_process(reactor)) {
        if (stop) {
            // close the endpoints this will cause pn_reactor_process() to
            // eventually break the loop
            if (pn_link) pn_link_close(pn_link);
            if (pn_ssn) pn_session_close(pn_ssn);
            pn_connection_close(pn_conn);
        }
    }

    fprintf(stdout,
            "TX: Sent: %"PRIu64" Accepted: %"PRIu64" Not Accepted: %"PRIu64" Body Size: %d bytes\n",
            count, acked, not_accepted, body_size);
    {
        double duration_sec = (stop_ts - start_ts) / (double)USECS_PER_SECOND;
        fprintf(stdout,
                "TX:  Throughput:  %"PRIu64" msgs sent over %.3f seconds. Rate: %.3f msgs/sec\n",
                count, duration_sec,
                (duration_sec > 0.0) ? (double)count / duration_sec : 0.0);
    }
    return 0;
}
