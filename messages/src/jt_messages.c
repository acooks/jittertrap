#include <string.h>
#include <jansson.h>

#include "jt_messages.h"

/*
 * FIXME: This will break when another top-level key:value pair is present.
 *
 * All messages must have format:
 * {'msg':'type', 'p':{}}
 */
int jt_msg_match_type(json_t *root, int type_id)
{
        json_t *msg_type;
        int cmp;
        msg_type = json_object_get(root, "msg");
        if (!msg_type || JSON_STRING != json_typeof(msg_type)) {
                fprintf(stderr, "not a jt message\n");
                return -1;
        }
        cmp = strncmp(jt_messages[type_id].key, json_string_value(msg_type),
                       strlen(jt_messages[type_id].key));
        if (cmp != 0) {
#if DEBUG
                fprintf(stderr, "[%s] type doesn't match [%s]\n",
                                jt_messages[type_id].key,
                                json_string_value(msg_type));
#endif
                return -1;
        }
        return 0;
}

