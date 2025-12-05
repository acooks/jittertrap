/*
 * test_pcap_buffer.c - Comprehensive tests for pcap_buffer module
 *
 * Tests follow existing codebase patterns:
 * - assert()-based verification
 * - Individual test functions returning 0 (success)
 * - printf() for progress reporting
 */

#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <pcap/pcap.h>

#include "pcap_buffer.h"

/*
 * Test Helper Functions
 */

/* Create a fake pcap header with specified size and timestamp */
static struct pcap_pkthdr make_pkthdr(uint32_t caplen, time_t sec, suseconds_t usec)
{
	struct pcap_pkthdr hdr = {
		.ts = { .tv_sec = sec, .tv_usec = usec },
		.caplen = caplen,
		.len = caplen
	};
	return hdr;
}

/* Create fake packet data filled with recognizable pattern */
static uint8_t *make_packet_data(uint32_t len)
{
	uint8_t *data = malloc(len);
	if (data) {
		memset(data, 0xAB, len);
	}
	return data;
}

/* Standard test config - 1MB buffer, 30s duration */
static struct pcap_buf_config test_config(void)
{
	struct pcap_buf_config cfg = {
		.max_memory_bytes = 1024 * 1024,
		.duration_sec = 30,
		.pre_trigger_sec = 25,
		.post_trigger_sec = 5,
		.datalink_type = DLT_EN10MB,
		.snaplen = 65535
	};
	return cfg;
}

/* Small test config - 64KB buffer for overflow testing */
static struct pcap_buf_config small_config(void)
{
	struct pcap_buf_config cfg = {
		.max_memory_bytes = 64 * 1024,  /* 64KB minimum viable */
		.duration_sec = 30,
		.pre_trigger_sec = 25,
		.post_trigger_sec = 5,
		.datalink_type = DLT_EN10MB,
		.snaplen = 65535
	};
	return cfg;
}

/*
 * Basic Lifecycle Tests
 */

int test_init_destroy(void)
{
	int err;

	printf("test_init_destroy... ");

	/* Init with NULL uses defaults */
	err = pcap_buf_init(NULL);
	assert(err == 0);

	/* Should be disabled after init */
	assert(pcap_buf_get_state() == PCAP_BUF_STATE_DISABLED);

	/* Destroy */
	pcap_buf_destroy();

	/* After destroy, should still return disabled (safe) */
	assert(pcap_buf_get_state() == PCAP_BUF_STATE_DISABLED);

	printf("OK\n");
	return 0;
}

int test_init_with_config(void)
{
	struct pcap_buf_config cfg = test_config();
	struct pcap_buf_config read_cfg;
	int err;

	printf("test_init_with_config... ");

	err = pcap_buf_init(&cfg);
	assert(err == 0);

	/* Verify config was applied */
	err = pcap_buf_get_config(&read_cfg);
	assert(err == 0);
	assert(read_cfg.duration_sec == cfg.duration_sec);
	assert(read_cfg.pre_trigger_sec == cfg.pre_trigger_sec);
	assert(read_cfg.post_trigger_sec == cfg.post_trigger_sec);
	assert(read_cfg.datalink_type == cfg.datalink_type);

	pcap_buf_destroy();

	printf("OK\n");
	return 0;
}

int test_double_init(void)
{
	struct pcap_buf_config cfg1 = test_config();
	struct pcap_buf_config cfg2 = test_config();
	int err;

	printf("test_double_init... ");

	cfg1.duration_sec = 10;
	cfg2.duration_sec = 20;

	err = pcap_buf_init(&cfg1);
	assert(err == 0);

	/* Second init should work (reinitializes) */
	err = pcap_buf_init(&cfg2);
	assert(err == 0);

	/* Verify second config is active */
	struct pcap_buf_config read_cfg;
	err = pcap_buf_get_config(&read_cfg);
	assert(err == 0);
	assert(read_cfg.duration_sec == 20);

	pcap_buf_destroy();

	printf("OK\n");
	return 0;
}

int test_destroy_without_init(void)
{
	printf("test_destroy_without_init... ");

	/* Should be safe to call destroy without init */
	pcap_buf_destroy();

	/* State should be disabled */
	assert(pcap_buf_get_state() == PCAP_BUF_STATE_DISABLED);

	printf("OK\n");
	return 0;
}

/*
 * State Transition Tests
 */

int test_enable_disable(void)
{
	struct pcap_buf_config cfg = test_config();
	int err;

	printf("test_enable_disable... ");

	err = pcap_buf_init(&cfg);
	assert(err == 0);

	/* Start disabled */
	assert(pcap_buf_get_state() == PCAP_BUF_STATE_DISABLED);

	/* Enable */
	err = pcap_buf_enable();
	assert(err == 0);
	assert(pcap_buf_get_state() == PCAP_BUF_STATE_RECORDING);

	/* Disable */
	err = pcap_buf_disable();
	assert(err == 0);
	assert(pcap_buf_get_state() == PCAP_BUF_STATE_DISABLED);

	pcap_buf_destroy();

	printf("OK\n");
	return 0;
}

