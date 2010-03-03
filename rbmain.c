#define _GNU_SOURCE
#include <sched.h>
#include <errno.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <poll.h>
#include <pthread.h>

#include "lock.h"
#include "rbtree.h"

/*
 * Test variables.
 */

#define MAX_STATS       20
#define MAX_THREADS     128
#define NUM_UPDATE_OPS  3

unsigned long *Values;
int Tree_Size   = 64;
int Tree_Scale  = 10000;

typedef struct
{
    __attribute__((__aligned__(CACHE_LINE_SIZE))) long long count;
} thread_counter_t;

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
rbtree_t My_Tree;
void *My_Lock;

unsigned long get_random(unsigned long *seed)
{
    unsigned int val = *seed;
    val = (val*1103515245+12345)>>5;
    *seed = val;
    return val;
}
void waste_time()
{
    static unsigned long seed;
    void *foo;
    foo = malloc((get_random(&seed) % 5000) + 5);
    memset(foo, 1, 5);
    free(foo);
}
void init_tree_data(int count)
{
    int ii;
    unsigned long seed = 0;
    unsigned long value;

    Values = (unsigned long *)malloc(count*sizeof(unsigned long));
    rb_create(&My_Tree, My_Lock);

    for (ii=0; ii<count; ii++)
    {
        value = get_random(&seed) % Tree_Scale + 1;
        rb_insert(&My_Tree, value, &value);
        Values[ii] = value;
    }
}

void set_affinity(int cpu_number)
{
    int result;
    cpu_set_t cpu;
    CPU_ZERO(&cpu);
    CPU_SET(cpu_number, &cpu);
    result = sched_setaffinity(0, sizeof(cpu_set_t), &cpu);
    printf("Affinity result %d %d\n", result, errno);
}

void *perftest_thread(void *arg)
{
    thread_data_t *thread_data = (thread_data_t *)arg;
    //int update_percent = thread_data->update_percent;
    int thread_index = thread_data->thread_index;
    int write_elem = thread_data->write_elem;
    int read_elem;
    unsigned long random_seed = 1234;
    //unsigned long random_seed = random();
    void *value;
    unsigned long int_value;

    unsigned long long n_reads = 0;
    unsigned long long n_inserts = 0;
    unsigned long long n_insert_fails = 0;
    unsigned long long n_deletes = 0;
    unsigned long long n_delete_fails = 0;

    //set_affinity(0);
    lock_thread_init(My_Lock, thread_index);
    lock_mb();

	while (goflag == GOFLAG_INIT)
		poll(NULL, 0, 10);

    switch (thread_data->mode)
    {
        case MODE_READONLY:
            while (goflag == GOFLAG_RUN) 
            {
                read_elem = get_random(&random_seed) % Tree_Scale;
                value = rb_find(&My_Tree, read_elem);
                n_reads++;
            }
            break;
        case MODE_WRITE:
            while (goflag == GOFLAG_RUN)
            {
                write_elem = get_random(&random_seed) % Tree_Size;
                value = rb_remove(&My_Tree, Values[write_elem]);
                if (value != NULL)
                    n_deletes++;
                else
                    n_delete_fails++;

                int_value = get_random(&random_seed) % Tree_Scale + 1;
                Values[write_elem] = int_value;
                rb_insert(&My_Tree, int_value, &int_value);
                n_inserts++;

            }
            break;
    }

    return get_thread_stats(n_reads, n_inserts, n_insert_fails, 
            n_deletes, n_delete_fails);
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

    void *vstats;
    unsigned long long *stats;
	int ii, jj;
	int nthreads = 0;
    unsigned long long tot_stats[MAX_STATS];
    thread_data_t thread_data[MAX_THREADS];
    int delay = 1;
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

    for (ii=0; ii<MAX_STATS; ii++)
    {
        tot_stats[ii] = 0;
    }

    printf("%s_%d Test: threads %d mode %d\n", 
            implementation_name(), Tree_Size, nthreads, mode);

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

    My_Lock = lock_init();
    lock_thread_init(My_Lock, 0);
    init_tree_data(Tree_Size);

	for (ii = 0; ii < nthreads; ii++)
    {
		pthread_create(&thread_data[ii].thread_id, NULL,
                perftest_thread, &thread_data[ii]);
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
	sleep(delay);
	goflag = GOFLAG_RUN;
	lock_mb();
	sleep(delay);
	lock_mb();
	goflag = GOFLAG_STOP;
	lock_mb();

	for (ii=0; ii<nthreads; ii++)
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
	printf("n_reads: ");
    for (jj=1; jj<9; jj++)
    {
        printf(" %7lld", tot_stats[jj]);
    }
    printf("\n\n");

    return 0;
}
