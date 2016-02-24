#ifndef TIME_SERIES_H
#define TIME_SERIES_H

#if 0
struct series_sample {
	struct timespec timestamp;
	int64_t window;
	int64_t mean_rx_bytes;
	int64_t mean_tx_bytes;
	int64_t mean_rx_packets;
	int64_t mean_tx_packets;
	int64_t min_rx_bytes;
	int64_t min_tx_bytes;
	int64_t min_rx_packets;
	int64_t min_tx_packets;
	int64_t max_rx_bytes;
	int64_t max_tx_bytes;
	int64_t max_rx_packets;
	int64_t max_tx_packets;
};

#endif

struct slist {
	struct sample *s;
	struct slist *next;
	struct slist *prev;
	int size;
};

void slist_push(struct slist *head, struct slist *new_tail);
int slist_size(struct slist *head);

struct slist *slist_pop(struct slist *head);
struct slist *slist_new();
struct slist *slist_idx(struct slist *head, int idx);
void slist_clear(struct slist *head);

#endif