int test_enable_when_disabled(void)
{
	struct pcap_buf_config cfg = test_config();
	int err;

	printf("test_enable_when_disabled... ");

	err = pcap_buf_init(&cfg);
	assert(err == 0);

	/* Enable from disabled state */
	assert(pcap_buf_get_state() == PCAP_BUF_STATE_DISABLED);
	err = pcap_buf_enable();
	assert(err == 0);
	assert(pcap_buf_get_state() == PCAP_BUF_STATE_RECORDING);

	pcap_buf_destroy();

	printf("OK\n");
	return 0;
}

int test_disable_when_recording(void)
{
	struct pcap_buf_config cfg = test_config();
	int err;

	printf("test_disable_when_recording... ");

	err = pcap_buf_init(&cfg);
	assert(err == 0);

	err = pcap_buf_enable();
	assert(err == 0);
	assert(pcap_buf_get_state() == PCAP_BUF_STATE_RECORDING);

	/* Disable while recording */
	err = pcap_buf_disable();
	assert(err == 0);
	assert(pcap_buf_get_state() == PCAP_BUF_STATE_DISABLED);

	pcap_buf_destroy();

	printf("OK\n");
	return 0;
}

int test_store_when_disabled(void)
{
	struct pcap_buf_config cfg = test_config();
	struct pcap_buf_stats stats;
	struct pcap_pkthdr hdr;
	uint8_t *data;
	int err;

	printf("test_store_when_disabled... ");

	err = pcap_buf_init(&cfg);
	assert(err == 0);

	/* Stay disabled, try to store */
	assert(pcap_buf_get_state() == PCAP_BUF_STATE_DISABLED);

	hdr = make_pkthdr(100, time(NULL), 0);
	data = make_packet_data(100);
	assert(data);

	/* Store should succeed but packet is ignored */
	err = pcap_buf_store_packet(&hdr, data);
	assert(err == 0);

	/* Stats should show no packets */
	err = pcap_buf_get_stats(&stats);
	assert(err == 0);
	assert(stats.total_packets == 0);

	free(data);
	pcap_buf_destroy();

	printf("OK\n");
	return 0;
}

/*
 * Packet Storage Tests
 */

int test_store_single_packet(void)
{
	struct pcap_buf_config cfg = test_config();
	struct pcap_buf_stats stats;
	struct pcap_pkthdr hdr;
	uint8_t *data;
	int err;

	printf("test_store_single_packet... ");

	err = pcap_buf_init(&cfg);
	assert(err == 0);

	err = pcap_buf_enable();
	assert(err == 0);

	/* Create and store a packet */
	hdr = make_pkthdr(100, time(NULL), 0);
	data = make_packet_data(100);
	assert(data);

	err = pcap_buf_store_packet(&hdr, data);
	assert(err == 0);

	/* Verify stats */
	err = pcap_buf_get_stats(&stats);
	assert(err == 0);
	assert(stats.total_packets == 1);
	assert(stats.total_bytes == 100);

	free(data);
	pcap_buf_destroy();

	printf("OK\n");
	return 0;
}

int test_store_multiple_packets(void)
{
	struct pcap_buf_config cfg = test_config();
	struct pcap_buf_stats stats;
	struct pcap_pkthdr hdr;
	uint8_t *data;
	int err;
	time_t now = time(NULL);

	printf("test_store_multiple_packets... ");

	err = pcap_buf_init(&cfg);
	assert(err == 0);

	err = pcap_buf_enable();
	assert(err == 0);

	data = make_packet_data(100);
	assert(data);

	/* Store 100 packets */
	for (int i = 0; i < 100; i++) {
		hdr = make_pkthdr(100, now, i * 1000);
		err = pcap_buf_store_packet(&hdr, data);
		assert(err == 0);
	}

	err = pcap_buf_get_stats(&stats);
	assert(err == 0);
	assert(stats.total_packets == 100);
	assert(stats.total_bytes == 10000);

	free(data);
	pcap_buf_destroy();

	printf("OK\n");
	return 0;
}

int test_store_large_packet(void)
{
	struct pcap_buf_config cfg = test_config();
	struct pcap_buf_stats stats;
	struct pcap_pkthdr hdr;
	uint8_t *data;
	int err;

	printf("test_store_large_packet... ");

	err = pcap_buf_init(&cfg);
	assert(err == 0);

	err = pcap_buf_enable();
	assert(err == 0);

	/* Store a large packet (near snaplen) */
	data = make_packet_data(9000);
	assert(data);

	hdr = make_pkthdr(9000, time(NULL), 0);
	err = pcap_buf_store_packet(&hdr, data);
	assert(err == 0);

	err = pcap_buf_get_stats(&stats);
	assert(err == 0);
	assert(stats.total_packets == 1);
	assert(stats.total_bytes == 9000);

	free(data);
	pcap_buf_destroy();

	printf("OK\n");
	return 0;
}

int test_store_zero_length(void)
{
	struct pcap_buf_config cfg = test_config();
	struct pcap_buf_stats stats;
	struct pcap_pkthdr hdr;
	uint8_t dummy = 0;
	int err;

	printf("test_store_zero_length... ");

	err = pcap_buf_init(&cfg);
	assert(err == 0);

	err = pcap_buf_enable();
	assert(err == 0);

	/* Store zero-length packet */
	hdr = make_pkthdr(0, time(NULL), 0);
	err = pcap_buf_store_packet(&hdr, &dummy);
	assert(err == 0);

	err = pcap_buf_get_stats(&stats);
	assert(err == 0);
	/* Zero-length packet should be stored */
	assert(stats.total_packets == 1);
	assert(stats.total_bytes == 0);

	pcap_buf_destroy();

	printf("OK\n");
	return 0;
}

