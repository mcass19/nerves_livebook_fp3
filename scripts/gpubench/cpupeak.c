// NEON fp32 FMA peak per core set: cpupeak <cpu list, e.g. 4,5,6,7>
#define _GNU_SOURCE
#include <arm_neon.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define ITERS 50000000L

static void *work(void *arg)
{
	int cpu = (int)(long)arg;
	cpu_set_t set;
	CPU_ZERO(&set);
	CPU_SET(cpu, &set);
	pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
	float32x4_t k = vdupq_n_f32(0.999f), m = vdupq_n_f32(0.001f);
	float32x4_t a0 = vdupq_n_f32(1), a1 = a0, a2 = a0, a3 = a0, a4 = a0, a5 = a0, a6 = a0, a7 = a0;
	float32x4_t a8 = a0, a9 = a0, a10 = a0, a11 = a0;
	for (long i = 0; i < ITERS; i++) {
		a0 = vfmaq_f32(m, a0, k); a1 = vfmaq_f32(m, a1, k); a2 = vfmaq_f32(m, a2, k);
		a3 = vfmaq_f32(m, a3, k); a4 = vfmaq_f32(m, a4, k); a5 = vfmaq_f32(m, a5, k);
		a6 = vfmaq_f32(m, a6, k); a7 = vfmaq_f32(m, a7, k); a8 = vfmaq_f32(m, a8, k);
		a9 = vfmaq_f32(m, a9, k); a10 = vfmaq_f32(m, a10, k); a11 = vfmaq_f32(m, a11, k);
	}
	float32x4_t s = vaddq_f32(vaddq_f32(vaddq_f32(a0, a1), vaddq_f32(a2, a3)),
				  vaddq_f32(vaddq_f32(a4, a5), vaddq_f32(a6, a7)));
	s = vaddq_f32(s, vaddq_f32(vaddq_f32(a8, a9), vaddq_f32(a10, a11)));
	return (void *)(long)vgetq_lane_f32(s, 0);
}

int main(int argc, char **argv)
{
	int cpus[8], n = 0;
	char *label = strdup(argv[1]);
	for (char *t = strtok(argv[1], ","); t && n < 8; t = strtok(NULL, ","))
		cpus[n++] = atoi(t);
	pthread_t th[8];
	struct timespec t0, t1;
	clock_gettime(CLOCK_MONOTONIC, &t0);
	for (int i = 0; i < n; i++)
		pthread_create(&th[i], NULL, work, (void *)(long)cpus[i]);
	for (int i = 0; i < n; i++)
		pthread_join(th[i], NULL);
	clock_gettime(CLOCK_MONOTONIC, &t1);
	double s = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
	double flops = (double)n * ITERS * 12 * 4 * 2;
	printf("cpus %s: %.2f s  %.1f GFLOPS fp32\n", label, s, flops / s / 1e9);
	return 0;
}
