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

//
// accept TCP connections and drain the incoming data as fast as possible
//

#include <errno.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdbool.h>
#include <time.h>

// Buffer size: currently the router uses 4K buffers and the raw connection supports up to 16 write buffers. Attempt to
// drain the raw connection write buffers each time data is read
//
#define BUFFER_SIZE (4096 * 32)

static long double humanize_rate(long double rate, const char **suffix)
{
    static const char * const units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    const int units_ct = 5;
    const double base = 1024.0;

    for (int i = 0; i < units_ct; ++i) {
        if (rate < base) {
            if (suffix)
                *suffix = units[i];
            return rate;
        }
        rate /= base;
    }
    if (suffix)
        *suffix = units[units_ct - 1];
    return rate;
}

typedef struct thread_context {
    int socket;
    bool half_close;
} thread_context_t;

static void *run(void* data)
{
    int sock = ((thread_context_t*) data)->socket;
    bool half_close = ((thread_context_t*) data)->half_close;

    int opt = 1;
    int err = setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (void*) &opt, sizeof(opt));
    if (err) {
        fprintf(stderr, "server: setsockopt() ERROR! %s\n", strerror(errno));
        return NULL;
    }

    if (half_close) {
        shutdown(sock, SHUT_WR);
    }

    struct timespec start = {0};
    int rc = clock_gettime(CLOCK_MONOTONIC_RAW, &start);
    if (rc) {
        fprintf(stderr, "server: clock_gettime() ERROR! %s\n", strerror(errno));
        return NULL;
    }

    char *buffer = (char*) malloc(BUFFER_SIZE);

    unsigned long rx_octets = 0;
    ssize_t received = 0;
    do {
        rx_octets += received;
        received = recv(sock, buffer, BUFFER_SIZE, 0);
    } while (received > 0);

    if (received < 0) {
        fprintf(stderr, "server: ERROR! %s\n", strerror(errno));
    } else {
        struct timespec end = {0};
        rc = clock_gettime(CLOCK_MONOTONIC_RAW, &end);
        if (rc == 0) {
            long long msecs = (end.tv_sec - start.tv_sec) * 1000LL;
            end.tv_nsec /= 1000000L;
            start.tv_nsec /= 1000000L;
            msecs = (msecs + end.tv_nsec) - start.tv_nsec;

            long double secs = msecs / 1000.0L;
            long double rate = secs != 0.0L ? ((long double) rx_octets / secs) : 0.0L;
            const char *suffix = "";
            rate = humanize_rate(rate, &suffix);
            fprintf(stdout, "server: recv %lu octets in %.3Lf secs, recv rate=%.3Lf %s/sec\n",
                    rx_octets, secs, rate, suffix);
        } else {
            fprintf(stderr, "server: clock_gettime() ERROR! %s\n", strerror(errno));
        }
    }

    close(sock);
    free(buffer);

    return NULL;
}


#define DEFAULT_PORT 9999U

static void usage(const char *prog)
{
    printf("Usage: %s <options>\n", prog);
    printf("-p \tThe TCP port to listen on [%u]\n", DEFAULT_PORT);
    printf("-H \tUse half-close transfer [off]\n");
    exit(1);
}

int main(int argc, char** argv)
{
    unsigned int port = DEFAULT_PORT;
    bool half_close = false;

    /* command line options */
    opterr = 0;
    int c;
    while ((c = getopt(argc, argv, "p:Hh")) != -1) {
        switch(c) {
            case 'h':
                usage(argv[0]);
                break;
            case 'p':
                if (sscanf(optarg, "%u", &port) != 1) {
                    fprintf(stderr, "Invalid port %s\n", optarg);
                    usage(argv[0]);
                }
                break;
            case 'H':
                half_close = true;
                break;
            default:
                usage(argv[0]);
                break;
        }
    }

    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) goto egress;

    int opt = 1;
    int err = setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    if (err) goto egress;

    struct sockaddr_in addr = (struct sockaddr_in) {
        0,
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
        .sin_port = htons(port)
    };

    err = bind(server_sock, (const struct sockaddr*) &addr, sizeof(addr));
    if (err) goto egress;

    err = listen(server_sock, 1);
    if (err) goto egress;

    printf("server: Listening for connections on port %d\n", port);

    while (1) {
        int sock = accept(server_sock, NULL, NULL);
        if (sock < 0) goto egress;

        printf("server: Connection accepted\n");

        pthread_t* thread = malloc(sizeof(pthread_t));
        thread_context_t* context = malloc(sizeof(thread_context_t));

        *context = (thread_context_t) {
            .socket = sock,
            .half_close = half_close,
        };

        pthread_create(thread, NULL, &run, (void*) context);
    }

egress:

    if (errno) {
        fprintf(stderr, "server: ERROR! %s\n", strerror(errno));
    }

    if (server_sock > 0) {
        shutdown(server_sock, SHUT_RDWR);
    }

    return errno;
}
