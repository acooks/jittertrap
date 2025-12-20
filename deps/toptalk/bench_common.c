/*
 * bench_common.c - Benchmark utilities implementation
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "bench_common.h"

/* Get CPU frequency from /proc/cpuinfo (Linux) */
uint64_t bench_get_cpu_freq(void)
{
	FILE *f = fopen("/proc/cpuinfo", "r");
	if (!f) {
		/* Fallback: assume 3 GHz */
		return 3000000000ULL;
	}

	char line[256];
	double mhz = 0.0;

	while (fgets(line, sizeof(line), f)) {
		if (strncmp(line, "cpu MHz", 7) == 0) {
			char *colon = strchr(line, ':');
			if (colon) {
				mhz = atof(colon + 1);
				break;
			}
		}
	}
	fclose(f);

	if (mhz < 100.0) {
		/* Fallback: assume 3 GHz */
		return 3000000000ULL;
	}

	return (uint64_t)(mhz * 1e6);
}

void bench_run(const char *name,
               void (*fn)(void *arg),
               void *arg,
               uint64_t iterations,
               struct bench_result *result)
{
	uint64_t freq = bench_get_cpu_freq();
	uint64_t start, end, total = 0;

	/* Warm up - run a few iterations to prime caches */
	for (uint64_t i = 0; i < 100 && i < iterations; i++) {
		fn(arg);
	}

	/* Timed run */
	start = bench_start();
	for (uint64_t i = 0; i < iterations; i++) {
		fn(arg);
	}
	end = bench_end();
	total = bench_cycles(start, end);

	/* Fill in results */
	result->name = name;
	result->iterations = iterations;
	result->total_cycles = total;
	result->cycles_per_op = (double)total / (double)iterations;
	result->ns_per_op = bench_cycles_to_ns(total, freq) / (double)iterations;
}

void bench_report_header(void)
{
	printf("\n%-40s %12s %12s %12s\n",
	       "Benchmark", "Iterations", "Cycles/op", "ns/op");
	printf("%-40s %12s %12s %12s\n",
	       "----------------------------------------",
	       "------------", "------------", "------------");
}

void bench_report(const struct bench_result *result)
{
	printf("%-40s %12lu %12.1f %12.1f\n",
	       result->name,
	       result->iterations,
	       result->cycles_per_op,
	       result->ns_per_op);
}
