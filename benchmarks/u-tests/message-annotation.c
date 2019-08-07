#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <ctype.h>
#include <string.h>
#include <arpa/inet.h>

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

static inline uint32_t get_32(const void *ptr)
{
    uint32_t rc;
    memcpy(&rc, ptr, 4);
    return ntohl(rc);
}

int get_type_info_fast(qd_iterator_pointer_t *iptr, uint8_t *tag, uint32_t *size, uint32_t *count)
{
    uint8_t *cursor  = iptr->cursor;
    *tag             = *cursor++;
    *count           = 0;
    *size            = 0;

    switch (*tag & 0xF0) {
    case 0x40:
        *size = 0;
        break;
    case 0x50:
        *size = 1;
        break;
    case 0x60:
        *size = 2;
        break;
    case 0x70:
        *size = 4;
        break;
    case 0x80:
        *size = 8;
        break;
    case 0x90:
        *size = 16;
        break;
    case 0xB0:
    case 0xD0:
    case 0xF0:
        *size = get_32(cursor);
        cursor += 4;
        break;

    case 0xA0:
    case 0xC0:
    case 0xE0:
        *size = *cursor++;
        break;

    default:
        return -1;
    }

    switch (*tag & 0xF0) {
    case 0xD0:
    case 0xF0:
        *count = get_32(cursor);
        cursor += 4;
        break;

    case 0xC0:
    case 0xE0:
        *count = *cursor++;
        break;
    }

    iptr->remaining -= cursor - iptr->cursor;
    iptr->cursor = cursor;
    return 0;
}




qd_message_t *build_message(void)
{
    qd_composed_field_t *f = qd_compose(QD_PERFORMATIVE_HEADER, 0);
    qd_compose_start_list(f);
    qd_compose_insert_bool(f, 0);
    qd_compose_end_list(f);

    /// MESSAGE ANNOTATIONS BEGIN
    f = qd_compose(QD_PERFORMATIVE_MESSAGE_ANNOTATIONS, f);
    qd_compose_start_map(f);

    qd_compose_insert_symbol(f, "key");
    qd_compose_insert_ulong(f, 0x555);

    // begin TRACE
    qd_compose_insert_string(f, QD_MA_TRACE);
    qd_compose_start_list(f);

    qd_compose_insert_string(f, "TestRouterA");
    qd_compose_end_list(f);

    qd_compose_end_map(f);
    /// MESSAGE ANNOTATIONS END

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
    //const int repeat = 100000;
    const int repeat = 1;

    //qd_bitmask_t *link_exclusions;
    //uint32_t      distance;
    //int           ingress_index = 0;

    qd_alloc_initialize();

    for (int i = 0; i < repeat; ++i) {

        qd_message_t *msg = build_message();
        qd_tracemask_t *tracemask = qd_tracemask();

        start_time = now_usec();

        //qd_message_message_annotations(msg);
        qd_iterator_t *ma_field_iter = qd_message_field_iterator(msg, QD_FIELD_MESSAGE_ANNOTATION);
        qd_iterator_pointer_t iptr;

        qd_iterator_get_view_cursor(ma_field_iter, &iptr);

        ptrdiff_t offset = iptr.cursor - qd_buffer_base(iptr.buffer);
        if (offset + iptr.remaining <= qd_buffer_size(iptr.buffer)) {
            printf("size=%lu\n", qd_buffer_size(iptr.buffer));
            printf("rem=%d\n", iptr.remaining);
            printf("offset=%ld\n", offset);
        } else {
            exit(-1);
        }

        uint8_t  tag;
        uint32_t size;
        uint32_t count;

        if (get_type_info_fast(&iptr, &tag, &size, &count) != 0)
            exit(-1);

        if (tag != QD_AMQP_MAP8 && tag != QD_AMQP_MAP32) {
            printf("OOPSIE\n");
            exit(-1);
        }
        printf("MAP%s size=%u count=%u\n", tag == QD_AMQP_MAP8 ? "8" : "32", size, count);
        qd_iterator_pointer_t ma_ingress;
        qd_iterator_pointer_t ma_trace;
        qd_iterator_pointer_t ma_to;
        qd_iterator_pointer_t ma_phase;
        int user_ma_count = 0;
        uint8_t *user_ma_start = 0;
        uint32_t user_ma_size = 0;

        user_ma_start = iptr.cursor;

        for (int j = 0; j < count; ++j) {

            // key
            uint8_t sub_tag;
            uint32_t sub_size;
            uint32_t sub_count;
            if (get_type_info_fast(&iptr, &sub_tag, &sub_size, &sub_count) != 0) {
                exit(-1);
            }
            printf("KEY SUB-TAG: 0x%X  SUB-SIZE: %u  SUB-COUNT %u\n", sub_tag, sub_size, sub_count);

            iptr.cursor += sub_size;
            iptr.remaining -= sub_size;

            // value
            if (get_type_info_fast(&iptr, &sub_tag, &sub_size, &sub_count) != 0) {
                exit(-1);
            }
            printf("VALUE SUB-TAG: 0x%X  SUB-SIZE: %u  SUB-COUNT %u\n", sub_tag, sub_size, sub_count);
            
            iptr.cursor += sub_size;
            iptr.remaining -= sub_size;

#if 0
            switch (sub_tag) {
            case QD_AMQP_STR8_UTF8:
            case QD_AMQP_SYM8:
            case QD_AMQP_STR32_UTF8:
            case QD_AMQP_SYM32:
                break;

            default:
                user_ma_count++;
            }
#endif
        }


        //qd_iterator_t *ingress_iter = router_annotate_message(false, tracemask, msg, &link_exclusions, &distance, &ingress_index);

        total_time += now_usec() - start_time;

        //ingress_index = 0;
        //qd_iterator_free(ingress_iter);
        qd_iterator_free(ma_field_iter);
        qd_message_free(msg);
        qd_tracemask_free(tracemask);
    }

    printf("total time: %ld\n", total_time);
    printf("total reps: %d\n",  repeat);
    printf("avg time  : %.3f\n", (double)total_time/(double)repeat);

    return 0;
}
