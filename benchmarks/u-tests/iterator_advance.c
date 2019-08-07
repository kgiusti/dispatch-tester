#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <ctype.h>
#include <string.h>
#include <arpa/inet.h>

#include "qpid/dispatch/alloc_pool.h"
#include <qpid/dispatch/iterator.h>

// microseconds per second
#define USECS_PER_SECOND 1000000

int64_t now_usec(void)
{
    struct timespec ts;
    int rc = clock_gettime(CLOCK_REALTIME, &ts);
    if (rc) {
        perror("clock_gettime failed");
        exit(errno);
    }

    return (USECS_PER_SECOND * (int64_t)ts.tv_sec) + (ts.tv_nsec / 1000);
}


int main(int argc, char *argv[])
{
    int64_t start_time;
    int64_t total_time = 0;
    //const int repeat = 1;
    const int repeat = 1000;
    const int buf_count = 10000;

    qd_buffer_list_t bufs;
    DEQ_INIT(bufs);

    qd_alloc_initialize();

    for (int i = 0; i < buf_count; ++i) {
        qd_buffer_t *buf = qd_buffer();

        memset(qd_buffer_base(buf), 'A', BUFFER_SIZE - 1);
        qd_buffer_insert(buf, BUFFER_SIZE - 1);
        DEQ_INSERT_TAIL(bufs, buf);
    }

    const int length = 80;
    qd_iterator_t *iter = qd_iterator_buffer(DEQ_HEAD(bufs), 0, buf_count * (BUFFER_SIZE - 1), ITER_VIEW_ALL);

    for (int j = 0; j < repeat; j++) {

        qd_iterator_reset(iter);

        start_time = now_usec();

        while (!qd_iterator_end(iter)) {
            qd_iterator_advance(iter, length);
            //printf("qd_iterator_remaining(iter) %u\n", qd_iterator_remaining(iter));
        }

        total_time += now_usec() - start_time;
    }


    qd_iterator_free(iter);
    qd_buffer_list_free_buffers(&bufs);

    printf("total time: %ld\n", total_time);
    printf("count     : %d\n", repeat);
    printf("avg   time: %.3f\n", (double)total_time/(double)repeat);
    return 0;
}
