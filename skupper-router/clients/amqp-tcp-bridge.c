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
 */

#include <proton/engine.h>
#include <proton/listener.h>
#include <proton/netaddr.h>
#include <proton/proactor.h>
#include <proton/sasl.h>
#include <proton/ssl.h>
#include <proton/transport.h>
#include <proton/message.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <inttypes.h>

pn_proactor_t *proactor;
bool stop = false;

char *listener_address = "0.0.0.0:5672";
char *server_address = "localhost:20002";
char *container_name = "AmqpTcpBridge";
int   credit_window = 1000;

#define SLOT_COUNT 128
#define BUF_SIZE   4096

typedef struct buffer_t {
    size_t size;
    uint8_t data[BUF_SIZE];
};

typedef struct context_t {
    pn_connection_t *amqp_conn;
    pn_link_t       *amqp_link;
    pn_raw_connection_t *raw_conn;
    unsigned int producer_slot;
    unsigned int consumer_slot;
    buffer_t buffers[SLOT_COUNT];
} context_t;

static unsigned int next_slot(unsigned int slot)
{
    return (slot + 1) % SLOT_COUNT;
}

static bool slots_full(const context_t *context)
{
    return next_slot(context->producer_slot) == context->consumer_slot;
}

static bool slots_empty(const context_t *context)
{
    return context->producer_slot == context->consumer_slot;
}

static void signal_handler(int signum)
{
    signal(SIGINT,  SIG_IGN);
    signal(SIGQUIT, SIG_IGN);

    switch (signum) {
    case SIGINT:
    case SIGQUIT:
        stop = true;
        if (proactor) pn_proactor_interrupt(proactor);
        break;
    default:
        break;
    }
}

static int link_receive(context_t *context, pn_link_t *link, pn_delivery_t *dlv)
{
    int written = 0;

    while (!slots_full(context) && pn_delivery_pending(dlv)) {
        buffer_t *buf = &context->slots[context->producer_slot];
        ssize_t rc = pn_link_recv(link, (char *)buf->data, BUF_SIZE);
        if (rc < 0) {
            break; // EOS or error
        } else if (rc == 0) {
            // no data available for now
            break;
        } else {
            buf->size = rc;
            context->producer_slot = next_slot(context->producer_slot);
            written += 1;
        }
    }

    return written;
}

static int raw_write_drain(context_t *context)
{
    int drained = 0;
    pn_raw_buffer_t rdesc = {0};

    assert(context->raw_conn);
    while (pn_raw_connection_take_written_buffers(context->raw_conn, &rdesc, 1) == 1) {
        unsigned int my_slot = rdesc.context;
        assert(my_slot == context->consumer_slot);
        drained += 1;
        context->consumer_slot = next_slot(context->consumer_slot);
    }

    return drained;
}

static int raw_send(context_t *context)
{
    int written = 0;

    size_t avail = pn_raw_connection_write_buffers_capacity(context->raw_conn);
    unsigned int slot = context->consumer_slot;
    while (avail && slot != context->producer_slot) {
        pn_raw_buffer_t rdesc = {0};
        rdesc.context = slot;
        rdesc.bytes = (char *) context->buffers[slot].data;
        rdesc.size = context->buffers[slot].size;
        rdesc.offset = 0;
        pn_raw_connection_write_buffers(context->raw_conn, &rdesc, 1);
        avail -= 1;
        slot = next_slot(slot);
        written += 1;
    }

    // advance consumer_slot AFTER buffers are written

    return written;
}

static void connect_raw(context_t *context)
{
    assert(context);

    context->raw_conn = pn_raw_connection();
    pn_raw_connection_set_context(context->raw_conn, context);
    pn_proactor_raw_connect(proactor, context->raw_conn, server_address);
    fprintf(stdout, "Connecting to %s...\n", server_address);
}

static void listener_event_handler(pn_event_t *e)
{
    fprintf(stdout, "Proactor event %s\n", pn_event_type_name(pn_event_type(e)));

    pn_listener_t *pn_listener = pn_event_listener(e);
    assert(pn_listener);

    switch (pn_event_type(e)) {
        case PN_LISTENER_OPEN: {
            /* char port[PN_MAX_ADDR];    /\* Get the listening port *\/ */
            /* pn_netaddr_host_port(pn_listener_addr(pn_listener), NULL, 0, port, sizeof(port)); */
            /* fprintf(stdout, "listening on %s\n", port); */
            /* fflush(stdout); */
            break;
        }
        case PN_LISTENER_ACCEPT: {
            context_t *context = calloc(1, sizeof(context_t));
            context->amqp_conn = pn_connection();
            pn_connection_set_context(context->amqp_conn, context);
            pn_listener_accept2(pn_listener, context->amqp_conn, 0);
            fprintf(stdout, "Accepted AMQP conn, context=%p\n", (void *)context);
            break;
        }
        case PN_LISTENER_CLOSE: {
            break;
        }
        default:
            break;
    }
}


