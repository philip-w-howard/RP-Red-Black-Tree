#ifndef __RBMAIN_H
#define __RBMAIN_H

//Copyright (c) 2010 Philip W. Howard
//
//Permission is hereby granted, free of charge, to any person obtaining a copy
//of this software and associated documentation files (the "Software"), to deal
//in the Software without restriction, including without limitation the rights
//to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//copies of the Software, and to permit persons to whom the Software is
//furnished to do so, subject to the following conditions:
//
//The above copyright notice and this permission notice shall be included in
//all copies or substantial portions of the Software.
//
//THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
//THE SOFTWARE.

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
#define MODE_TRAVERSE       4
#define MODE_TRAVERSEN      5

#define UPDATE_MAX          1000000 // used to compute update percent rate
typedef struct
{
    pthread_t thread_id;
    int thread_index;
    int update_percent;             // number out of UPDATE_MAX that are updates
    int insert_percent;
    int delete_percent;
    int mode;
    int write_elem;
    void *lock;
    int done;
} thread_data_t;

typedef struct
{
    int size;
    int scale;
    int runtime;
    int mode;
    int cpus;
    int readers;
    int writers;
    int poll_rcu;
    int update_percent;             // number out of UPDATE_MAX that are updates
    int insert_percent;
    int delete_percent;
    int stm_stats;
    char name[100];
} param_t;

unsigned long init_random_seed();
unsigned long get_random(unsigned long *seed);

#endif

