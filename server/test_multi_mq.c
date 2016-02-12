#include <time.h>
#include <inttypes.h>
#include <stdio.h>
#include <assert.h>

#include "mq_msg_ws.h"
#include "mq_msg_stats.h"

/* a callback for consuming messages. */
int ws_message_printer(struct mq_ws_msg *m,
                       void *data __attribute__((unused)))
{
        assert(m);
        printf("m: %s\n", m->m);
        return 0;
}

/* a callback for consuming messages. */
int stats_message_printer(struct mq_stats_msg *m,
                       void *data __attribute__((unused)))
{
        assert(m);
        printf("m: %"PRId64 "\n", m->window);
        return 0;
}


int test_mq_msg_ws(void)
{
        char msg[MAX_JSON_MSG_LEN];
	int err, cb_err;
        unsigned long id;

        printf("test for consume-from-empty case\n");
        err = mq_ws_init();
        assert(!err);

        err = mq_ws_consumer_subscribe(&id);
        assert(!err);

	err = mq_ws_consume(id, ws_message_printer, msg, &cb_err);
        assert(-JT_WS_MQ_EMPTY == err);

        err = mq_ws_consumer_unsubscribe(id);
        assert(!err);

        err = mq_ws_destroy();
        assert(!err);

        printf("OK.\n");

	return 0;
}

int test_mq_msg_stats(void)
{
        char msg[MAX_JSON_MSG_LEN];
	int err, cb_err;
        unsigned long id;

        printf("test for consume-from-empty case\n");
        err = mq_stats_init();
        assert(!err);

        err = mq_stats_consumer_subscribe(&id);
        assert(!err);

	err = mq_stats_consume(id, stats_message_printer, msg, &cb_err);
        assert(-JT_WS_MQ_EMPTY == err);

        err = mq_stats_consumer_unsubscribe(id);
        assert(!err);

        err = mq_stats_destroy();
        assert(!err);

        printf("OK.\n");

	return 0;
}



int main(void)
{
	test_mq_msg_ws();
	test_mq_msg_stats();
	return 0;
}