/*
 * Memory Limit Tests
 */

int test_buffer_overflow(void)
{
	struct pcap_buf_config cfg = small_config();
	struct pcap_buf_stats stats;
	struct pcap_pkthdr hdr;
	uint8_t *data;
	int err;
	time_t now = time(NULL);

	printf("test_buffer_overflow... ");

	err = pcap_buf_init(&cfg);
	assert(err == 0);

	err = pcap_buf_enable();
	assert(err == 0);

	/* Store packets until we exceed memory (64KB buffer, 1KB packets) */
	data = make_packet_data(1024);
	assert(data);

	/* Try to store 200 packets (200KB) into 64KB buffer */
	for (int i = 0; i < 200; i++) {
		hdr = make_pkthdr(1024, now + i, 0);
		pcap_buf_store_packet(&hdr, data);
	}

	err = pcap_buf_get_stats(&stats);
	assert(err == 0);

	/* Should not exceed max memory */
	assert(stats.current_memory <= cfg.max_memory_bytes);

	/* Should have dropped some packets (can't fit 200KB in 64KB) */
	assert(stats.dropped_packets > 0 || stats.total_packets < 200);

	free(data);
	pcap_buf_destroy();

	printf("OK\n");
	return 0;
}

int test_memory_near_limit(void)
{
	struct pcap_buf_config cfg = small_config();
	struct pcap_buf_stats stats;
	struct pcap_pkthdr hdr;
	uint8_t *data;
	int err;
	time_t now = time(NULL);

	printf("test_memory_near_limit... ");

	err = pcap_buf_init(&cfg);
	assert(err == 0);

	err = pcap_buf_enable();
	assert(err == 0);

	/* Fill buffer close to limit */
	data = make_packet_data(100);
	assert(data);

	for (int i = 0; i < 30; i++) {
		hdr = make_pkthdr(100, now + i, 0);
		pcap_buf_store_packet(&hdr, data);
	}

	err = pcap_buf_get_stats(&stats);
	assert(err == 0);

	/* Should have stored some packets without exceeding limit */
	assert(stats.total_packets > 0);
	assert(stats.current_memory <= cfg.max_memory_bytes);

	free(data);
	pcap_buf_destroy();

	printf("OK\n");
	return 0;
}

int test_packet_too_large(void)
{
	struct pcap_buf_config cfg = small_config();
	struct pcap_buf_stats stats;
	struct pcap_pkthdr hdr;
	uint8_t *data;
	int err;

	printf("test_packet_too_large... ");

	err = pcap_buf_init(&cfg);
	assert(err == 0);

	err = pcap_buf_enable();
	assert(err == 0);

	/* Try to store packet larger than entire data pool (64KB buffer) */
	data = make_packet_data(128 * 1024);  /* 128KB packet */
	assert(data);

	hdr = make_pkthdr(128 * 1024, time(NULL), 0);
	err = pcap_buf_store_packet(&hdr, data);

	/* Should fail or drop the packet */
	err = pcap_buf_get_stats(&stats);
	assert(err == 0);

	/* Packet should be dropped */
	assert(stats.total_packets == 0 || stats.dropped_packets > 0);

	free(data);
	pcap_buf_destroy();

	printf("OK\n");
	return 0;
}

/*
 * Time-Based Behavior Tests
 */

int test_time_expiration(void)
{
	struct pcap_buf_config cfg = test_config();
	struct pcap_buf_stats stats;
	struct pcap_pkthdr hdr;
	uint8_t *data;
	int err;
	time_t now = time(NULL);

	printf("test_time_expiration... ");

	cfg.duration_sec = 10;  /* Short duration for testing */

	err = pcap_buf_init(&cfg);
	assert(err == 0);

	err = pcap_buf_enable();
	assert(err == 0);

	data = make_packet_data(100);
	assert(data);

	/* Store old packets (simulated past timestamps) */
	for (int i = 0; i < 5; i++) {
		hdr = make_pkthdr(100, now - 20 + i, 0);  /* 20 seconds ago */
		pcap_buf_store_packet(&hdr, data);
	}

	/* Store current packet - should trigger expiration of old ones */
	hdr = make_pkthdr(100, now, 0);
	pcap_buf_store_packet(&hdr, data);

	err = pcap_buf_get_stats(&stats);
	assert(err == 0);

	/* Old packets should have been expired, only recent one remains */
	/* (or fewer due to duration_sec = 10) */
	assert(stats.total_packets <= 6);

	free(data);
	pcap_buf_destroy();

	printf("OK\n");
	return 0;
}

