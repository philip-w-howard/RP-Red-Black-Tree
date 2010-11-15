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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <poll.h>
#include <pthread.h>
#include <time.h>

#include "lock.h"
#include "avl.h"
#include "atomic_ops.h"
#include "rcu.h"

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

#define MODE_READONLY       0
#define MODE_WRITE          1
#define MODE_TRAVERSE       2
#define MODE_TRAVERSEN      3

typedef struct
{
    pthread_t thread_id;
    int thread_index;
    int update_percent;
    int mode;
    int write_elem;
    void *lock;
} thread_data_t;

#define GOFLAG_INIT 0
#define GOFLAG_RUN  1
#define GOFLAG_STOP 2

volatile int goflag = GOFLAG_INIT;
avl_node_t My_Tree;

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
} param_t;

param_t Params = {64, 128, 1, MODE_READONLY, NUM_CPUS, 0, 0, 0};

#define RING_SIZE 50

typedef struct
{
    char op[RING_SIZE];
    long value[RING_SIZE];
    int head;
    int count;
} op_ring_buff_t;
void ring_insert(op_ring_buff_t *ring, char op, long value)
{
    ring->op[ring->head] = op;
    ring->value[ring->head] = value;
    ring->head++;
    if (ring->head >= RING_SIZE) ring->head = 0;
    ring->count++;
}
void ring_output(op_ring_buff_t *ring)
{
    int count = ring->count;
    int head  = ring->head;
    if (count > RING_SIZE) count = RING_SIZE;

    printf("ring: ");
    while (count-- > 0)
    {
        head--;
        if (head < 0) head = RING_SIZE-1;
        printf(" %c%ld", ring->op[head], ring->value[head]);
    }
    printf("\n");
}
//#define RANDOM_SEED  ( (unsigned long)time(NULL) ) // 12390325
static unsigned long init_random_seed()
{
    unsigned long seed;
    struct timespec cur_time;

    clock_gettime(CLOCK_REALTIME, &cur_time);
    seed = cur_time.tv_sec + cur_time.tv_nsec;

    printf("random seed: %ld 0x%08lX\n", seed, seed);
    return seed;
}

unsigned long get_random2(unsigned long *seed)
{
    unsigned long a1 = *seed;
    unsigned long a2, a3;

    assert(a1 != 0);

    a1 = a1 ^ (a1 << 21);
    a2 = a1 ^ (a1 >> 17);
    a3 = a2 ^ (a2 << 4);

    *seed = a3;

    return a3;
}

unsigned long get_random(unsigned long *seed)
{
    unsigned int val = *seed;
    val = val*214013+2531011;
    *seed = val;
    return val>>5 & 0x7FFFFFFF;
}

unsigned long get_random1(unsigned long *seed)
{
    unsigned int val = *seed;
    val = (val*1103515245+12345)>>5;
    *seed = val;
    return val;
}
static volatile int Global = 0;

void waste_time()
{
    static unsigned long seed = 7876579;
    int count;
    int ii;

    count = get_random(&seed) % 5000 + 5;
    for (ii=0; ii<count; ii++)
    {
        Global++;
        lock_mb();
    }
}
void init_tree_data(int count, void *lock)
{
    int ii;
    unsigned long seed = init_random_seed(); // random();
    unsigned long value;

    avl_create(&My_Tree, lock);

    for (ii=0; ii<count; ii++)
    {
        value = get_random(&seed) % Params.scale + 1;
        while ( !avl_insert(&My_Tree, value, (void *)value) )
        {
            value = get_random(&seed) % Params.scale + 1;
        }
    }
}

#ifdef __sparc__
#include <sys/types.h>
#include <sys/processor.h>
#include <sys/procset.h>

void set_affinity(int cpu_number)
{
    int result;
    int core;

    //core = (cpu_number*8 + cpu_number/8) % Params.cpus;
    core = cpu_number % Params.cpus;
    result = processor_bind(P_LWPID, P_MYID, core, NULL);
    if (result != 0) 
    {
        printf("Affinity result %d %d %d %d\n", result, errno, cpu_number,core);
    }
}
#else
void set_affinity(int cpu_number)
{
    int result;
    cpu_set_t cpu;
    while (cpu_number >= Params.cpus)
    {
        cpu_number -= Params.cpus;
    }
    CPU_ZERO(&cpu);
    CPU_SET(cpu_number, &cpu);
    result = sched_setaffinity(0, sizeof(cpu_set_t), &cpu);
    if (result != 0) 
    {
        printf("Affinity result %d %d %d\n", cpu_number, result, errno);
    }
}
#endif

