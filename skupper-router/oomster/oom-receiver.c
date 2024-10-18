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

/* Out-of-memory (OOM) receiver
 *
 * Attempt to overload a routers memory by back pressuring incoming data
 */


bool stop = false;
bool debug_mode = false;
unsigned int links = 1;
int credit_window = 1000;

uint32_t  in_max_frame = 512;       // smallest frame allowed

// The total session window size will be set to per_link_session_frames * links octets.
uint32_t  per_link_session_frames = 10;

char *source_address = "oom-address";  // name of the source node to receive from
char _addr[] = "127.0.0.1:5672";
char *host_address = _addr;
char *container_name = "OOMReceiver";
char proactor_address[1024];

pn_proactor_t *proactor;


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


/* Process each event posted by the proactor
 */
static bool event_handler(pn_event_t *event)
{
    const pn_event_type_t type = pn_event_type(event);
    debug("new event=%s\n", pn_event_type_name(type));
    switch (type) {

    case PN_CONNECTION_BOUND: {
        // Create and open all the endpoints needed to receive messages
        //
        pn_connection_t *pn_conn = pn_event_connection(event);
        pn_transport_t *tport = pn_connection_transport(pn_conn);
        if (in_max_frame) {
            pn_transport_set_max_frame(tport, in_max_frame);
        }
        pn_connection_open(pn_conn);
        pn_session_t *pn_ssn = pn_session(pn_conn);
#if (PN_VERSION_MAJOR > 0) || (PN_VERSION_MINOR > 39)
        if (per_link_session_frames) {
            uint32_t window = per_link_session_frames * links;
            assert(window >= 2);
            int rc = pn_session_set_incoming_window_and_lwm(pn_ssn, window, window/2);
            if (rc != 0) {
                fprintf(stderr, "Failed to set incoming window and low watermark\n");
                fflush(stderr);
                abort();
            }
        }
#endif
        pn_session_open(pn_ssn);

        for (int i = 0; i < links; ++i) {
            char namebuf[32];
            snprintf(namebuf, 32, "OOMReceiver%d", i);
            pn_link_t *pn_link = pn_receiver(pn_ssn, namebuf);
            pn_terminus_set_address(pn_link_source(pn_link), source_address);
            pn_link_open(pn_link);
            pn_link_flow(pn_link, credit_window);
        }
    } break;

    case PN_DELIVERY: {
        // Do nothing!  Let link buffers fill until the session backpressures the router.
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
    printf("-a \tThe address:port of the server [%s]\n", host_address);
    printf("-l \tOpen N stalled receiver links [%u]\n", links);
    printf("-i \tContainer name [%s]\n", container_name);
    printf("-s \tSource address [%s]\n", source_address);
    printf("-w \tCredit window [%d]\n", credit_window);
    printf("-F \tSet Incoming Max Frame (minimum 512) [%"PRIu32" bytes]\n", in_max_frame);
    printf("-W \tSet total allowed incoming frames per link (minimum 2 per link) [%"PRIu32" frames]\n", per_link_session_frames);
    printf("-D \tPrint debug info [off]\n");
    printf("\n");
    printf("Runs until ^C hit (SIGQUIT)\n");
    exit(1);
}


int main(int argc, char** argv)
{
    /* command line options */
    opterr = 0;
    int c;
    while((c = getopt(argc, argv, "i:a:s:hDw:l:F:W:")) != -1) {
        switch(c) {
        case 'h': usage(argv[0]); break;
        case 'a': host_address = optarg; break;
        case 'l':
            if (sscanf(optarg, "%u", &links) != 1 || links == 0)
                usage(argv[0]);
            break;
        case 'i': container_name = optarg; break;
        case 's': source_address = optarg; break;
        case 'w':
            if (sscanf(optarg, "%d", &credit_window) != 1 || credit_window <= 0)
                usage(argv[0]);
            break;
        case 'D': debug_mode = true;       break;
        case 'F':
            if (sscanf(optarg, "%"SCNu32, &in_max_frame) != 1 || in_max_frame < 512)
                usage(argv[0]);
            break;
        case 'W':
            if (sscanf(optarg, "%"SCNu32, &per_link_session_frames) != 1 || per_link_session_frames < 2)
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

    char *host = host_address;
    if (strncmp(host, "amqp://", 7) == 0)
        host += 7;
    char *port = strrchr(host, ':');
    if (port) {
        *port++ = 0;
    } else {
        port = "5672";
    }

    pn_connection_t *pn_conn = pn_connection();
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
    return 0;
}
