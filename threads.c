/*-
 * Copyright (c) 2016 Semihalf.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "bench.h"

struct list {
	struct list *next;
	long val;
};
#define MAX_THREADS 2

static struct list *lists[MAX_THREADS];

void nonatominc_inc(struct list *l, size_t n)
{
	for (size_t i=0; i < n; i++) {
		l->val += 1;
		l = l->next;
	}
}

void atomic_add(struct list *l, size_t n)
{
	for (size_t i=0; i < n; i++) {
		__atomic_fetch_add(&l->val, 1, __ATOMIC_RELAXED);
		l = l->next;
	}
}

void atomic_rw(struct list *l, size_t n, bool modify)
{
	for (size_t i=0; i < n; i++) {
		if (modify) {
			l->val += 1;
		} else {
			__atomic_load_n(&l->val, __ATOMIC_RELAXED);
		}
		l = l->next;
	}
}

void benchmark_a(struct thrarg *thr)
{
	struct list *l = lists[thr->params.id];
	size_t n  = thr->params.iters;

	nonatominc_inc(l, n);
}

void benchmark_s(struct thrarg *thr)
{
	struct list *l = lists[thr->params.id];
	size_t n  = thr->params.iters;
	atomic_add(l, n);
}

void benchmark_w(struct thrarg *thr)
{
	unsigned id = thr->params.id;
	struct list *l = lists[id];
	size_t n  = thr->params.iters;

	atomic_rw(l, n, (bool)id);
}

void benchmark_r(struct thrarg *thr)
{
	size_t i;
	long r = 0;
	unsigned id = thr->params.id;
	struct list *l = lists[id];
	size_t n  = thr->params.iters;

	for (i=0; i < n; i++) {
		r = l->val;
		l = l->next;
	}
	USE(r);
}

void init(struct thrarg *arg)
{
	(void)*arg;
}

static void usage()
{
	fprintf(stderr, "Usage:\tthreads <padding> s|r|w|a\n");
}

static void exit_usage()
{
	usage();
	exit(1);
}

int main(int argc, char **argv)
{
	unsigned i;
	unsigned nthreads = MAX_THREADS;
	unsigned pad;
	benchmark_t benchmark;
	char *mem;

	if (argc != 3)
		exit_usage();

	if ( sscanf(argv[1], "%u", &pad) < 1)
		exit_usage();

	if (pad < sizeof(struct list) && pad != 0)
		exit_usage();

	switch(argv[2][0]) {
	case 's':
		benchmark = benchmark_s;
		break;
	case 'r':
		benchmark = benchmark_r;
		break;
	case 'w':
		benchmark = benchmark_w;
		break;
	case 'a':
		benchmark = benchmark_a;
		break;
	default:
		return 1;
	}

	if (posix_memalign((void **)&mem, 128, nthreads*pad)) {
		perror("posix_memalign");
		return 1;
	}

	memset(mem, 0, nthreads*pad);
	for (i=0; i < nthreads; i++) {
		struct list *l = (struct list *)&mem[i*pad];
		l->next = l;
		lists[i] = l;
	}

	const char *bp = getenv("BENCH_PRINT");
	bool print_samples = (bp && strcmp(bp,"y") == 0) ? true : false;

	struct thrarg thrarg = { .params = {
		.threads = nthreads,
		.benchmark = benchmark,
		.init = init,
		.print_samples = print_samples,
		.max_samples = 100,
		.min_time =  10*1000*1000,
		.max_error = 10,
	}};

	int err = benchmark_auto(&thrarg);
	if (err < 0) {
		fprintf(stderr, "Bench error %s\n", strerror(err));
		return 1;
	}
	printf("%u %.2f %.2f %.2f\n", pad, thrarg.result.avg, thrarg.result.err, thrarg.result.u);

	return 0;
}