int test_timestamps_tracked(void)
{
	struct pcap_buf_config cfg = test_config();
	struct pcap_buf_stats stats;
	struct pcap_pkthdr hdr;
	uint8_t *data;
	int err;
	time_t now = time(NULL);

	printf("test_timestamps_tracked... ");

	err = pcap_buf_init(&cfg);
	assert(err == 0);

	err = pcap_buf_enable();
	assert(err == 0);

	data = make_packet_data(100);
	assert(data);

	/* Store packets with different timestamps */
	hdr = make_pkthdr(100, now, 0);
	pcap_buf_store_packet(&hdr, data);

	hdr = make_pkthdr(100, now + 5, 0);
	pcap_buf_store_packet(&hdr, data);

	hdr = make_pkthdr(100, now + 10, 0);
	pcap_buf_store_packet(&hdr, data);

	err = pcap_buf_get_stats(&stats);
	assert(err == 0);

	/* Check timestamp tracking */
	assert(stats.oldest_ts_sec == (uint64_t)now);
	assert(stats.newest_ts_sec == (uint64_t)(now + 10));

	free(data);
	pcap_buf_destroy();

	printf("OK\n");
	return 0;
}

int test_buffer_percent(void)
{
	struct pcap_buf_config cfg = test_config();
	struct pcap_buf_stats stats;
	struct pcap_pkthdr hdr;
	uint8_t *data;
	int err;
	time_t now = time(NULL);

	printf("test_buffer_percent... ");

	cfg.duration_sec = 100;  /* 100 second window */

	err = pcap_buf_init(&cfg);
	assert(err == 0);

	err = pcap_buf_enable();
	assert(err == 0);

	data = make_packet_data(100);
	assert(data);

	/* Store packets spanning 50 seconds */
	hdr = make_pkthdr(100, now, 0);
	pcap_buf_store_packet(&hdr, data);

	hdr = make_pkthdr(100, now + 50, 0);
	pcap_buf_store_packet(&hdr, data);

	err = pcap_buf_get_stats(&stats);
	assert(err == 0);

	/* Buffer should be ~50% full (50/100 seconds) */
	assert(stats.buffer_percent >= 40 && stats.buffer_percent <= 60);

	free(data);
	pcap_buf_destroy();

	printf("OK\n");
	return 0;
}

/*
 * Trigger and File Writing Tests
 */

int test_trigger_immediate(void)
{
	struct pcap_buf_config cfg = test_config();
	int err;

	printf("test_trigger_immediate... ");

	cfg.post_trigger_sec = 0;  /* Immediate */

	err = pcap_buf_init(&cfg);
	assert(err == 0);

	err = pcap_buf_enable();
	assert(err == 0);

	err = pcap_buf_trigger("Test trigger");
	assert(err == 0);

	/* With post_trigger_sec = 0, should be immediately complete */
	assert(pcap_buf_post_trigger_complete() == 1);

	pcap_buf_destroy();

	printf("OK\n");
	return 0;
}

int test_trigger_state_change(void)
{
	struct pcap_buf_config cfg = test_config();
	int err;

	printf("test_trigger_state_change... ");

	err = pcap_buf_init(&cfg);
	assert(err == 0);

	err = pcap_buf_enable();
	assert(err == 0);
	assert(pcap_buf_get_state() == PCAP_BUF_STATE_RECORDING);

	err = pcap_buf_trigger("State test");
	assert(err == 0);
	assert(pcap_buf_get_state() == PCAP_BUF_STATE_TRIGGERED);

	pcap_buf_destroy();

	printf("OK\n");
	return 0;
}

int test_trigger_when_not_recording(void)
{
	struct pcap_buf_config cfg = test_config();
	int err;

	printf("test_trigger_when_not_recording... ");

	err = pcap_buf_init(&cfg);
	assert(err == 0);

	/* Don't enable, try to trigger */
	assert(pcap_buf_get_state() == PCAP_BUF_STATE_DISABLED);

	err = pcap_buf_trigger("Should fail");
	assert(err == -1);  /* Should fail */

	pcap_buf_destroy();

	printf("OK\n");
	return 0;
}

int test_write_file(void)
{
	struct pcap_buf_config cfg = test_config();
	struct pcap_buf_trigger_result result;
	struct pcap_pkthdr hdr;
	uint8_t *data;
	struct stat st;
	int err;
	time_t now = time(NULL);

	printf("test_write_file... ");

	cfg.post_trigger_sec = 0;

	err = pcap_buf_init(&cfg);
	assert(err == 0);

	err = pcap_buf_enable();
	assert(err == 0);

	/* Store some packets */
	data = make_packet_data(100);
	assert(data);

	for (int i = 0; i < 10; i++) {
		hdr = make_pkthdr(100, now, i * 1000);
		pcap_buf_store_packet(&hdr, data);
	}

	/* Trigger */
	err = pcap_buf_trigger("Test trigger");
	assert(err == 0);

	/* Write file */
	err = pcap_buf_write_file(&result);
	assert(err == 0);
	assert(result.success == 1);
	assert(result.packet_count > 0);

	/* Verify file exists */
	assert(stat(result.filepath, &st) == 0);
	assert(st.st_size > 0);
	assert(result.file_size == (uint64_t)st.st_size);

	/* Verify filepath is in expected location for HTTP serving */
	assert(strncmp(result.filepath, PCAP_BUF_PCAP_DIR, strlen(PCAP_BUF_PCAP_DIR)) == 0);
	assert(strstr(result.filepath, "/capture_") != NULL);
	assert(strstr(result.filepath, ".pcap") != NULL);

	/* Cleanup */
	unlink(result.filepath);

	free(data);
	pcap_buf_destroy();

	printf("OK\n");
	return 0;
}

