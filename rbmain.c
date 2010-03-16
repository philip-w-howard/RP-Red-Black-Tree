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

#ifndef NUM_CPUS
#define NUM_CPUS 16
#endif

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
#define MODE_TRAVERSE       2
#define MODE_TRAVERSEW      3

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
rbtree_t My_Tree;
//void *My_Lock;

#define RING_SIZE 50

typedef struct
{
    char op[RING_SIZE];
    long value[RING_SIZE];
    int head;
    int count;
} op_ring_buff_t;
static void ring_insert(op_ring_buff_t *ring, char op, long value)
{
    ring->op[ring->head] = op;
    ring->value[ring->head] = value;
    ring->head++;
    if (ring->head >= RING_SIZE) ring->head = 0;
    ring->count++;
}
static void ring_output(op_ring_buff_t *ring)
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
void init_tree_data(int count, void *lock)
{
    int ii;
    unsigned long seed = 0;
    unsigned long value;

    Values = (unsigned long *)malloc(count*sizeof(unsigned long));
    rb_create(&My_Tree, lock);

    for (ii=0; ii<count; ii++)
    {
        value = get_random(&seed) % Tree_Scale + 1;
        while ( !rb_insert(&My_Tree, value, (void *)value) )
        {
            value = get_random(&seed) % Tree_Scale + 1;
        }
        Values[ii] = value;
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
    //int update_percent = thread_data->update_percent;
    int thread_index = thread_data->thread_index;
    int write_elem = thread_data->write_elem;
    int read_elem;
    unsigned long random_seed = 1234;
    //unsigned long random_seed = random();
    void *value;
    unsigned long int_value;
    long key=0;
    long new_key=0;

    unsigned long long n_reads = 0;
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
        case MODE_TRAVERSE:
            while (goflag == GOFLAG_RUN) 
            {
                value = rb_first(&My_Tree, &new_key);
                assert(new_key == -1);

                while (value != NULL)
                {
                    key = new_key;
                    value = rb_next(&My_Tree, key, &new_key);
                    if (value != NULL && key >= new_key)
                    {
                        write_lock(My_Tree.lock);
                        printf("******************************************\n"
                               "%s\nkey: %ld new: %ld\n"
                               "******************************************\n", 
                               (char *)value, key, new_key);
                        //rb_output(&My_Tree);
                        goflag = GOFLAG_STOP;
                        write_unlock(My_Tree.lock);
                        return get_thread_stats(n_reads, n_inserts, n_insert_fails, 
                            n_deletes, n_delete_fails);
                    }
                }

                assert(key == Tree_Scale + 1);
                n_reads++;
            }
            break;
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
                ring_insert(&ring, 'D', Values[write_elem]);
                if (value != NULL)
                    n_deletes++;
                else
                    n_delete_fails++;

                /*
                if (!rb_valid(&My_Tree)) 
                {
                    rb_output_list(&My_Tree);
                    fprintf(stderr, "Invalid tree\n");
                    exit(-1);
                }
                */

                int_value = get_random(&random_seed) % Tree_Scale + 1;
                while ( !rb_insert(&My_Tree, int_value, (void *)int_value) )
                {
                    int_value = get_random(&random_seed) % Tree_Scale + 1;
                }
                Values[write_elem] = int_value;
                //ring_insert(&ring, 'I', Values[write_elem]);
                n_inserts++;
                /*
                if (!rb_valid(&My_Tree)) 
                {
                    rb_output_list(&My_Tree);
                    fprintf(stderr, "Invalid tree\n");
                    exit(-1);
                }
                */
            }
            //ring_output(&ring);
            break;
    }

    lock_thread_close(thread_data->lock, thread_index);

    return get_thread_stats(n_reads, n_inserts, n_insert_fails, 
            n_deletes, n_delete_fails);
}

/*
 * Mainprogram.
 */

void usage(int argc, char *argv[])
{
	fprintf(stderr, 
       "Usage: %s nthreads [[[READ | WRITE | TRAVERSE | TRAVERSEW ] init] max_elem]\n",

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
    int work_delay = 1;
    int mode = MODE_WRITE;
    void *lock;

	if (argc > 1) nthreads = atoi(argv[1]);
    if (argc > 2)
    {
        if (strcmp(argv[2], "READ")==0)
            mode = MODE_READONLY;
        else if (strcmp(argv[2], "WRITE") == 0)
            mode = MODE_WRITE;
        else if (strcmp(argv[2], "TRAVERSE") == 0)
            mode = MODE_TRAVERSE;
        else if (strcmp(argv[2], "TRAVERSEW") == 0)
            mode = MODE_TRAVERSEW;
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

    lock = lock_init();
    lock_thread_init(lock, 0);
    init_tree_data(Tree_Size, lock);

    if (mode == MODE_TRAVERSE || mode == MODE_TRAVERSEW)
    {
        unsigned long value;
        rb_insert(&My_Tree, -1, &value);
        rb_insert(&My_Tree, Tree_Scale + 1, &value);
    }

    printf("%s_%d Test: threads %d mode %d\n", 
            implementation_name(), Tree_Size, nthreads, mode);

    for (ii=0; ii<MAX_THREADS; ii++)
    {
        thread_data[ii].thread_index = ii;
        //thread_data[ii].update_percent = update_percent;
        if (mode == MODE_WRITE && ii>0)
            thread_data[ii].mode = MODE_READONLY;
        else if (mode == MODE_TRAVERSEW && ii==0)
        {
            thread_data[ii].mode = MODE_WRITE;
            mode = MODE_TRAVERSE;
        }
        else
            thread_data[ii].mode = mode;

        thread_data[ii].lock = lock;
    }

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
    //rb_output(&My_Tree);

	sleep(delay);
	goflag = GOFLAG_RUN;
	lock_mb();
	sleep(work_delay);
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

    if (!rb_valid(&My_Tree)) rb_output_list(&My_Tree);

    return 0;
}
