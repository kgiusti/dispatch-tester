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


/* Out-of-memory (OOM) sender
 *
 * Attempt to overload the routers memory by sending as much data as possible.
 */

#include "proton/connection.h"
#include "proton/delivery.h"
#include "proton/link.h"
#include "proton/message.h"
#include "proton/session.h"
#include "proton/transport.h"
#include "proton/proactor.h"

#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <assert.h>


#define MIN(X,Y) ((X) > (Y) ? (Y) : (X))

// body data - block of 0's
//

bool stop = false;
bool debug_mode = false;
bool verbose = false;

int links = 1;        // # sending links to open, one messages sent per link

char *target_address = "oom-address";
char _addr[] = "127.0.0.1:5672";
char *host_address = _addr;
char *container_name = "OOMSender";
char proactor_address[1024];

uint64_t total_bytes;

pn_connection_t *pn_conn;
pn_proactor_t *proactor;


// minimal AMQP header for a message that contains a single binary value
// that is 2^32-1 bytes long
//
const uint8_t msg_header[] = {
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
    0xff, 0xff, 0xff, 0xff // 4 bytes for length (UINT32_MAX)
    // start of data...
};


// pn_link_send a body batch whenever there is output capacity
const char body_batch[] =
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


__attribute__((format(printf, 1, 2))) void debug(const char *format, ...)
{
    va_list args;

    if (!debug_mode) return;

    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    fflush(stdout);
}


static void signal_handler(int signum)
{
    signal(signum, SIG_IGN);
    stop = true;
    if (proactor)
        pn_proactor_interrupt(proactor);
}


/* Process each event posted by the proactor.
 */
static bool event_handler(pn_event_t *event)
{
    const pn_event_type_t type = pn_event_type(event);
    debug("new event=%s\n", pn_event_type_name(type));
    switch (type) {

    case PN_CONNECTION_INIT: {
        // Create and open all the endpoints needed to send messages
        //
        pn_connection_open(pn_conn);
        pn_session_t *pn_ssn = pn_session(pn_conn);
        pn_session_open(pn_ssn);
        for (int i = 0; i < links; ++i) {
            char namebuf[32];
            snprintf(namebuf, 32, "OOMSender%d", i);
            pn_link_t *pn_link = pn_sender(pn_ssn, namebuf);
            pn_terminus_set_address(pn_link_target(pn_link), target_address);
            pn_link_open(pn_link);
        }
    } break;

    case PN_LINK_FLOW: {
        // the remote has given us some credit, now we can send messages
        //
        static long tag = 0;  // a simple tag generator
        pn_link_t *sender = pn_event_link(event);
        const int credit = pn_link_credit(sender);
        const size_t win_capacity = pn_transport_get_remote_max_frame(pn_connection_transport(pn_conn))
            * pn_session_remote_incoming_window(pn_link_session(sender));
        const size_t ob = pn_session_outgoing_bytes(pn_link_session(sender));

        debug("Link flow: credit=%d window capacity=%zu (buffered=%zu)\n",
              credit, win_capacity, ob);

        pn_delivery_t *dlv = pn_link_current(sender);
        if (!dlv && credit > 0) {
            // first arrival of credit, start sending the message
            ++tag;
            dlv = pn_delivery(sender, pn_dtag((const char *)&tag, sizeof(tag)));
            assert(dlv);
            pn_link_send(sender, (const char *)msg_header, sizeof(msg_header));
            pn_delivery_set_context(dlv, (void *) 0);  // context is the body octet sent counter
            total_bytes = sizeof(msg_header);
        }

        uintptr_t bytes_sent = (uintptr_t) pn_delivery_get_context(dlv);
        size_t to_send = sizeof(body_batch);
        to_send = MIN(to_send, UINT32_MAX - bytes_sent);
        if (to_send > 0) {
            // Respect the remotes incoming window capacity
            if (win_capacity > ob) {
                to_send = MIN(to_send, win_capacity - ob);
                ssize_t rc = pn_link_send(sender, body_batch, to_send);
                if (rc != to_send) {
                    fprintf(stderr, "ERROR: pn_link_send failed: %zd\n", rc);
                    exit(1);
                }
                debug("%zu bytes written\n", to_send);
                bytes_sent += to_send;
                total_bytes += to_send;
                pn_delivery_set_context(dlv, (void *) bytes_sent);
            } else {
                debug("Session remote in window full, available capacity=%zu buffered=%zu\n",
                      win_capacity, ob);
            }
        } else {
            // Message done - start new one when credit arrives
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
                pn_delivery_settle(dlv);
                break;

            case PN_REJECTED:
            case PN_RELEASED:
            case PN_MODIFIED:
                debug("Delivery failed not accepted (%"PRIu64")\n", rs);
                pn_delivery_settle(dlv);
                break;

            default:
                break;
            }
        }
    } break;

    case PN_TRANSPORT_ERROR: {
        pn_condition_t *tcond = pn_transport_condition(pn_event_transport(event));
        if (tcond) {
            fprintf(stderr, "TRANPORT ERROR: %s %s\n",
                    pn_condition_get_name(tcond), pn_condition_get_description(tcond));
        }
    } break;

    case PN_PROACTOR_INTERRUPT:
        assert(stop);  // expect: due to stopping
        // fall through
    case PN_PROACTOR_INACTIVE:
        debug("proactor inactive!\n");
        return true;

    default:
        break;
    }

    return false;
}