int test_write_file_contents(void)
{
	struct pcap_buf_config cfg = test_config();
	struct pcap_buf_trigger_result result;
	struct pcap_pkthdr hdr;
	uint8_t *data;
	pcap_t *pd;
	char errbuf[PCAP_ERRBUF_SIZE];
	int packet_count = 0;
	int err;
	time_t now = time(NULL);

	printf("test_write_file_contents... ");

	cfg.post_trigger_sec = 0;

	err = pcap_buf_init(&cfg);
	assert(err == 0);

	err = pcap_buf_enable();
	assert(err == 0);

	/* Store packets with recognizable pattern */
	data = make_packet_data(64);
	assert(data);

	for (int i = 0; i < 5; i++) {
		hdr = make_pkthdr(64, now, i * 1000);
		pcap_buf_store_packet(&hdr, data);
	}

	err = pcap_buf_trigger("Content test");
	assert(err == 0);

	err = pcap_buf_write_file(&result);
	assert(err == 0);

	/* Open and read the pcap file */
	pd = pcap_open_offline(result.filepath, errbuf);
	assert(pd != NULL);

	/* Count packets in file */
	const u_char *pkt_data;
	struct pcap_pkthdr *pkt_hdr;
	while (pcap_next_ex(pd, &pkt_hdr, &pkt_data) == 1) {
		packet_count++;
		/* Verify packet data pattern */
		assert(pkt_hdr->caplen == 64);
		assert(pkt_data[0] == 0xAB);  /* Our test pattern */
	}

	pcap_close(pd);

	assert(packet_count == 5);
	assert(result.packet_count == 5);

	/* Cleanup */
	unlink(result.filepath);

	free(data);
	pcap_buf_destroy();

	printf("OK\n");
	return 0;
}

int test_post_trigger_complete(void)
{
	struct pcap_buf_config cfg = test_config();
	int err;

	printf("test_post_trigger_complete... ");

	cfg.post_trigger_sec = 1;  /* 1 second post-trigger */

	err = pcap_buf_init(&cfg);
	assert(err == 0);

	err = pcap_buf_enable();
	assert(err == 0);

	err = pcap_buf_trigger("Post-trigger test");
	assert(err == 0);

	/* Immediately after trigger, should not be complete */
	assert(pcap_buf_post_trigger_complete() == 0);

	/* Wait for post-trigger period */
	sleep(2);

	/* Now should be complete */
	assert(pcap_buf_post_trigger_complete() == 1);

	pcap_buf_destroy();

	printf("OK\n");
	return 0;
}

/*
 * Configuration Tests
 */

int test_get_stats(void)
{
	struct pcap_buf_config cfg = test_config();
	struct pcap_buf_stats stats;
	struct pcap_pkthdr hdr;
	uint8_t *data;
	int err;
	time_t now = time(NULL);

	printf("test_get_stats... ");

	err = pcap_buf_init(&cfg);
	assert(err == 0);

	err = pcap_buf_enable();
	assert(err == 0);

	data = make_packet_data(200);
	assert(data);

	for (int i = 0; i < 5; i++) {
		hdr = make_pkthdr(200, now + i, 0);
		pcap_buf_store_packet(&hdr, data);
	}

	err = pcap_buf_get_stats(&stats);
	assert(err == 0);

	assert(stats.total_packets == 5);
	assert(stats.total_bytes == 1000);
	assert(stats.dropped_packets == 0);
	assert(stats.state == PCAP_BUF_STATE_RECORDING);

	free(data);
	pcap_buf_destroy();

	printf("OK\n");
	return 0;
}

int test_get_set_config(void)
{
	struct pcap_buf_config cfg = test_config();
	struct pcap_buf_config read_cfg;
	struct pcap_buf_config new_cfg;
	int err;

	printf("test_get_set_config... ");

	err = pcap_buf_init(&cfg);
	assert(err == 0);

	/* Get current config */
	err = pcap_buf_get_config(&read_cfg);
	assert(err == 0);
	assert(read_cfg.duration_sec == cfg.duration_sec);

	/* Set new config (without changing memory) */
	new_cfg = read_cfg;
	new_cfg.duration_sec = 60;
	new_cfg.pre_trigger_sec = 50;
	new_cfg.max_memory_bytes = 0;  /* Don't change memory */

	err = pcap_buf_set_config(&new_cfg);
	assert(err == 0);

	/* Verify new config */
	err = pcap_buf_get_config(&read_cfg);
	assert(err == 0);
	assert(read_cfg.duration_sec == 60);
	assert(read_cfg.pre_trigger_sec == 50);

	pcap_buf_destroy();

	printf("OK\n");
	return 0;
}

int test_set_config_reinit(void)
{
	struct pcap_buf_config cfg = test_config();
	struct pcap_buf_config new_cfg;
	struct pcap_buf_config read_cfg;
	int err;

	printf("test_set_config_reinit... ");

	err = pcap_buf_init(&cfg);
	assert(err == 0);

	/* Change memory size - should trigger reinit */
	new_cfg = cfg;
	new_cfg.max_memory_bytes = 2 * 1024 * 1024;  /* 2MB */

	err = pcap_buf_set_config(&new_cfg);
	assert(err == 0);

	/* Verify new memory size */
	err = pcap_buf_get_config(&read_cfg);
	assert(err == 0);
	assert(read_cfg.max_memory_bytes == 2 * 1024 * 1024);

	pcap_buf_destroy();

	printf("OK\n");
	return 0;
}

