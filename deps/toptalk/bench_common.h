/*
 * bench_common.h - Benchmark utilities for performance measurement
 *
 * Provides:
 * - Cycle-accurate timing using rdtsc (x86_64)
 * - Wall-clock timing utilities
 * - Benchmark runner and reporting
 */

#ifndef BENCH_COMMON_H
#define BENCH_COMMON_H

#include <stdint.h>
#include <stdio.h>

/* Benchmark result structure */
struct bench_result {
	const char *name;
	uint64_t iterations;
	uint64_t total_cycles;
	double cycles_per_op;
	double ns_per_op;
};

/*
 * Read CPU timestamp counter (x86_64).
 * Returns current cycle count. Use bench_cycles() to compute elapsed.
 */
static inline uint64_t bench_start(void)
{
	uint32_t lo, hi;
	/* Serialize to ensure timing is accurate */
	__asm__ volatile (
		"cpuid\n\t"
		"rdtsc\n\t"
		: "=a" (lo), "=d" (hi)
		: "a" (0)
		: "rbx", "rcx"
	);
	return ((uint64_t)hi << 32) | lo;
}

/*
 * Read timestamp counter at end of measurement.
 * Uses rdtscp for better serialization on modern CPUs.
 */
static inline uint64_t bench_end(void)
{
	uint32_t lo, hi;
	__asm__ volatile (
		"rdtscp\n\t"
		"mov %%eax, %0\n\t"
		"mov %%edx, %1\n\t"
		"cpuid\n\t"
		: "=r" (lo), "=r" (hi)
		:
		: "rax", "rbx", "rcx", "rdx"
	);
	return ((uint64_t)hi << 32) | lo;
}

/*
 * Compute elapsed cycles between start and end.
 */
static inline uint64_t bench_cycles(uint64_t start, uint64_t end)
{
	return end - start;
}

/*
 * Get approximate CPU frequency in Hz.
 * Uses /proc/cpuinfo on Linux.
 */
uint64_t bench_get_cpu_freq(void);

/*
 * Convert cycles to nanoseconds given CPU frequency.
 */
static inline double bench_cycles_to_ns(uint64_t cycles, uint64_t freq_hz)
{
	return (double)cycles * 1e9 / (double)freq_hz;
}

/*
 * Run a benchmark function multiple times and collect statistics.
 *
 * name: Benchmark name for reporting
 * fn: Function to benchmark (called with arg)
 * arg: Argument passed to fn
 * iterations: Number of times to call fn
 * result: Output benchmark results
 */
void bench_run(const char *name,
               void (*fn)(void *arg),
               void *arg,
               uint64_t iterations,
               struct bench_result *result);

/*
 * Print benchmark results in a formatted table.
 */
void bench_report(const struct bench_result *result);

/*
 * Print header for benchmark report table.
 */
void bench_report_header(void);

/*
 * Prevent compiler from optimizing away a value.
 * Use to ensure benchmark results are "used".
 */
#define BENCH_DONT_OPTIMIZE(val) \
	__asm__ volatile ("" : : "r,m" (val) : "memory")

/*
 * Memory barrier to prevent reordering.
 */
#define BENCH_BARRIER() \
	__asm__ volatile ("" ::: "memory")

#endif /* BENCH_COMMON_H */
