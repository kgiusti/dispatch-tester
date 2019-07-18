#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <ctype.h>

#include "proton/message.h"
#include "qpid/dispatch/amqp.h"
#include "qpid/dispatch/buffer.h"
#include "qpid/dispatch/alloc_pool.h"
#include "qpid/dispatch/parse.h"
#include "qpid/dispatch/trace_mask.h"
#include "qpid/dispatch/bitmask.h"
#include "qpid/dispatch/message.h"


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


qd_message_t *build_message(void)
{
    qd_composed_field_t *f = qd_compose(QD_PERFORMATIVE_HEADER, 0);
    qd_compose_start_list(f);
    qd_compose_insert_bool(f, 0);
    qd_compose_end_list(f);

    f = qd_compose(QD_PERFORMATIVE_MESSAGE_ANNOTATIONS, f);
    qd_compose_start_map(f);

    // begin TRACE
    qd_compose_insert_string(f, QD_MA_TRACE);
    qd_compose_start_list(f);

    qd_compose_insert_string(f, "TestRouterA");
    qd_compose_end_list(f);
    qd_compose_end_map(f);

    f = qd_compose(QD_PERFORMATIVE_PROPERTIES, f);
    qd_compose_start_list(f);
    qd_compose_insert_null(f);                    // message-id
    qd_compose_insert_null(f);                    // user-id
    qd_compose_insert_string(f, "amqp://foo.com/app"); // to
    qd_compose_insert_null(f);                    // subject
    qd_compose_insert_string(f, "amqp://reply-to.com/address");
    qd_compose_end_list(f);

    f = qd_compose(QD_PERFORMATIVE_BODY_AMQP_VALUE, f);
    qd_compose_insert_string(f, "Hi There!");

    qd_message_t *msg = qd_message();
    qd_message_compose_2(msg, f);

    return msg;
}


// NOTE: must modify router_node.c to make this function public!
extern qd_iterator_t *router_annotate_message(bool edge_mode,
                                              qd_tracemask_t *tracemask,
                                              qd_message_t  *msg,
                                              qd_bitmask_t **link_exclusions,
                                              uint32_t      *distance,
                                              int           *ingress_index);

int main(int argc, char *argv[])
{
    int64_t start_time;
    int64_t total_time = 0;
    const int repeat = 100000;

    qd_bitmask_t *link_exclusions;
    uint32_t      distance;
    int           ingress_index = 0;

    qd_alloc_initialize();

    for (int i = 0; i < repeat; ++i) {

        qd_message_t *msg = build_message();
        qd_tracemask_t *tracemask = qd_tracemask();

        start_time = now_usec();

        qd_message_message_annotations(msg);

        qd_iterator_t *ingress_iter = router_annotate_message(false, tracemask, msg, &link_exclusions, &distance, &ingress_index);

        total_time += now_usec() - start_time;

        ingress_index = 0;
        qd_iterator_free(ingress_iter);
        qd_message_free(msg);
        qd_tracemask_free(tracemask);
    }

    printf("total time: %ld\n", total_time);
    printf("total reps: %d\n",  repeat);
    printf("avg time  : %.3f\n", (double)total_time/(double)repeat);

    return 0;
}
