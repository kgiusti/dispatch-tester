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
// Connect to the TCP server and send data as fast as possible
//

#define _GNU_SOURCE

#include <errno.h>
#include <netdb.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdbool.h>
#include <time.h>


// Buffer size: currently the router uses 4K buffers and the raw connection supports up to 16 receive buffers. Attempt
// to fill the raw connection receive buffers each time data is sent
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


static int run_sender(int sock, unsigned long amount, bool half_close)
{
    unsigned long remaining = amount;
    char *buffer = (char*) malloc(BUFFER_SIZE);
    memset(buffer, 'x', BUFFER_SIZE);

    if (half_close) {
        shutdown(sock, SHUT_RD);
    }

    struct timespec start = {0};
    int rc = clock_gettime(CLOCK_MONOTONIC_RAW, &start);
    if (rc) {
        fprintf(stderr, "client: clock_gettime() ERROR! %s\n", strerror(errno));
        return -1;
    }

    while (remaining > 0) {
        ssize_t sent = send(sock, buffer,
                            (remaining > BUFFER_SIZE) ? BUFFER_SIZE : remaining,
                            0);
        if (sent < 0) {
            fprintf(stderr, "client: ERROR! %s\n", strerror(errno));
            return -1;
        }
        remaining -= sent;
    }

    struct timespec end = {0};
    rc = clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    if (rc) {
        fprintf(stderr, "client: clock_gettime() ERROR! %s\n", strerror(errno));
        return -1;
    }

    long long msecs = (end.tv_sec - start.tv_sec) * 1000LL;
    end.tv_nsec /= 1000000L;
    start.tv_nsec /= 1000000L;
    msecs = (msecs + end.tv_nsec) - start.tv_nsec;

    long double secs = msecs / 1000.0L;
    long double rate = secs != 0.0L ? ((long double) amount / secs) : 0.0L;
    const char *suffix = "";
    rate = humanize_rate(rate, &suffix);
    fprintf(stdout, "client: sent %lu octets in %.3Lf secs, send rate=%.3Lf %s/sec\n",
            amount, secs, rate, suffix);

    free(buffer);
    return 0;
}

#define DEFAULT_PORT 9999U
#define DEFAULT_OCTETS (1024UL * 1024UL * 1024UL)

static void usage(const char *prog)
{
    printf("Usage: %s <options>\n", prog);
    printf("-p \tThe TCP port to connect to [%u]\n", DEFAULT_PORT);
    printf("-c \t# of octets to transfer [%lu (K|M|G)]\n", DEFAULT_OCTETS);
    printf("-H \tUse half-close transfer [off]\n");
    exit(1);
}


int main(int argc, char *argv[])
{
    unsigned int port = DEFAULT_PORT;
    unsigned long amount = DEFAULT_OCTETS;
    bool half_close = false;

    /* command line options */
    opterr = 0;
    int c;
    while ((c = getopt(argc, argv, "p:c:Hh")) != -1) {
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
            case 'c': {
                unsigned long scale = 1;
                char *ptr = strpbrk(optarg, "KMG");
                if (ptr) {
                    switch (*ptr) {
                        case 'K':
                            scale = 1024;
                            break;
                        case 'M':
                            scale = 1024 * 1024;
                            break;
                        case 'G':
                            scale = 1024 * 1024 * 1024;
                            break;
                    }
                    *ptr = 0;
                }
                if (sscanf(optarg, "%lu", &amount) != 1) {
                    fprintf(stderr, "Invalid count %s\n", optarg);
                    usage(argv[0]);
                }
                amount *= scale;
                break;
            }
            case 'H':
                half_close = true;
                break;
            default:
                usage(argv[0]);
                break;
        }
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) goto egress;

    struct sockaddr_in addr = (struct sockaddr_in) {
        0,
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
        .sin_port = htons(port)
    };

    printf("client: Connecting to port %d\n", port);

    int err = connect(sock, (const struct sockaddr*) &addr, sizeof(addr));
    if (err) goto egress;

    printf("client: Connected, sending %lu octets\n", amount);

    run_sender(sock, amount, half_close);

egress:

    if (errno) {
        fprintf(stderr, "client: ERROR! %s\n", strerror(errno));
    }

    if (sock > 0) close(sock);
    exit(errno);
}