static void amqp_event_handler(pn_event_t *e)
{
    fprintf(stdout, "Proactor event %s\n", pn_event_type_name(pn_event_type(e)));

    context_t *context = pn_connection_get_context(pn_event_connection(e));

    switch (pn_event_type(e)) {

        case PN_CONNECTION_INIT:
            pn_connection_set_container(pn_event_connection(e), container_name);
            break;

        case PN_CONNECTION_REMOTE_OPEN: {
            pn_connection_open(pn_event_connection(e)); /* Complete the open */
            break;
        }
        case PN_SESSION_REMOTE_OPEN: {
            pn_session_open(pn_event_session(e));
            break;
        }
        case PN_LINK_REMOTE_OPEN: {
            pn_link_t *l = pn_event_link(e);
            if (context->amqp_link) {
                fprintf(stdout, "Rejecting extra link\n");
                pn_link_open(l);
                pn_link_close(l);
            }

            if (pn_link_is_receiver) {
                const char* target = pn_terminus_get_address(pn_link_remote_target(l));
                pn_terminus_set_address(pn_link_target(l), target);
                pn_link_flow(l, credit_window);
            } else {
                fprintf(stderr, "ERROR: AMQP Sender not supported!\n");
                exit(1);
                /* const char *source = pn_terminus_get_address(pn_link_remote_source(l)); */
                /* pn_terminus_set_address(pn_link_source(l), source); */
            }
            pn_link_open(l);


            // now initiate a raw connection to the TCP server
            context->amqp_link = link;
            connect_raw(context);
            break;
        }
        case PN_CONNECTION_REMOTE_CLOSE:
            pn_connection_close(pn_event_connection(e));
            assert(context);
            context->amqp_connection = 0;
            if (context->raw_conn == 0) {
                fprintf(stdout, "Freeing context %p\n", (void *)context);
                free(context);
            }
            break;

        case PN_SESSION_REMOTE_CLOSE:
            pn_session_close(pn_event_session(e));
            pn_session_free(pn_event_session(e));
            break;

        case PN_LINK_REMOTE_CLOSE:
            context->amqp_link = 0;
            pn_link_close(pn_event_link(e));
            pn_link_free(pn_event_link(e));
            break;

        case PN_LINK_FLOW:
            break;

        case PN_DELIVERY:
            assert(pn_event_delivery(e) == pn_link_current(context->amqp_link));
            // fallthrough
        case PN_CONNECTION_WAKE: {
            if (!context->amqp_link)
                break;

            pn_delivery_t *d = pn_link_current(context->amqp_link);

            int written = 0;
            if (pn_delivery_pending(d)) {
                if (context->raw_conn)
                    raw_write_drain(context);  // attempt to make room in slots

                written = link_receive(context, pn_event_link(e), d);
            }

            // try sending the new data
            if (written && context->raw_conn) {
                pn_raw_connection_wake(context->raw_conn);
            }

            if (!pn_delivery_pending(d) && !pn_delivery_partial(d)) {
                fprintf(stdout, "Delivery done\n");
                pn_delivery_update(d, PN_ACCEPTED);
                pn_delivery_settle(d);
                d = 0;
                pn_link_flow(context->amqp_link, 1);
            }
        }
        break;
    }

    default:
        break;
    }
}

static void raw_event_handler(pn_event_t *e)
{
    fprintf(stdout, "Proactor event %s\n", pn_event_type_name(pn_event_type(e)));

    context_t *context = pn_event_raw_connection(event);
    assert(context);

    if (pn_event_type(e) == PN_RAW_CONNECTION_DISCONNECTED) {
        context->raw_conn = 0;
        if (context->amqp_conn == 0) {
            fprintf(stdout, "Freeing context %p\n", (void *)context);
            free(context);
        } else {
            fprintf(stdout, "Force-close AMQP connection...\n");
            pn_connection_close(context->amqp_conn);
        }
        return;  // raw conn no longer valid
    }

    bool was_full = slots_full(context);
    raw_write_drain(context);
    raw_send(context);

    if (slots_empty(context) && context->amqp_link == 0) {
        fprintf(stdout, "Stream complete, closing raw conn...\n");
        pn_raw_connection_close(context->raw_conn);
    } else if (was_full && !slots_full(context)) {
        if (context->amqp_conn) {
            pn_connection_wake(context->amqp_conn);
        }
    }
}

static void proactor_event_handler(pn_event_type *e)
{
    fprintf(stdout, "Proactor event %s\n", pn_event_type_name(pn_event_type(e)));
}


static void usage(const char *prog)
{
    printf("Usage: %s <options>\n", prog);
    printf("-a \tThe address to listen on [%s]\n", listener_address);
    printf("-s \tThe address of the server to connect to [%s]\n", server_address);
    printf("-i \tContainer name [%s]\n", container_name);
    exit(1);
}


int main(int argc, char **argv)
{
    // command line options
    opterr = 0;
    int c;
    while ((c = getopt(argc, argv, "ha:i:s:")) != -1) {
        switch(c) {
            case 'h': usage(argv[0]); break;
            case 'a': listener_address = optarg; break;
            case 'i': container_name = optarg; break;
            case 's': server_address = optarg; break;
            default:
                usage(argv[0]);
                break;
        }
    }

    signal(SIGQUIT, signal_handler);
    signal(SIGINT,  signal_handler);

    proactor = pn_proactor();
    pn_proactor_listen(proactor, pn_listener(), listener_address, 16);
    fprintf(stdout, "Listening for AMQP on %s\n", listener_address);

    do {
        pn_event_batch_t *events = pn_proactor_wait(proactor);

        if (pn_event_batch_connection(events) != 0) {
            pn_event_t *e;
            while ((e = pn_event_batch_next(events))) {
                amqp_event_handler(e);
            }
        } else if (pn_event_batch_raw_connection(events) != 0) {
            pn_event_t *e;
            while ((e = pn_event_batch_next(events))) {
                raw_event_handler(e);
            }
        } else if (pn_event_batch_listener(events) != 0) {
            pn_event_t *e;
            while ((e = pn_event_batch_next(events))) {
                listener_event_handler(e);
            }
        } else {
            pn_event_t *e;
            while ((e = pn_event_batch_next(events))) {
                proactor_event_handler(e);
            }
        }
        pn_proactor_done(proactor, events);
    } while (!stop);

    pn_proactor_free(proactor);
    return 0;
}
