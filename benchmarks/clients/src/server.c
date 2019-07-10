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


#define BOOL2STR(b) ((b)?"true":"false")

pn_proactor_t *proactor;
bool stop = false;

char *server_address = "0.0.0.0:9999";
char *container_name = "BenchServer";
bool  presettle = false;           // true = send presettled
int   credit_window = 1000;

uint64_t rx_count;
uint64_t tx_count;


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


// for rx data
#define RX_MAX_SIZE (1024 * 64)
char in_buffer[RX_MAX_SIZE];

// encoded message:

#define BODY_SIZE_SMALL  100
#define BODY_SIZE_MEDIUM 2000
#define BODY_SIZE_LARGE  60000  // NOTE RX_MAX_SIZE
int body_size = BODY_SIZE_SMALL;
char _payload[BODY_SIZE_LARGE] = {0};
pn_bytes_t body_data = {
    .size  = 0,
    .start = _payload,
};

char *encode_buffer = NULL;
size_t encode_buffer_size = 0;    // size of malloced memory
size_t encoded_data_size = 0;     // length of encoded content


void generate_message(void)
{
    pn_message_t *out_message = pn_message();
    pn_data_t *body = pn_message_body(out_message);
    pn_data_clear(body);

    pn_data_put_list(body);
    pn_data_enter(body);

    // block of 0s - body_size - long size bytes
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


static void link_send(pn_event_t *e)
{
    static long tag = 0;  // a simple tag generator
    pn_link_t *sender = pn_event_link(e);
    int credit = pn_link_credit(sender);
    while (credit-- > 0) {
        ++tag;
        pn_delivery_t *delivery = pn_delivery(sender,
                                              pn_dtag((const char *)&tag,
                                                      sizeof(tag)));
        pn_link_send(sender, encode_buffer, encoded_data_size);
        pn_link_advance(sender);
        if (presettle) {
            pn_delivery_settle(delivery);
        }
    }
}


static void link_receive(pn_event_t *e)
{
    pn_delivery_t *dlv = pn_event_delivery(e);
    pn_link_t *link = pn_delivery_link(dlv);

    if (pn_delivery_readable(dlv)) {

        ssize_t rc = PN_EOS;
        while (pn_delivery_pending(dlv) > 0) {
            rc = pn_link_recv(link, in_buffer, RX_MAX_SIZE);
            if (rc == PN_EOS)
                break;
        }

        if (!pn_delivery_partial(dlv)) {
            // A full message has arrived
            pn_delivery_update(dlv, PN_ACCEPTED);
            pn_delivery_settle(dlv);  // dlv is now freed
            rx_count += 1;
        }

        if (pn_link_credit(link) <= credit_window/2) {
            // Grant enough credit to bring it up to CAPACITY:
            pn_link_flow(link, credit_window - pn_link_credit(link));
        }
    }
}


static void delivery_updated(pn_delivery_t *dlv)
{
    uint64_t rs = pn_delivery_remote_state(dlv);
    switch (rs) {
    case PN_RECEIVED:
        // This is not a terminal state - it is informational, and the
        // peer is still processing the message.
        break;
    case PN_REJECTED:
    case PN_RELEASED:
    case PN_MODIFIED:
    default:
        fprintf(stderr, "Message not accepted - code: 0x%lX\n", (unsigned long)rs);
        // fallthough
    case PN_ACCEPTED:
        pn_delivery_settle(dlv);
        break;
    }
}


static void handle(pn_event_t* e)
{
    switch (pn_event_type(e)) {

    case PN_LISTENER_OPEN: {
        char port[PN_MAX_ADDR];    /* Get the listening port */
        pn_netaddr_host_port(pn_listener_addr(pn_event_listener(e)), NULL, 0, port, sizeof(port));
        fprintf(stdout, "listening on %s\n", port);
        fflush(stdout);
        break;
    }
    case PN_LISTENER_ACCEPT: {
        /* Configure a transport to allow SSL and SASL connections. See ssl_domain setup in main() */
        pn_transport_t *t = pn_transport();
        pn_transport_set_server(t); /* Must call before pn_sasl() */
        pn_listener_accept2(pn_event_listener(e), NULL, t);
        break;
    }
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
        if (pn_link_is_sender(l)) {
            const char *source = pn_terminus_get_address(pn_link_remote_source(l));
            pn_terminus_set_address(pn_link_source(l), source);
        } else {
            const char* target = pn_terminus_get_address(pn_link_remote_target(l));
            pn_terminus_set_address(pn_link_target(l), target);
            pn_link_flow(l, credit_window);
        }
        pn_link_open(l);
        break;
    }
    case PN_CONNECTION_REMOTE_CLOSE:
        pn_connection_close(pn_event_connection(e));
        break;

    case PN_SESSION_REMOTE_CLOSE:
        pn_session_close(pn_event_session(e));
        pn_session_free(pn_event_session(e));
        break;

    case PN_LINK_REMOTE_CLOSE:
        pn_link_close(pn_event_link(e));
        pn_link_free(pn_event_link(e));
        break;

    case PN_LINK_FLOW:
        link_send(e);
        break;

    case PN_DELIVERY: {
        pn_delivery_t *d = pn_event_delivery(e);
        if (pn_delivery_readable(d) && !pn_delivery_partial(d))
            link_receive(e);

        if (pn_delivery_updated(d))
            delivery_updated(d);
        break;
    }

    default:
        break;
    }
}


static void usage(void)
{
  printf("Usage: server <options>\n");
  printf("-a      \tThe address to listen on [%s]\n", server_address);
  printf("-i      \tContainer name [%s]\n", container_name);
  printf("-s      \tBody size in bytes ('s'=%d 'm'=%d 'l'=%d) [%d]\n",
         BODY_SIZE_SMALL, BODY_SIZE_MEDIUM, BODY_SIZE_LARGE, body_size);
  printf("-u      \tSend all messages presettled [%s]\n", BOOL2STR(presettle));
  printf("-w      \tCredit window [%d]\n", credit_window);
  exit(1);
}


int main(int argc, char **argv)
{
    // command line options
    opterr = 0;
    int c;
    while ((c = getopt(argc, argv, "ha:i:s:uw:")) != -1) {
        switch(c) {
        case 'h': usage(); break;
        case 'a': server_address = optarg; break;
        case 'i': container_name = optarg; break;
        case 's':
            switch (optarg[0]) {
            case 's': body_size = BODY_SIZE_SMALL; break;
            case 'm': body_size = BODY_SIZE_MEDIUM; break;
            case 'l': body_size = BODY_SIZE_LARGE; break;
            default:
                usage();
            }
            break;
        case 'u': presettle = true; break;
        case 'w':
            if (sscanf(optarg, "%d", &credit_window) != 1 || credit_window <= 0)
                usage();
            break;

        default:
            usage();
            break;
        }
    }

    signal(SIGQUIT, signal_handler);
    signal(SIGINT,  signal_handler);

    generate_message();

    proactor = pn_proactor();
    pn_proactor_listen(proactor, pn_listener(), server_address, 16);

    do {
        pn_event_batch_t *events = pn_proactor_wait(proactor);
        pn_event_t *e;
        while ((e = pn_event_batch_next(events))) {
            handle(e);
        }
        pn_proactor_done(proactor, events);
    } while(!stop);

    pn_proactor_free(proactor);
    return 0;
}
