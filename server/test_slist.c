#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <stdio.h>

#define MAX_IFACE_LEN 8
#define SAMPLES_PER_FRAME 1
#include "iface_stats.h"
#include "slist.h"

#define NODECNT 10

int main(int argc, char *argv[])
{
	struct slist *list;

	list = slist_new();
	assert(list);

	assert(0 == slist_size(list));

	assert(list == slist_idx(list, 0));

	assert(NULL == slist_idx(list, -1));

	assert(NULL == slist_idx(list, 1));

	/* Build an array and set canary values */
	struct slist *nodes1[NODECNT];
	for (int i = 0; i < NODECNT; i++) {
		nodes1[i] = malloc(sizeof(struct slist));
		nodes1[i]->s = malloc(sizeof(struct sample));
		nodes1[i]->s->rx_bytes = i;
	}

	/* Then add array elements to a list */
	for (int i = 0; i < NODECNT; i++) {
		slist_push(list, nodes1[i]);

		assert(1 + i == slist_size(list));
	}

	/* check slist_size */
	assert(NODECNT == slist_size(list));

	/* check the list_idx function */
	for (int i = 0; i < NODECNT; i++) {
		struct slist *n = slist_idx(list, i);
		assert(n);
		assert(n->s->rx_bytes == i);
	}

	/* check slist_clear */
	slist_clear(list);
	assert(0 == slist_size(list));

}