static void usage(const char *progname)
{
    printf("Usage: %s <options>\n", progname);
    printf("-a \tThe address:port of the router [%s]\n", host_address);
    printf("-l \t# of sending links to create [%d]\n", links);
    printf("-i \tContainer name [%s]\n", container_name);
    printf("-t \tTarget address [%s]\n", target_address);
    printf("-D \tPrint debug info [off]\n");
    printf("-v \tPrint total bytes sent [off]\n");
    printf("\n");
    printf("Sends continually until ^C hit (SIGQUIT)\n");
    exit(1);
}


int main(int argc, char** argv)
{
    /* command line options */
    opterr = 0;
    int c;
    while ((c = getopt(argc, argv, "ha:l:i:t:Dv")) != -1) {
        switch(c) {
        case 'h': usage(argv[0]); break;
        case 'a': host_address = optarg; break;
        case 'l':
            if (sscanf(optarg, "%d", &links) != 1)
                usage(argv[0]);
            break;
        case 'i': container_name = optarg; break;
        case 't': target_address = optarg; break;
        case 'D': debug_mode = true; break;
        case 'v': verbose = true; break;

        default:
            usage(argv[0]);
            break;
        }
    }

    signal(SIGQUIT, signal_handler);
    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);

    char *host = host_address;
    if (strncmp(host, "amqp://", 7) == 0)
        host += 7;
    char *port = strrchr(host, ':');
    if (port) {
        *port++ = 0;
    } else {
        port = "5672";
    }

    pn_conn = pn_connection();
    // the container name should be unique for each client
    pn_connection_set_container(pn_conn, container_name);
    pn_connection_set_hostname(pn_conn, host);
    proactor = pn_proactor();
    pn_proactor_addr(proactor_address, sizeof(proactor_address), host, port);
    pn_proactor_connect2(proactor, pn_conn, 0, proactor_address);

    bool done = false;
    while (!done) {
        debug("Waiting for proactor event...\n");
        pn_event_batch_t *events = pn_proactor_wait(proactor);
        debug("Start new proactor batch\n");
        uint64_t start_bytes = total_bytes;
        pn_event_t *event = pn_event_batch_next(events);
        while (event) {
            done = event_handler(event);
            if (done)
                break;

            event = pn_event_batch_next(events);
        }

        debug("Proactor batch processing done\n");
        pn_proactor_done(proactor, events);

        if (verbose && total_bytes > start_bytes) {
            fprintf(stdout, "Bytes send: %"PRIu64"\n", total_bytes);
        }
    }

    pn_proactor_free(proactor);
    return 0;
}
