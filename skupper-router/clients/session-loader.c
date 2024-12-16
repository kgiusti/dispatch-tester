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

/*
 * A test traffic generator that repeatedly creates and destroys sessions and links while keeping the parent connection
 * up. This client expects that there is already a test receiver subscribed to test-address.
 */

#include "proton/connection.h"
#include "proton/delivery.h"
#include "proton/link.h"
#include "proton/proactor.h"
#include "proton/session.h"
#include "proton/transport.h"

#include <arpa/inet.h>
#include <assert.h>
#include <inttypes.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BOOL2STR(b) ((b)?"true":"false")
#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))

bool stop = false;
bool verbose = false;

uint32_t session_limit = 1000;               // # messages to send
uint32_t link_limit = 100;                // # sent
uint32_t session_count;
uint32_t link_count;

const char *target_address = "test-address";
const char *host_address = "127.0.0.1:5672";
const char *container_name = "SessionLoader";
char proactor_address[1024];

//
pn_proactor_t   *proactor;

// An encoded message fragment. This test closes links with incomplete messages "in flight"
//
const uint8_t msg_fragment[] = {
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
    0x00, 0x01, 0x00, 0x00  // 4 bytes for length here (fake - no data)
};


__attribute__((format(printf, 1, 2))) void debug(const char *format, ...)
{
    va_list args;

    if (!verbose) return;

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

void start_message(pn_link_t *pn_link)
{
    static long tag = 0;  // a simple tag generator

    assert(pn_link);
    if (pn_link_current(pn_link)) {
        fprintf(stderr, "Cannot create delivery - in process\n");
        exit(EXIT_FAILURE);
    }

    debug("start message %s!\n", pn_link_name(pn_link));

    pn_delivery_t *dlv = pn_delivery(pn_link, pn_dtag((const char *)&tag, sizeof(tag)));
    if (dlv == NULL) {
        fprintf(stderr, "Failed to create a delivery\n");
        exit(EXIT_FAILURE);
    }
    ++tag;

    pn_delivery_set_context(dlv, (void *)((uintptr_t) 0));

    // start sending the message
    ssize_t rc = pn_link_send(pn_link, (const char *)msg_fragment, sizeof(msg_fragment));
    if (rc != sizeof(msg_fragment)) {
        fprintf(stderr, "Link send failed error=%ld\n", rc);
        exit(EXIT_FAILURE);
    }
}


/* Process each event posted by the proactor.
   Return true if client has stopped.
 */
static bool event_handler(pn_event_t *event)
{
    const pn_event_type_t etype = pn_event_type(event);
    debug("new event=%s\n", pn_event_type_name(etype));

    switch (etype) {

    case PN_CONNECTION_INIT: {
        pn_connection_t *pn_conn = pn_event_connection(event);
        pn_connection_open(pn_conn);
        pn_session_t *pn_ssn = pn_session(pn_conn);
        pn_session_open(pn_ssn);
    } break;

    case PN_SESSION_REMOTE_OPEN:  // fallthrough
    case PN_SESSION_LOCAL_OPEN: {
        pn_session_t *pn_ssn = pn_event_session(event);
        if (pn_session_state(pn_ssn) == (PN_LOCAL_ACTIVE | PN_REMOTE_ACTIVE)) {
            debug("session opened - create links...\n");
            for (uint32_t i = 0; i < link_limit; ++i) {
                char name[64];
                snprintf(name, sizeof(name), "Session-%"PRIu32":Link-%"PRIu32, session_count, i);
                pn_link_t *pn_link = pn_sender(pn_ssn, name);
                pn_terminus_set_address(pn_link_target(pn_link), target_address);
                pn_link_open(pn_link);
            }
        }
    } break;

    case PN_LINK_FLOW: {
        pn_link_t *pn_link = pn_event_link(event);
        if (link_count < link_limit && !pn_link_current(pn_link) && pn_link_credit(pn_link) > 0) {
            start_message(pn_link);
            link_count += 1;
            if (link_count == link_limit) {
                // Teardown the session
                debug("messages in flight, closing session...\n");
                pn_session_close(pn_event_session(event));
            }
        }
    } break;

    case PN_SESSION_REMOTE_CLOSE:  // fallthrough
    case PN_SESSION_LOCAL_CLOSE: {
        pn_session_t *pn_ssn = pn_event_session(event);
        if (pn_session_state(pn_ssn) == (PN_LOCAL_CLOSED | PN_REMOTE_CLOSED)) {
            session_count += 1;
            if (session_count < session_limit) {
                // continue with a new session
                debug("starting new session (%"PRIu32" of %"PRIu32")...\n",
                      session_count, session_limit);
                link_count = 0;
                pn_session_free(pn_ssn);
                pn_ssn = pn_session(pn_event_connection(event));
                pn_session_open(pn_ssn);
            } else {
                debug("test complete!\n");
                stop = true;
                pn_proactor_interrupt(proactor);
            }
        }
    } break;

    case PN_PROACTOR_INACTIVE:
        stop = true;
        debug("proactor inactive!\n");
        // fallthrough
    case PN_PROACTOR_INTERRUPT: {
        return stop;
    } break;

    default:
        break;
    }

    return false;
}


static void usage(const char *prog)
{
    printf("Usage: %s <options>\n", prog);
    printf("-a  \tThe host address [%s]\n", host_address);
    printf("-i  \tContainer name [%s]\n", container_name);
    printf("-t  \tTarget address [%s]\n", target_address);
    printf("-c  \t# of sessions to create [%"PRIu32"]\n", session_limit);
    printf("-l  \t# of links per session to create [%"PRIu32"]\n", link_limit);
    printf("-D  \tPrint debug info [off]\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char** argv)
{
    /* command line options */
    opterr = 0;
    int c;
    while ((c = getopt(argc, argv, "ha:i:t:c:l:D")) != -1) {
        switch(c) {
        case 'h': usage(argv[0]); break;
        case 'a': host_address = optarg; break;
        case 'c':
            if (sscanf(optarg, "%"SCNu32, &session_limit) != 1 || session_limit == 0)
                usage(argv[0]);
            break;
        case 'i': container_name = optarg; break;
        case 'l':
            if (sscanf(optarg, "%"SCNu32, &link_limit) != 1 || link_limit == 0)
                usage(argv[0]);
            break;
        case 't': target_address = optarg; break;
        case 'D': verbose = true; break;
        default:
            usage(argv[0]);
            break;
        }
    }

    signal(SIGQUIT, signal_handler);
    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);

    // test infrastructure may add a "amqp[s]://" prefix to the address string.
    // That causes proactor much grief, so strip it off
    if (strncmp("amqps://", host_address, strlen("amqps://")) == 0) {
        host_address += strlen("amqps://"); // no! no ssl for you!
    } else if (strncmp("amqp://", host_address, strlen("amqp://")) == 0) {
        host_address += strlen("amqp://");
    }

    // trim port from hostname
    char *hostname = strdup(host_address);
    char *port = strchr(hostname, ':');
    if (port) {
        *port++ = 0;
    } else {
        port = "5672";
    }

    pn_connection_t *pn_conn = pn_connection();
    // the container name should be unique for each client
    pn_connection_set_container(pn_conn, container_name);
    pn_connection_set_hostname(pn_conn, hostname);
    proactor = pn_proactor();
    pn_proactor_addr(proactor_address, sizeof(proactor_address), hostname, port);
    pn_proactor_connect2(proactor, pn_conn, 0, proactor_address);
    free(hostname);

    bool done = false;
    while (!done) {
        debug("Waiting for proactor event...\n");
        pn_event_batch_t *events = pn_proactor_wait(proactor);
        debug("Start new proactor batch\n");
        pn_event_t *event = pn_event_batch_next(events);
        while (event) {
            done = event_handler(event);
            if (done)
                break;

            event = pn_event_batch_next(events);
        }

        debug("Proactor batch processing done\n");
        pn_proactor_done(proactor, events);
    }

    debug("Send complete!\n");
    pn_proactor_free(proactor);

    return EXIT_SUCCESS;
}
