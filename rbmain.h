#ifndef __RBMAIN_H
#define __RBMAIN_H

#define _GNU_SOURCE
#include <sched.h>
#include <errno.h>
#include <assert.h>
#include <time.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <poll.h>
#include <pthread.h>

#include "lock.h"
#include "rbtree.h"
#include "atomic_ops.h"

/*
 * Test variables.
 */

#ifdef __sun__
#define NUM_CPUS 64
#else
#define NUM_CPUS 16
#endif

#define MAX_STATS       20
#define MAX_THREADS     128
#define NUM_UPDATE_OPS  3

unsigned long *Values;

typedef struct
{
    __attribute__((__aligned__(CACHE_LINE_SIZE))) long long count;
} thread_counter_t;

#define MODE_READ           0
#define MODE_WRITE          1
#define MODE_RANDOM         2
#define MODE_NOOP           3

#define UPDATE_MAX          1000000 // used to compute update percent rate
typedef struct
{
    pthread_t thread_id;
    int thread_index;
    int update_percent;             // number out of UPDATE_MAX that are updates
    int mode;
    int write_elem;
    void *lock;
    int done;
} thread_data_t;

typedef struct
{
    int size;
    int scale;
    int delay;
    int mode;
    int cpus;
    int readers;
    int writers;
    int poll_rcu;
    int update_percent;             // number out of UPDATE_MAX that are updates
} param_t;

unsigned long init_random_seed();
unsigned long get_random(unsigned long *seed);

#endif