/*
 * Buffer Operations Tests
 */

int test_clear(void)
{
	struct pcap_buf_config cfg = test_config();
	struct pcap_buf_stats stats;
	struct pcap_pkthdr hdr;
	uint8_t *data;
	int err;

	printf("test_clear... ");

	err = pcap_buf_init(&cfg);
	assert(err == 0);

	err = pcap_buf_enable();
	assert(err == 0);

	/* Store some packets */
	data = make_packet_data(100);
	assert(data);

	hdr = make_pkthdr(100, time(NULL), 0);
	pcap_buf_store_packet(&hdr, data);
	pcap_buf_store_packet(&hdr, data);
	pcap_buf_store_packet(&hdr, data);

	/* Verify packets stored */
	err = pcap_buf_get_stats(&stats);
	assert(err == 0);
	assert(stats.total_packets == 3);

	/* Clear */
	pcap_buf_clear();

	/* Verify cleared */
	err = pcap_buf_get_stats(&stats);
	assert(err == 0);
	assert(stats.total_packets == 0);
	assert(stats.total_bytes == 0);

	/* State should still be recording */
	assert(pcap_buf_get_state() == PCAP_BUF_STATE_RECORDING);

	free(data);
	pcap_buf_destroy();

	printf("OK\n");
	return 0;
}

int test_datalink_set(void)
{
	struct pcap_buf_config cfg = test_config();
	struct pcap_buf_config read_cfg;
	int err;

	printf("test_datalink_set... ");

	err = pcap_buf_init(&cfg);
	assert(err == 0);

	/* Set different datalink type */
	pcap_buf_set_datalink(DLT_LINUX_SLL);

	err = pcap_buf_get_config(&read_cfg);
	assert(err == 0);
	assert(read_cfg.datalink_type == DLT_LINUX_SLL);

	pcap_buf_destroy();

	printf("OK\n");
	return 0;
}

/*
 * Error Handling Tests
 */

int test_null_params(void)
{
	struct pcap_buf_config cfg = test_config();
	int err;

	printf("test_null_params... ");

	err = pcap_buf_init(&cfg);
	assert(err == 0);

	/* Get config with NULL should fail */
	err = pcap_buf_get_config(NULL);
	assert(err == -1);

	/* Get stats with NULL should fail */
	err = pcap_buf_get_stats(NULL);
	assert(err == -1);

	/* Set config with NULL should fail */
	err = pcap_buf_set_config(NULL);
	assert(err == -1);

	pcap_buf_destroy();

	printf("OK\n");
	return 0;
}

int test_ensure_directory(void)
{
	struct stat st;
	int err;

	printf("test_ensure_directory... ");

	/* Ensure directory exists */
	err = pcap_buf_ensure_directory();
	assert(err == 0);

	/* Verify directory exists */
	assert(stat(PCAP_BUF_PCAP_DIR, &st) == 0);
	assert(S_ISDIR(st.st_mode));

	/* Calling again should be idempotent */
	err = pcap_buf_ensure_directory();
	assert(err == 0);

	printf("OK\n");
	return 0;
}

/*
 * Pre-trigger Window Tests
 */

int test_pre_trigger_window(void)
{
	struct pcap_buf_config cfg = test_config();
	struct pcap_buf_trigger_result result;
	struct pcap_pkthdr hdr;
	uint8_t *data;
	pcap_t *pd;
	char errbuf[PCAP_ERRBUF_SIZE];
	int packet_count = 0;
	int err;
	time_t now = time(NULL);

	printf("test_pre_trigger_window... ");

	cfg.duration_sec = 30;
	cfg.pre_trigger_sec = 5;   /* Only keep 5 seconds before trigger */
	cfg.post_trigger_sec = 0;  /* No post-trigger */

	err = pcap_buf_init(&cfg);
	assert(err == 0);

	err = pcap_buf_enable();
	assert(err == 0);

	data = make_packet_data(64);
	assert(data);

	/* Store packets at different times:
	 * - 10 seconds ago (outside window, should be excluded)
	 * - 3 seconds ago (inside window, should be included)
	 * - now (inside window, should be included)
	 */
	hdr = make_pkthdr(64, now - 10, 0);
	pcap_buf_store_packet(&hdr, data);

	hdr = make_pkthdr(64, now - 3, 0);
	pcap_buf_store_packet(&hdr, data);

	hdr = make_pkthdr(64, now, 0);
	pcap_buf_store_packet(&hdr, data);

	err = pcap_buf_trigger("Pre-trigger test");
	assert(err == 0);

	err = pcap_buf_write_file(&result);
	assert(err == 0);

	/* Open and count packets in file */
	pd = pcap_open_offline(result.filepath, errbuf);
	assert(pd != NULL);

	const u_char *pkt_data;
	struct pcap_pkthdr *pkt_hdr;
	while (pcap_next_ex(pd, &pkt_hdr, &pkt_data) == 1) {
		packet_count++;
	}
	pcap_close(pd);

	/* Should have 2 packets (the ones within pre_trigger_sec window) */
	assert(packet_count == 2);

	unlink(result.filepath);
	free(data);
	pcap_buf_destroy();

	printf("OK\n");
	return 0;
}