static void *rcu_thread(void *arg)
{
    thread_data_t *thread_data = (thread_data_t *)arg;

    set_affinity(thread_data->thread_index);
    lock_thread_init(thread_data->lock, thread_data->thread_index);
    lock_mb();

	//while (goflag == GOFLAG_INIT)
	//	poll(NULL, 0, 10);

    while (goflag != GOFLAG_STOP) 
    {
        rcu_poll(thread_data->lock);
        //if (!rcu_poll(thread_data->lock)) poll(NULL, 0, 10);
    }

    poll(NULL, 0, 10);
    while (rcu_poll(thread_data->lock)) 
    {
        poll(NULL, 0, 10);
    }

    return get_thread_stats(0, 0, 0, 0, 0, 0);
}
void *perftest_thread(void *arg)
{
    thread_data_t *thread_data = (thread_data_t *)arg;
    //int update_percent = thread_data->update_percent;
    int thread_index = thread_data->thread_index;
    unsigned long random_seed = init_random_seed();
    //unsigned long random_seed = random();
    void *value;
    unsigned long int_value;
    long key=0;

    unsigned long long n_reads = 0;
    unsigned long long n_read_fails = 0;
    unsigned long long n_inserts = 0;
    unsigned long long n_insert_fails = 0;
    unsigned long long n_deletes = 0;
    unsigned long long n_delete_fails = 0;

    op_ring_buff_t ring;
    ring.head = 0;
    ring.count = 0;

    set_affinity(thread_index);
    lock_thread_init(thread_data->lock, thread_index);
    lock_mb();

	while (goflag == GOFLAG_INIT)
		poll(NULL, 0, 10);

    switch (thread_data->mode)
    {
        case MODE_TRAVERSEN:
            while (goflag == GOFLAG_RUN) 
            {
                long new_key=0;

                value = avl_first(&My_Tree, &new_key);
                assert(new_key == -1);

                while (value != NULL)
                {
                    key = new_key;
                    value = avl_next(&My_Tree, key, &new_key);
                    if (value != NULL && key >= new_key)
                    {
                        write_lock(My_Tree.lock);
                        printf("******************************************\n"
                               "%s\nkey: %ld new: %ld\n"
                               "******************************************\n", 
                               (char *)value, key, new_key);
                        //avl_output(&My_Tree);
                        goflag = GOFLAG_STOP;
                        write_unlock(My_Tree.lock);
                        return get_thread_stats(
                                n_reads, n_read_fails,
                                n_inserts, n_insert_fails, 
                                n_deletes, n_delete_fails);
                    }
                }
                assert(key == Params.scale + 1);
                n_reads++;
            }
            break;
        case MODE_TRAVERSE:
            while (goflag == GOFLAG_RUN) 
            {
                avl_node_t *new_node, *node;

                rw_lock(My_Tree.lock);
                new_node = avl_first_n(&My_Tree);
                assert(new_node->key == -1);

                while (new_node != NULL)
                {
                    node = new_node;
                    key = node->key;

                    new_node = avl_next_n(node);
                    if (new_node != NULL && node->key >= new_node->key)
                    {
                        printf("******************************************\n"
                               "%s\nkey: %ld new: %ld\n"
                               "******************************************\n", 
                               (char *)node->value, node->key, new_node->key);
                        //avl_output(&My_Tree);
                        goflag = GOFLAG_STOP;
                        rw_unlock(My_Tree.lock);
                        return get_thread_stats(
                                n_reads, n_read_fails,
                                n_inserts, n_insert_fails, 
                                n_deletes, n_delete_fails);
                    }
                }

                rw_unlock(My_Tree.lock);

                assert(key == Params.scale + 1);
                n_reads++;
            }
            break;
        case MODE_READONLY:
            while (goflag == GOFLAG_RUN) 
            {
                int_value = get_random(&random_seed) % Params.scale;
                value = avl_find(&My_Tree, int_value);
                n_reads++;
            }
            break;
        case MODE_WRITE:
            while (goflag == GOFLAG_RUN)
            {
                int_value = get_random(&random_seed) % Params.size;
                /*
                while (avl_remove(&My_Tree, int_value) == NULL)
                {
                    n_delete_fails++;
                    int_value = get_random(&random_seed) % Params.size;
                }
                n_deletes++;
                */
                if (avl_remove(&My_Tree, int_value) == NULL)
                    n_delete_fails++;
                else
                    n_deletes++;

                int_value = get_random(&random_seed) % Params.scale + 1;
                if ( avl_insert(&My_Tree, int_value, (void *)int_value) )
                    n_inserts++;
                else
                    n_insert_fails++;
            }
            break;
    }

    lock_thread_close(thread_data->lock, thread_index);

    return get_thread_stats(
            n_reads, n_read_fails,
            n_inserts, n_insert_fails, 
            n_deletes, n_delete_fails);
}

/*
 * Mainprogram.
 */

void usage(int argc, char *argv[], char *bad_arg)
{
    if (bad_arg != NULL) fprintf(stderr, "Invalid param %s\n", bad_arg);

	fprintf(stderr, 
       "Usage: %s [c:<CPUS>] [m:<READ | TRAVERSE | TRAVERSEN>] [s:<tree size>] [S:<tree scale>] [w:<writers>] [r:<readers>] [p]\n",

       argv[0]);
	exit(-1);
}

