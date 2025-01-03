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
 * Opens N receive links to the router. Drains all incoming messages until ^C hit
 */

#include "proton/connection.h"
#include "proton/delivery.h"
#include "proton/link.h"
#include "proton/message.h"
#include "proton/session.h"
#include "proton/proactor.h"
#include "proton/transport.h"
#include "proton/version.h"

#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <assert.h>


bool _debug = false;
char *source_address = "balanced/address";
char *host_address = "127.0.0.1:5672";
char *container_name = "LinkReceiver";
char proactor_address[PN_MAX_ADDR];
pn_proactor_t *proactor;
int max_links = 100;
unsigned long msg_count = 0;

#define RX_BUF_SIZE 65536
char rx_buffer[RX_BUF_SIZE];

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


/* Process each event posted by the proactor
 */
static bool event_handler(pn_event_t *event)
{
    const pn_event_type_t type = pn_event_type(event);
    debug("new event=%s\n", pn_event_type_name(type));
    switch (type) {

    case PN_CONNECTION_BOUND: {
        // Create and open all the endpoints needed to send a message
        //
        pn_connection_t *pn_conn = pn_event_connection(event);
        pn_connection_open(pn_conn);
        pn_session_t *pn_ssn = pn_session(pn_conn);
        pn_session_open(pn_ssn);

        for (int index = 0; index < max_links; ++index) {
            char link_name[64];
            snprintf(link_name, sizeof(link_name), "L:%d", index);
            pn_link_t *pn_link = pn_receiver(pn_ssn, link_name);
            pn_terminus_set_address(pn_link_source(pn_link), source_address);
            pn_link_open(pn_link);
            pn_link_flow(pn_link, 1000);
        }
    } break;

    case PN_DELIVERY: {
        pn_delivery_t *dlv = pn_event_delivery(event);
        pn_link_t *pn_link = pn_event_link(event);
        size_t avail = pn_delivery_pending(dlv);
        bool rx_done = false;
        uintptr_t total = (uintptr_t) pn_delivery_get_context(dlv);
        while (avail && !rx_done) {
             // Drain the data as it comes in rather than waiting for the
             // entire delivery to arrive. This allows the receiver to handle
             // messages that are way huge.
             ssize_t rc = pn_link_recv(pn_link, rx_buffer, sizeof(rx_buffer));
             rx_done = (rc == PN_EOS || rc < 0);
             if (!rx_done)
                 total += rc;
             avail = pn_delivery_pending(dlv);
        }

        if (rx_done || !pn_delivery_partial(dlv)) {
            // A full message has arrived (or a failure occurred)
            debug("Message %lu (link %s) receive complete %"PRIuPTR" bytes, accepting\n", msg_count, pn_link_name(pn_link), total);
            pn_delivery_update(dlv, PN_ACCEPTED);
            pn_delivery_settle(dlv);  // dlv is now freed
            pn_link_flow(pn_link, 1);
            msg_count += 1;
        } else if (total) {
            debug("Link received %"PRIuPTR" octets\n", total);
            pn_delivery_set_context(dlv, (void *) total);
        }
    } break;

    case PN_PROACTOR_INACTIVE:
    case PN_PROACTOR_INTERRUPT: {
        debug("proactor inactive!\n");
        return true;
    } break;

    default:
        break;
    }

    return false;
}

static void usage(void)
{
    printf("Usage: receiver <options>\n");
    printf("-a \tThe address:port of the server [%s]\n", host_address);
    printf("-l \tTotal receive links to open [%d]\n", max_links);
    printf("-i \tContainer name [%s]\n", container_name);
    printf("-s \tSource address [%s]\n", source_address);
    printf("-D \tPrint debug info [off]\n");
    exit(EXIT_FAILURE);
}


int main(int argc, char** argv)
{
    /* command line options */
    opterr = 0;
    int c;
    while((c = getopt(argc, argv, "hDi:a:s:l:")) != -1) {
        switch(c) {
        case 'h': usage(); break;
        case 'a': host_address = optarg; break;
        case 'l':
            if (sscanf(optarg, "%d", &max_links) != 1 || max_links <= 0)
                usage();
            break;
        case 'i': container_name = optarg; break;
        case 's': source_address = optarg; break;
        case 'D': _debug = true;           break;
        default:
            usage();
            break;
        }
    }

    signal(SIGQUIT, signal_handler);
    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);

    // trim port from hostname
    char *hostname = strdup(host_address);
    char *port = strrchr(hostname, ':');
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

    pn_proactor_free(proactor);

    debug("Total messages received: %lu\n", msg_count);
    return 0;
}