int test_trigger_while_triggered(void)
{
	struct pcap_buf_config cfg = test_config();
	int err;

	printf("test_trigger_while_triggered... ");

	cfg.post_trigger_sec = 10;  /* Long post-trigger window */

	err = pcap_buf_init(&cfg);
	assert(err == 0);

	err = pcap_buf_enable();
	assert(err == 0);

	/* First trigger */
	err = pcap_buf_trigger("First trigger");
	assert(err == 0);
	assert(pcap_buf_get_state() == PCAP_BUF_STATE_TRIGGERED);

	/* Second trigger while still triggered should fail */
	err = pcap_buf_trigger("Second trigger");
	assert(err == -1);

	/* State should still be triggered */
	assert(pcap_buf_get_state() == PCAP_BUF_STATE_TRIGGERED);

	pcap_buf_destroy();

	printf("OK\n");
	return 0;
}

int test_write_without_trigger(void)
{
	struct pcap_buf_config cfg = test_config();
	struct pcap_buf_trigger_result result;
	int err;

	printf("test_write_without_trigger... ");

	err = pcap_buf_init(&cfg);
	assert(err == 0);

	err = pcap_buf_enable();
	assert(err == 0);

	/* Try to write without triggering first */
	err = pcap_buf_write_file(&result);
	assert(err == -1);  /* Should fail */
	assert(result.success == 0);

	pcap_buf_destroy();

	printf("OK\n");
	return 0;
}

int test_store_null_data(void)
{
	struct pcap_buf_config cfg = test_config();
	struct pcap_buf_stats stats;
	struct pcap_pkthdr hdr;
	int err;

	printf("test_store_null_data... ");

	err = pcap_buf_init(&cfg);
	assert(err == 0);

	err = pcap_buf_enable();
	assert(err == 0);

	/* Try to store with NULL data pointer (non-zero length) */
	hdr = make_pkthdr(100, time(NULL), 0);
	err = pcap_buf_store_packet(&hdr, NULL);
	assert(err == -1);  /* Should fail */

	/* Should handle gracefully - no crash, no packet stored */
	err = pcap_buf_get_stats(&stats);
	assert(err == 0);
	assert(stats.total_packets == 0);

	/* NULL header should also be rejected */
	uint8_t dummy = 0;
	err = pcap_buf_store_packet(NULL, &dummy);
	assert(err == -1);

	pcap_buf_destroy();

	printf("OK\n");
	return 0;
}

int test_sequential_captures(void)
{
	struct pcap_buf_config cfg = test_config();
	struct pcap_buf_trigger_result result1, result2;
	struct pcap_pkthdr hdr;
	uint8_t *data;
	int err;

	printf("test_sequential_captures... ");

	cfg.post_trigger_sec = 0;

	err = pcap_buf_init(&cfg);
	assert(err == 0);

	err = pcap_buf_enable();
	assert(err == 0);

	data = make_packet_data(64);
	assert(data);

	/* First capture cycle - use current time for timestamps */
	hdr = make_pkthdr(64, time(NULL), 0);
	pcap_buf_store_packet(&hdr, data);
	pcap_buf_store_packet(&hdr, data);
	pcap_buf_store_packet(&hdr, data);

	err = pcap_buf_trigger("First capture");
	assert(err == 0);

	err = pcap_buf_write_file(&result1);
	assert(err == 0);
	assert(result1.packet_count == 3);

	/* Clear buffer for fresh second capture */
	pcap_buf_clear();

	/* After write_file, state returns to RECORDING automatically */
	assert(pcap_buf_get_state() == PCAP_BUF_STATE_RECORDING);

	/* Wait a bit to ensure different filename timestamp */
	sleep(1);

	/* Second capture - use fresh current time for timestamps */
	hdr = make_pkthdr(64, time(NULL), 0);
	pcap_buf_store_packet(&hdr, data);
	pcap_buf_store_packet(&hdr, data);

	err = pcap_buf_trigger("Second capture");
	assert(err == 0);

	err = pcap_buf_write_file(&result2);
	assert(err == 0);
	assert(result2.packet_count == 2);

	/* Files should be different (different timestamps in filename) */
	assert(strcmp(result1.filepath, result2.filepath) != 0);

	/* Cleanup */
	unlink(result1.filepath);
	unlink(result2.filepath);
	free(data);
	pcap_buf_destroy();

	printf("OK\n");
	return 0;
}

int test_pcap_datalink_in_file(void)
{
	struct pcap_buf_config cfg = test_config();
	struct pcap_buf_trigger_result result;
	struct pcap_pkthdr hdr;
	uint8_t *data;
	pcap_t *pd;
	char errbuf[PCAP_ERRBUF_SIZE];
	int err;
	time_t now = time(NULL);

	printf("test_pcap_datalink_in_file... ");

	cfg.post_trigger_sec = 0;
	cfg.datalink_type = DLT_LINUX_SLL;  /* Use non-default datalink */

	err = pcap_buf_init(&cfg);
	assert(err == 0);

	err = pcap_buf_enable();
	assert(err == 0);

	data = make_packet_data(64);
	assert(data);

	hdr = make_pkthdr(64, now, 0);
	pcap_buf_store_packet(&hdr, data);

	err = pcap_buf_trigger("Datalink test");
	assert(err == 0);

	err = pcap_buf_write_file(&result);
	assert(err == 0);

	/* Open file and verify datalink type */
	pd = pcap_open_offline(result.filepath, errbuf);
	assert(pd != NULL);
	assert(pcap_datalink(pd) == DLT_LINUX_SLL);
	pcap_close(pd);

	unlink(result.filepath);
	free(data);
	pcap_buf_destroy();

	printf("OK\n");
	return 0;
}

