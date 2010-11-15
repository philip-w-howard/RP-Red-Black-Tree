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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <poll.h>
#include <pthread.h>

#include <atomic_ops.h>

#include <urcu.h>
#include <urcu-defer.h>

#define lock_mb()        AO_nop_full()

/*
 * Test variables.
 */

#ifndef NUM_CPUS
#define NUM_CPUS 16
#endif

#define MAX_THREADS 128

unsigned long **Values;
int Tree_Size   = 64;
int Tree_Scale  = 10000;

#define MODE_READONLY       0
#define MODE_WRITE          1
#define MODE_WRITE_SAME     2
#define MODE_WRITE_SEP      3

typedef struct
{
    pthread_t thread_id;
    int thread_index;
    int update_percent;
    int mode;
    int write_elem;
} thread_data_t;

#define GOFLAG_INIT 0
#define GOFLAG_RUN  1
#define GOFLAG_STOP 2

volatile int goflag = GOFLAG_INIT;

unsigned long get_random(unsigned long *seed)
{
    unsigned int val = *seed;
    val = (val*1103515245+12345)>>5;
    *seed = val;
    return val;
}
void init_tree_data(int count)
{
    int ii;
    unsigned long seed = 0;
    unsigned long value;

    Values = (unsigned long **)malloc(count*sizeof(unsigned long *));

    for (ii=0; ii<count; ii++)
    {
        value = get_random(&seed) % Tree_Scale + 1;
        Values[ii] = (unsigned long *)malloc(sizeof(unsigned long));
        *Values[ii] = value;
    }
}

void set_affinity(int cpu_number)
{
    int result;
    cpu_set_t cpu;
    while (cpu_number >= NUM_CPUS)
    {
        cpu_number -= NUM_CPUS;
    }
    CPU_ZERO(&cpu);
    CPU_SET(cpu_number, &cpu);
    result = sched_setaffinity(0, sizeof(cpu_set_t), &cpu);
    if (result != 0) 
    {
        printf("Affinity result %d %d %d\n", cpu_number, result, errno);
    }
}

void *perftest_thread(void *arg)
{
    thread_data_t *thread_data = (thread_data_t *)arg;

    int thread_index = thread_data->thread_index;
    int write_elem = thread_data->write_elem;
    int read_elem;
    unsigned long random_seed = 1234;

    unsigned long *value_ptr;
    unsigned long *new_value_ptr;
    unsigned long value;

    unsigned long long reads = 0;
    unsigned long long writes = 0;

    set_affinity(thread_index);
    rcu_register_thread();
    rcu_defer_register_thread();
    lock_mb();

	while (goflag == GOFLAG_INIT)
		poll(NULL, 0, 10);

    switch (thread_data->mode)
    {
        case MODE_READONLY:
            while (goflag == GOFLAG_RUN) 
            {
                read_elem = get_random(&random_seed) % Tree_Scale;
                value = *Values[read_elem];
                reads++;
            }
            break;
        case MODE_WRITE:
            while (goflag == GOFLAG_RUN)
            {
                write_elem = get_random(&random_seed) % Tree_Size;
                value_ptr = Values[write_elem];
                value = get_random(&random_seed) % Tree_Scale + 1;

                new_value_ptr = (unsigned long *)malloc(sizeof(unsigned long *));
                *new_value_ptr = value;

                rcu_assign_pointer(Values[write_elem], new_value_ptr);

                defer_rcu(free, value_ptr);
                writes++;
            }
            break;
    }

    rcu_unregister_thread();
    rcu_defer_unregister_thread();

    printf("thread %d reads %lld writes %lld\n", thread_index, reads, writes);
    return NULL;
}

/*
 * Mainprogram.
 */

void usage(int argc, char *argv[])
{
	fprintf(stderr, 
       "Usage: %s nthreads [[[READ | WRITE] init] max_elem]\n",

       argv[0]);
	exit(-1);
}

int main(int argc, char *argv[])
{
    //struct sched_param sched_params;

	int ii;
	int nthreads = 0;
    thread_data_t thread_data[MAX_THREADS];
    int delay = 1;
    int work_delay = 1;
    int mode = MODE_WRITE;

	if (argc > 1) nthreads = atoi(argv[1]);
    if (argc > 2)
    {
        if (strcmp(argv[2], "READ")==0)
            mode = MODE_READONLY;
        else if (strcmp(argv[2], "WRITE") == 0)
            mode = MODE_WRITE;
        else if (strcmp(argv[2], "RANDOM") == 0)
            mode = MODE_WRITE;
        else if (strcmp(argv[2], "SAME") == 0)
            mode = MODE_WRITE;
        else if (strcmp(argv[2], "SEP") == 0)
            mode = MODE_WRITE;
        else
            usage(argc, argv);
    }
	if (argc > 3) 
    {
        Tree_Size = atoi(argv[3]);
    }
    if (argc > 4) Tree_Scale = atoi(argv[4]);

	if (nthreads < 1 || Tree_Scale < 10)
    {
        usage(argc, argv);
	}

    init_tree_data(Tree_Size);

    for (ii=0; ii<MAX_THREADS; ii++)
    {
        thread_data[ii].thread_index = ii;
        //thread_data[ii].update_percent = update_percent;
        if (mode == MODE_WRITE && ii>0)
            thread_data[ii].mode = MODE_READONLY;
        else
            thread_data[ii].mode = mode;
        if (mode == MODE_WRITE_SAME)
            thread_data[ii].write_elem = Tree_Size/2;
        else if (mode == MODE_WRITE_SEP)
            thread_data[ii].write_elem = Tree_Size/2 - 3*nthreads/2 + ii*3;
    }

	for (ii = 0; ii < nthreads; ii++)
    {
		pthread_create(&thread_data[ii].thread_id, NULL,
                perftest_thread, &thread_data[ii]);
    }

	lock_mb();
	sleep(delay);
	goflag = GOFLAG_RUN;
	lock_mb();
	sleep(work_delay);
	lock_mb();
	goflag = GOFLAG_STOP;
	lock_mb();

	for (ii=0; ii<nthreads; ii++)
    {
        pthread_join(thread_data[ii].thread_id, NULL);
    }

    return 0;
}
