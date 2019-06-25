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


#include "proton/reactor.h"
#include "proton/message.h"
#include "proton/connection.h"
#include "proton/session.h"
#include "proton/link.h"
#include "proton/delivery.h"
#include "proton/event.h"
#include "proton/handlers.h"

static int quiet = 0;

// Example application data.  This data will be instantiated in the event
// handler, and is available during event processing.  In this example it
// holds configuration and state information.
//
typedef struct {
    int count;          // # messages to send
    int anon;           // use anonymous link if true
    int presettled;     // send all messages presettled
    int acked;          // exit after N acks
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
        // we do not wait for ack until the last message
        pn_link_set_snd_settle_mode(sender, PN_SND_MIXED);
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
        while (credit > 0 && data->count > 0) {
            --credit;
            --data->count;
            ++tag;
            pn_delivery_t *delivery;
            delivery = pn_delivery(sender,
                                   pn_dtag((const char *)&tag, sizeof(tag)));
            pn_link_send(sender, data->msg_data, data->msg_len);
            pn_link_advance(sender);
            if (data->presettled && data->count > 0) {
                // send pre-settled until the last one, then wait for an ack on
                // the last sent message. This allows the sender to send
                // messages as fast as possible and then exit when the consumer
                // has dealt with the last one.
                //
                pn_delivery_settle(delivery);
            }
        }
    } break;

    case PN_DELIVERY: {
        // Since the example sends all messages but the last pre-settled
        // (pre-acked), only the last message's delivery will get updated with
        // the remote state (acked/nacked).
        //
        pn_delivery_t *dlv = pn_event_delivery(event);
        if (pn_delivery_updated(dlv) && pn_delivery_remote_state(dlv)) {
            uint64_t rs = pn_delivery_remote_state(dlv);
            switch (rs) {
            case PN_RECEIVED:
                // This is not a terminal state - it is informational, and the
                // peer is still processing the message.
                break;
            case PN_ACCEPTED:
                --data->acked;
                pn_delivery_settle(dlv);
                if (!quiet) fprintf(stdout, "Send complete!\n");
                break;
            case PN_REJECTED:
            case PN_RELEASED:
            case PN_MODIFIED:
                --data->acked;
                pn_delivery_settle(dlv);
                fprintf(stderr, "Message not accepted - code:%lu\n", (unsigned long)rs);
                break;
            default:
                // ??? no other terminal states defined, so ignore anything else
                --data->acked;
                pn_delivery_settle(dlv);
                fprintf(stderr, "Unknown delivery failure - code=%lu\n", (unsigned long)rs);
                break;
            }

            if (data->acked == 0) {
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
  printf("Usage: send <options> <message>\n");
  printf("-a      \tThe host address [localhost:5672]\n");
  printf("-c      \t# of messages to send [1]\n");
  printf("-t      \tTarget address [examples]\n");
  printf("-n      \tUse an anonymous link [off]\n");
  printf("-i      \tContainer name [SendExample]\n");
  printf("-q      \tQuiet - turn off stdout\n");
  printf("-p      \tSend all messages presettled [off]\n");
  printf("message \tA text string to send.\n");
  exit(1);
}

int main(int argc, char** argv)
{
    char *address = "localhost";
    char *msgtext = "Hello World!";
    char *container = "SendExample";
    int c;

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
    app_data->acked = 1;
    app_data->target = "examples";

    /* Attach the pn_handshaker() handler.  This handler deals with endpoint
     * events from the peer so we don't have to.
     */
    pn_handler_add(handler, pn_handshaker());

    /* command line options */
    opterr = 0;
    while((c = getopt(argc, argv, "i:a:c:t:nhqp")) != -1) {
        switch(c) {
        case 'h': usage(); break;
        case 'a': address = optarg; break;
        case 'c':
            app_data->count = atoi(optarg);
            if (app_data->count < 1) usage();
            break;
        case 't': app_data->target = optarg; break;
        case 'n': app_data->anon = 1; break;
        case 'i': container = optarg; break;
        case 'q': quiet = 1; break;
        case 'p': app_data->presettled = 1; break;
        default:
            usage();
            break;
        }
    }
    if (!app_data->presettled)
        // wait for all msgs to be acked
        app_data->acked = app_data->count;

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

    // wait up to 5 seconds for activity before returning from
    // pn_reactor_process()
    pn_reactor_set_timeout(reactor, 5000);

    pn_reactor_start(reactor);

    while (pn_reactor_process(reactor)) {
        /* Returns 'true' until the connection is shut down.
         * pn_reactor_process() will return true at least once every 5 seconds
         * (due to the timeout).  If no timeout was configured,
         * pn_reactor_process() returns as soon as it finishes processing all
         * pending I/O and events. Once the connection has closed,
         * pn_reactor_process() will return false.
         */
    }

    return 0;
}
