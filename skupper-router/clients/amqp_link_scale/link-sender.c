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
 * Opens N sending links to the router. Sends 1 message per link. Exits after all messages accepted. See link-receiver.c
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

#define MIN(X,Y) (((X) < (Y)) ? (X) : (Y))

bool _debug = false;
char *target_address = "balanced/address";
char _addr[] = "127.0.0.1:5672";
char *host_address = _addr;
char *container_name = "LinkSender";
char proactor_address[PN_MAX_ADDR];
pn_proactor_t *proactor;
int max_links = 100;
int closed_links = 0;
uint32_t body_length = 1024;

#define TX_BUF_SIZE 32768
char tx_buffer[TX_BUF_SIZE];


// An encoded message fragment that contains a binary body.  Length TBD.
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
    // 4 bytes for length starts here (network order)
};


__attribute__((format(printf, 1, 2))) void debug(const char *format, ...)
{
    va_list args;

    if (!_debug) return;

    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    fflush(stdout);
}


static void signal_handler(int signum)
{
    signal(signum, SIG_IGN);
    if (proactor)
        pn_proactor_interrupt(proactor);
}


pn_delivery_t *start_message(pn_link_t *pn_link)
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

    pn_delivery_set_context(dlv, (void *)((uintptr_t) 0));  // body bytes sent

    // start sending the message
    ssize_t rc = pn_link_send(pn_link, (const char *)msg_fragment, sizeof(msg_fragment));
    if (rc != sizeof(msg_fragment)) {
        fprintf(stderr, "Link send failed error=%ld\n", rc);
        exit(EXIT_FAILURE);
    }

    // append the body length
    uint32_t len = htonl(body_length);
    rc = pn_link_send(pn_link, (const char *)&len, sizeof(len));
    if (rc != sizeof(len)) {
        fprintf(stderr, "Link send body length failed error=%ld\n", rc);
        exit(EXIT_FAILURE);
    }

    return dlv;
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
        for (int index = 0; index < max_links; ++index) {
            char name[64];
            snprintf(name, sizeof(name), "L:%d", index);
            pn_link_t *pn_link = pn_sender(pn_ssn, name);
            pn_terminus_set_address(pn_link_target(pn_link), target_address);
            pn_link_open(pn_link);
        }
    } break;


    case PN_LINK_FLOW: {
        pn_link_t *pn_link = pn_event_link(event);
        if ((pn_link_state(pn_link) & PN_LOCAL_ACTIVE)) {
            pn_delivery_t *dlv = (pn_delivery_t *) pn_link_get_context(pn_link);
            if (!dlv && pn_link_credit(pn_link) > 0) {
                dlv = start_message(pn_link);
                pn_link_set_context(pn_link, dlv);
            }
            if (dlv && pn_delivery_writable(dlv)) {
                uintptr_t sent = (uintptr_t) pn_delivery_get_context(dlv);
                if (sent < body_length) {
                    size_t amount = MIN(body_length - sent, TX_BUF_SIZE);
                    ssize_t rc = pn_link_send(pn_link, tx_buffer, amount);
                    if (rc != amount) {
                        fprintf(stderr, "Unexpected send failure\n");
                        exit(EXIT_FAILURE);
                    }
                    sent += amount;
                    pn_delivery_set_context(dlv, (void *)sent);
                    if (sent == body_length) {
                        pn_link_advance(pn_link);
                    }
                }
            }
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
                pn_link_t *pn_link = pn_delivery_link(dlv);
                pn_delivery_settle(dlv);
                pn_link_close(pn_link);
                closed_links += 1;
                if (closed_links == max_links)
                    return true;  // exit
                break;
            }
        }
    } break;

    case PN_PROACTOR_INACTIVE:
        debug("proactor inactive!\n");
        // fallthrough
    case PN_PROACTOR_INTERRUPT: {
        return true;  // exit
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
    printf("-l  \t# of links to create [%d]\n", max_links);
    printf("-s  \tSize of the message [%"PRIu32" bytes]\n", body_length);
    printf("-D  \tPrint debug info [off]\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char** argv)
{
    /* command line options */
    opterr = 0;
    int c;
    while ((c = getopt(argc, argv, "hDa:i:t:l:s:")) != -1) {
        switch(c) {
        case 'h': usage(argv[0]); break;
        case 'a': host_address = optarg; break;
        case 'i': container_name = optarg; break;
        case 't': target_address = optarg; break;
        case 'D': _debug = true; break;
        case 'l':
            if (sscanf(optarg, "%d", &max_links) != 1 || max_links <= 0)
                usage(argv[0]);
            break;
        case 's':
            if (sscanf(optarg, "%"SCNu32, &body_length) != 1 || body_length == 0)
                usage(argv[0]);
            break;
        default:
            usage(argv[0]);
            break;
        }
    }

    signal(SIGQUIT, signal_handler);
    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);

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
