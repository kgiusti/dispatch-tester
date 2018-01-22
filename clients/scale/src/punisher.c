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
#include <time.h>


#include "proton/reactor.h"
#include "proton/message.h"
#include "proton/connection.h"
#include "proton/session.h"
#include "proton/link.h"
#include "proton/delivery.h"
#include "proton/event.h"
#include "proton/handlers.h"

// Example application data.  This data will be instantiated in the event
// handler, and is available during event processing.  In this example it
// holds configuration and state information.
//
typedef struct {
    int count;          // # messages to send
    int anon;           // use anonymous link if true
    int presettle;      // send all pre settled
    int sent;           // # messages sent
    char *target;       // name of destination target
    char *msg_data;     // pre-encoded outbound message
    int msg_len;        // bytes in msg_data
} app_data_t;

// helper to pull pointer to app_data_t instance out of the pn_handler_t
//
#define GET_APP_DATA(handler) ((app_data_t *)pn_handler_mem(handler))

// Called when reactor exits to clean up app_data
//
static void delete_handler(pn_handler_t *handler)
{
    app_data_t *d = GET_APP_DATA(handler);
    if (d->msg_data) {
        free(d->msg_data);
        d->msg_data = NULL;
    }
}

/* Process each event posted by the reactor.
 */
static void event_handler(pn_handler_t *handler,
                          pn_event_t *event,
                          pn_event_type_t type)
{
    app_data_t *data = GET_APP_DATA(handler);

    switch (type) {

    case PN_CONNECTION_INIT: {
        // Create and open all the endpoints needed to send a message
        //
        pn_connection_t *conn = pn_event_connection(event);
        pn_connection_open(conn);
        pn_session_t *ssn = pn_session(conn);
        pn_session_open(ssn);
        pn_link_t *sender = pn_sender(ssn, "MySender");
        pn_link_set_snd_settle_mode(sender, (data->presettle) ? PN_SND_SETTLED : PN_SND_UNSETTLED);
        if (!data->anon) {
            pn_terminus_set_address(pn_link_target(sender), data->target);
        }
        pn_link_open(sender);
    } break;

    case PN_LINK_FLOW: {
        // the remote has given us some credit, now we can send messages
        //
        static long tag = 0;  // a simple tag generator
        pn_link_t *sender = pn_event_link(event);
        int credit = pn_link_credit(sender);
        while (credit > 0 && (data->count == 0 ||
                              data->sent < data->count)) {
            --credit;
            ++data->sent;
            ++tag;
            pn_delivery_t *delivery;
            delivery = pn_delivery(sender,
                                   pn_dtag((const char *)&tag, sizeof(tag)));
            pn_link_send(sender, data->msg_data, data->msg_len);
            pn_link_advance(sender);
            if (data->presettle) {
                pn_delivery_settle(delivery);
            }
        }
    } break;

    case PN_DELIVERY: {
        pn_delivery_t *dlv = pn_event_delivery(event);
        if (pn_delivery_updated(dlv) && pn_delivery_remote_state(dlv)) {
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
                pn_delivery_settle(dlv);
                fprintf(stderr, "Message not accepted - code:0x%lX\n", (unsigned long)rs);
                break;
            default:
                fprintf(stderr, "Unknown delivery failure - code=0x%lX\n", (unsigned long)rs);
                break;
            }

            if (data->count == data->sent) {
                // initiate clean shutdown of the endpoints
                pn_link_t *link = pn_delivery_link(dlv);
                pn_link_close(link);
                pn_session_t *ssn = pn_link_session(link);
                pn_session_close(ssn);
                pn_connection_close(pn_session_connection(ssn));
            }
        }
    } break;

    default:
        break;
    }
}