void parse_args(int argc, char *argv[])
{
    int ii;
    char *param, *value;
    char arg[1000];

    for (ii=1; ii<argc; ii++)
    {
        strcpy(arg, argv[ii]);
        param = strtok(arg, ":");
        if (param == NULL || strlen(param) != 1) usage(argc, argv, argv[ii]);

        value = strtok(NULL, ":");
        if (value == NULL) usage(argc, argv, argv[ii]);

        switch (*param)
        {
            case 'c':
                Params.cpus = atoi(value);
                if (Params.cpus < 1) usage(argc, argv, argv[ii]);
                break;
            case 'd':
                Params.delay = atoi(value);
                if (Params.delay < 1) usage(argc, argv, argv[ii]);
                break;
            case 'm':
                if (strcmp(value, "READ")==0)
                    Params.mode = MODE_READONLY;
                else if (strcmp(value, "TRAVERSE") == 0)
                    Params.mode = MODE_TRAVERSE;
                else if (strcmp(value, "TRAVERSEN") == 0)
                    Params.mode = MODE_TRAVERSEN;
                else
                    usage(argc, argv, argv[ii]);
                break;
            case 'p':
                Params.poll_rcu = 1;
                break;
            case 'r':
                Params.readers = atoi(value);
                if (Params.readers < 0) usage(argc, argv, argv[ii]);
                break;

            case 's':
                Params.size = atoi(value);
                if (Params.size < 1) usage(argc, argv, argv[ii]);

                if (Params.scale < Params.size*2) Params.scale = Params.size*2;
                break;
            case 'S':
                Params.scale = atoi(value);
                if (Params.scale < 10) usage(argc, argv, argv[ii]);
                break;
            case 'w':
                Params.writers = atoi(value);
                if (Params.writers < 0) usage(argc, argv, argv[ii]);
                break;
            default:
                usage(argc, argv, argv[ii]);
                break;
        }
    }
}

int main(int argc, char *argv[])
{
    void *vstats;
    unsigned long long *stats;
	int ii, jj;
    unsigned long long tot_stats[MAX_STATS];
    thread_data_t thread_data[MAX_THREADS];
    void *lock;

    parse_args(argc, argv);

    for (ii=0; ii<MAX_STATS; ii++)
    {
        tot_stats[ii] = 0;
    }

    fprintf(stderr, "%s_%d_%d Test: r:%d w:%d p:%d mode %d %d\n", 
            implementation_name(), Params.size, Params.mode, 
            Params.readers, Params.writers, Params.poll_rcu, Params.mode, Params.scale);
    printf("%s_%d_%d Test: r:%d w:%d p:%d mode %d %d\n", 
            implementation_name(), Params.size, Params.mode, 
            Params.readers, Params.writers, Params.poll_rcu, Params.mode, Params.scale);

    lock = lock_init();
    lock_thread_init(lock, 0);
    init_tree_data(Params.size, lock);

    if (Params.mode == MODE_TRAVERSE || Params.mode == MODE_TRAVERSEN)
    {
        unsigned long value;
        avl_insert(&My_Tree, -1, &value);
        avl_insert(&My_Tree, Params.scale + 1, &value);
    }

    printf("pre tree size: %d\n", avl_size(&My_Tree));

    for (ii=0; ii<MAX_THREADS; ii++)
    {
        thread_data[ii].thread_index = ii;

        if (ii < Params.writers)
            thread_data[ii].mode = MODE_WRITE;
        else
            thread_data[ii].mode = Params.mode;

        thread_data[ii].lock = lock;
    }

	for (ii = 0; ii < Params.readers+Params.writers; ii++)
    {
		pthread_create(&thread_data[ii].thread_id, NULL,
                perftest_thread, &thread_data[ii]);
		//pthread_create(&thread_data[ii].thread_id, NULL,
        //        locktest_thread, &thread_data[ii]);
    }

    if (Params.poll_rcu)
    {
        pthread_create(&thread_data[ii].thread_id, NULL,
                rcu_thread, &thread_data[ii]);
        ii++;
    }

	lock_mb();
    //if (sched_getparam(0, &sched_params) != 0)
    //{
        //printf("Unable to get scheduling params\n");
    //}

    //sched_params.sched_priority = 1;
    //if (sched_setscheduler(0, SCHED_RR, &sched_params) != 0)
    //{
        //printf("Realtime scheduling not active\n");
    //}
    //avl_output(&My_Tree);

	sleep(1);
    printf("init done\n");
	sleep(1);
	goflag = GOFLAG_RUN;
	lock_mb();
	sleep(Params.delay);
	lock_mb();
	goflag = GOFLAG_STOP;
	lock_mb();

	for (ii = 0; ii < Params.readers+Params.writers; ii++)
    {
        pthread_join(thread_data[ii].thread_id, &vstats);
        stats = (unsigned long long *)vstats;
        printf("Thr %2d ", ii);

        for (jj=1; jj<stats[0]+1; jj++)
        {
            printf(" %7lld", stats[jj]);
            tot_stats[jj] += stats[jj];
        }
        printf("\n");
    }

    printf("post tree size: %d\n", avl_size(&My_Tree));

	printf("n_reads: ");
    for (jj=1; jj<9; jj++)
    {
        printf(" %7lld", tot_stats[jj]);
    }
    printf("\n\n");

    if (!avl_valid(&My_Tree)) avl_output_list(&My_Tree);

    return 0;
}
