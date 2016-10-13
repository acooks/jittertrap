#ifndef INTERVALS_USER_H
#define INTERVALS_USER_H

/* In intervals.h there is a definition for struct tt_top_flows that looks
 * something like this:
 *
 * struct tt_top_flows {
 *         int count;
 *         struct flow_record flow[MAX_FLOW_COUNT][INTERVAL_COUNT];
 * };
 *
 * MAX_FLOW_COUNT and INTERVAL_COUNT must be user-defined
 * and used when accessing .flow inside struct tt_top_flows.
 */

#define INTERVAL_COUNT 8
#define MAX_FLOW_COUNT 20

#endif