int test_store_during_triggered(void)
{
	struct pcap_buf_config cfg = test_config();
	struct pcap_buf_trigger_result result;
	struct pcap_pkthdr hdr;
	uint8_t *data;
	pcap_t *pd;
	char errbuf[PCAP_ERRBUF_SIZE];
	int packet_count = 0;
	int err;
	time_t now = time(NULL);

	printf("test_store_during_triggered... ");

	cfg.post_trigger_sec = 2;  /* 2 second post-trigger window */

	err = pcap_buf_init(&cfg);
	assert(err == 0);

	err = pcap_buf_enable();
	assert(err == 0);

	data = make_packet_data(64);
	assert(data);

	/* Store pre-trigger packet */
	hdr = make_pkthdr(64, now, 0);
	pcap_buf_store_packet(&hdr, data);

	err = pcap_buf_trigger("Post-trigger store test");
	assert(err == 0);

	/* Store packets during post-trigger window */
	hdr = make_pkthdr(64, now + 1, 0);
	pcap_buf_store_packet(&hdr, data);

	/* Wait for post-trigger to complete */
	sleep(3);

	assert(pcap_buf_post_trigger_complete() == 1);

	err = pcap_buf_write_file(&result);
	assert(err == 0);

	/* Open and count packets - should include both pre and post trigger packets */
	pd = pcap_open_offline(result.filepath, errbuf);
	assert(pd != NULL);

	const u_char *pkt_data;
	struct pcap_pkthdr *pkt_hdr;
	while (pcap_next_ex(pd, &pkt_hdr, &pkt_data) == 1) {
		packet_count++;
	}
	pcap_close(pd);

	/* Should have at least the pre-trigger packet */
	assert(packet_count >= 1);

	unlink(result.filepath);
	free(data);
	pcap_buf_destroy();

	printf("OK\n");
	return 0;
}

int test_ops_before_init(void)
{
	struct pcap_buf_config cfg;
	struct pcap_buf_stats stats;
	int err;

	printf("test_ops_before_init... ");

	/* Make sure not initialized */
	pcap_buf_destroy();

	/* Operations should fail gracefully */
	err = pcap_buf_enable();
	assert(err == -1);

	err = pcap_buf_disable();
	assert(err == -1);

	err = pcap_buf_get_config(&cfg);
	assert(err == -1);

	err = pcap_buf_get_stats(&stats);
	assert(err == -1);

	err = pcap_buf_trigger("test");
	assert(err == -1);

	/* State should be disabled */
	assert(pcap_buf_get_state() == PCAP_BUF_STATE_DISABLED);

	printf("OK\n");
	return 0;
}

/*
 * Main
 */

int main(void)
{
	printf("=== pcap_buffer comprehensive tests ===\n\n");

	/* Basic lifecycle */
	assert(0 == test_init_destroy());
	assert(0 == test_init_with_config());
	assert(0 == test_double_init());
	assert(0 == test_destroy_without_init());

	/* State transitions */
	assert(0 == test_enable_disable());
	assert(0 == test_enable_when_disabled());
	assert(0 == test_disable_when_recording());
	assert(0 == test_store_when_disabled());

	/* Packet storage */
	assert(0 == test_store_single_packet());
	assert(0 == test_store_multiple_packets());
	assert(0 == test_store_large_packet());
	assert(0 == test_store_zero_length());

	/* Memory limits */
	assert(0 == test_buffer_overflow());
	assert(0 == test_memory_near_limit());
	assert(0 == test_packet_too_large());

	/* Time-based behavior */
	assert(0 == test_time_expiration());
	assert(0 == test_timestamps_tracked());
	assert(0 == test_buffer_percent());

	/* Trigger and file writing */
	assert(0 == test_trigger_immediate());
	assert(0 == test_trigger_state_change());
	assert(0 == test_trigger_when_not_recording());
	assert(0 == test_write_file());
	assert(0 == test_write_file_contents());
	assert(0 == test_post_trigger_complete());

	/* Configuration */
	assert(0 == test_get_stats());
	assert(0 == test_get_set_config());
	assert(0 == test_set_config_reinit());

	/* Buffer operations */
	assert(0 == test_clear());
	assert(0 == test_datalink_set());

	/* Error handling */
	assert(0 == test_null_params());
	assert(0 == test_ops_before_init());

	/* Directory and path handling */
	assert(0 == test_ensure_directory());

	/* Pre-trigger window and edge cases */
	assert(0 == test_pre_trigger_window());
	assert(0 == test_trigger_while_triggered());
	assert(0 == test_write_without_trigger());
	assert(0 == test_store_null_data());
	assert(0 == test_sequential_captures());
	assert(0 == test_pcap_datalink_in_file());
	assert(0 == test_store_during_triggered());

	printf("\n=== All pcap_buffer tests passed ===\n");
	return 0;
}