static void usage(void)
{
  printf("Usage: punisher <options> <message>\n");
  printf("-a      \tThe host address [localhost:5672]\n");
  printf("-c      \t# of messages to send, 0=forever [1] \n");
  printf("-t      \tTarget address [examples]\n");
  printf("-i      \tContainer name [SendExample]\n");
  printf("-u      \tSend all messages pre-settled [off]\n");
  printf("-n      \tUse anonymous link [off]\n");
  printf("message \tA text string to send.\n");
  exit(1);
}

int main(int argc, char** argv)
{
    char *address = "localhost:5672";
    char *msgtext = "Hello World!";
    char *container = "SendExample";
    int c;
    struct timespec start;
    struct timespec end;

    /* Create a handler for the connection's events.  event_handler() will be
     * called for each event and delete_handler will be called when the
     * connection is released.  The handler will allocate an app_data_t
     * instance which can be accessed when the event_handler is called.
     */
    pn_handler_t *handler = pn_handler_new(event_handler,
                                           sizeof(app_data_t),
                                           delete_handler);

    /* set up the application data with defaults */
    app_data_t *app_data = GET_APP_DATA(handler);
    memset(app_data, 0, sizeof(app_data_t));
    app_data->count = 1;
    app_data->target = "examples";

    /* Attach the pn_handshaker() handler.  This handler deals with endpoint
     * events from the peer so we don't have to.
     */
    pn_handler_add(handler, pn_handshaker());

    /* command line options */
    opterr = 0;
    while((c = getopt(argc, argv, "i:a:c:t:nhu")) != -1) {
        switch(c) {
        case 'h':
            printf("%s: inflict an unreasonably high message load\n", argv[0]);
            usage();
            break;
        case 'a': address = optarg; break;
        case 'c':
            app_data->count = atoi(optarg);
            if (app_data->count < 0) usage();
            break;
        case 't': app_data->target = optarg; break;
        case 'n': app_data->anon = 1; break;
        case 'i': container = optarg; break;
        case 'u': app_data->presettle = 1; break;
        default:
            usage();
            break;
        }
    }
    if (optind < argc) msgtext = argv[optind];


    // create a single message and pre-encode it so we only have to do that
    // once.  All transmits will use the same pre-encoded message simply for
    // speed.
    //
    pn_message_t *message = pn_message();
    pn_message_set_address(message, app_data->target);
    pn_data_t *body = pn_message_body(message);
    pn_data_clear(body);

    // This message's body contains a single string
    if (pn_data_fill(body, "S", msgtext)) {
        fprintf(stderr, "Error building message!\n");
        exit(1);
    }
    pn_data_rewind(body);
    {
        // encode the message, expanding the encode buffer as needed
        //
        size_t len = 128;
        char *buf = (char *)malloc(len);
        int rc = 0;
        do {
            rc = pn_message_encode(message, buf, &len);
            if (rc == PN_OVERFLOW) {
                free(buf);
                len *= 2;
                buf = malloc(len);
            }
        } while (rc == PN_OVERFLOW);
        app_data->msg_len = len;
        app_data->msg_data = buf;
    }
    pn_decref(message);   // message no longer needed

    pn_reactor_t *reactor = pn_reactor();
    pn_connection_t *conn = pn_reactor_connection(reactor, handler);

    // the container name should be unique for each client
    pn_connection_set_container(conn, container);
    pn_connection_set_hostname(conn, address);  // FIXME

    pn_reactor_set_timeout(reactor, 1000);
    pn_reactor_start(reactor);
    clock_gettime(CLOCK_REALTIME, &start);

    while (pn_reactor_process(reactor)) {
        /* Returns 'true' until the connection is shut down */
        if (app_data->presettle && app_data->sent == app_data->count) {
            // sent everything pre-settled and no disposition updates expected,
            // so close the connection
            pn_connection_close(conn);
        }
    }
    clock_gettime(CLOCK_REALTIME, &end);

    long diff = end.tv_sec - start.tv_sec;
    printf("Thruput %ld (%d messages in %ld seconds)\n",
           (diff > 0) ? app_data->count / diff : app_data->count,
           app_data->count, diff);
    return 0;
}
