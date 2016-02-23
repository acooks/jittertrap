#include <time.h>
#include <stdint.h>
#include <assert.h>
#include <stdlib.h>

#include "slist.h"

int lsize;

int slist_size(struct slist *head)
{
	return head->size;
}

void slist_push(struct slist *head, struct slist *new_tail)
{
	assert(head);
	assert(head->prev);
	assert(head->next);
	assert(new_tail);
	struct slist *old_tail = head->prev;

	new_tail->next = head;
	new_tail->prev = old_tail;

	old_tail->next = new_tail;
	head->prev = new_tail;
	head->size++;
}

struct slist *slist_pop(struct slist *head)
{
	assert(head);
	assert(head->next);
	assert(head->prev);
	struct slist *pop_node = head->next;
	assert(pop_node->next);
	assert(pop_node->prev);

	/* always keep the last (sentinel) element */
	if (pop_node == head) {
		return NULL;
	}

	head->next = pop_node->next;
	pop_node->next->prev = head;

	pop_node->next = NULL;
	pop_node->prev = NULL;

	assert(pop_node->s);
	head->size--;
	return pop_node;
}

struct slist *slist_new()
{
	struct slist *head = malloc(sizeof(struct slist));
	head->next = head;
	head->prev = head;
	head->s = NULL;
	head->size = 0;
	return head;
}

/* index is zero-based */
struct slist *slist_idx(struct slist *head, int idx)
{
	struct slist *ln = head;
	for (int i = idx + 1; i > 0; i--) {
		ln = ln->next;
	}
	return ln;
}

void slist_clear(struct slist *head)
{
	assert(head);
	assert(head->next);
	assert(head->prev);
	while (NULL != slist_pop(head)) {
		head->size--;
	}
	assert(0 == head->size);
}
