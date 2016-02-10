#ifndef MQ_MSG_WS_H
#define MQ_MSG_WS_H

#define MAX_CONSUMERS 32
#define MAX_Q_DEPTH 32
#define MAX_JSON_MSG_LEN 3000

struct jtmq_msg {
        char m[MAX_JSON_MSG_LEN];
};

#endif
