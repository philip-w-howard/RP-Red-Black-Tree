#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <poll.h>
#include <pthread.h>

#include "rcu.h"
#include "lock.h"
//#include "rcutestdata.h"

/*
 * Test variables.
 */
#define MAX_THREADS     128

#define RCU_STRESS_PIPE_LEN 5

struct rcu_stress {
	int pipe_count;
	int mbtest;
};

typedef struct
{
    __attribute__((__aligned__(CACHE_LINE_SIZE))) long long
        count[RCU_STRESS_PIPE_LEN+1];
} stress_count_t;

typedef struct
{
    __attribute__((__aligned__(CACHE_LINE_SIZE))) long long count;
} thread_counter_t;

thread_counter_t n_reads[MAX_THREADS];
thread_counter_t n_updates[MAX_THREADS];
stress_count_t   stress_count[MAX_THREADS];

struct rcu_stress rcu_stress_array[RCU_STRESS_PIPE_LEN];
struct rcu_stress *rcu_stress_current;
int rcu_stress_idx = 0;

long long n_mberror = 0;
int garbage = 0;

typedef struct
{
    void *lock;
    pthread_t thread_id;
    int thread_index;
} thread_data_t;

#define GOFLAG_INIT 0
#define GOFLAG_RUN  1
#define GOFLAG_STOP 2

volatile int goflag = GOFLAG_INIT;

unsigned int get_random(unsigned int *seed)
{
    unsigned int val = *seed;
    val = (val*1103515245+12345)>>5;
    *seed = val;
    return val;
}
void waste_time()
{
    static unsigned int seed;
    int ii;
    int foo[100];
    unsigned int index;
    unsigned int count = get_random(&seed) % 2000;
    for (ii=0; ii<count; ii++)
    {
        index = get_random(&seed) % 100;
        foo[index] += 3;
    }
}
/*
 * Stress test.
 */

void *rcu_test_read(void *arg)
{
    static unsigned int seed;
	int i;
	int itercnt = 0;
	struct rcu_stress *p;
	int pc;
    thread_data_t *thread_data = (thread_data_t *)arg;
    int thread_index;

    thread_index = thread_data->thread_index;

    lock_thread_init(thread_data->lock, thread_index);

	while (goflag == GOFLAG_INIT)
		poll(NULL, 0, 10);
	while (goflag == GOFLAG_RUN) {
		read_lock(thread_data->lock);
		p = rcu_dereference(rcu_stress_current);
		if (p->mbtest == 0)
			n_mberror++;
        //
        // should we randomize this time wasting?
		for (i = 0; i < 100; i++)
        {
            get_random(&seed);
        }

		pc = p->pipe_count;
		read_unlock(thread_data->lock);

		if ((pc > RCU_STRESS_PIPE_LEN) || (pc < 0))
			pc = RCU_STRESS_PIPE_LEN;
		stress_count[thread_index].count[pc]++;
		n_reads[thread_index].count++;

		if ((++itercnt % 0x1000) == 0) {
//            waste_time();
		}
	}

    lock_thread_close(thread_data->lock, thread_index);

	return (NULL);
}

void *rcu_test_update(void *arg)
{
	int i;
	struct rcu_stress *p;
    thread_data_t *thread_data = (thread_data_t *)arg;
    int thread_index;

    thread_index = thread_data->thread_index;

    lock_thread_init(thread_data->lock, thread_index);

	while (goflag == GOFLAG_INIT)
		poll(NULL, 0, 10);
	while (goflag == GOFLAG_RUN) {
		i = rcu_stress_idx + 1;
		if (i >= RCU_STRESS_PIPE_LEN)
			i = 0;
		p = &rcu_stress_array[i];
        write_lock(thread_data->lock);
		p->mbtest = 0;
		lock_mb();
		p->pipe_count = 0;
		p->mbtest = 1;
		rcu_assign_pointer(rcu_stress_current, p);
        write_unlock(thread_data->lock);
		rcu_synchronize(thread_data->lock);
		rcu_stress_idx = i;
		for (i = 0; i < RCU_STRESS_PIPE_LEN; i++)
			if (i != rcu_stress_idx)
				rcu_stress_array[i].pipe_count++;
		n_updates[thread_index].count++;
	}

    lock_thread_close(thread_data->lock, thread_index);

    return NULL;
}

/*
 * Mainprogram.
 */

void usage(int argc, char *argv[])
{
	fprintf(stderr, "Usage: %s [nthreads [ %% time interval ] ]\n", argv[0]);
	exit(-1);
}

int main(int argc, char *argv[])
{
	int ii, jj;
    long long sum = 0;
	int nthreads = 1;
    long long tot_reads = 0;
    long long tot_updates = 0;
    thread_data_t thread_data[MAX_THREADS];
    int delay = 1;
    void *lock;

	if (argc > 1) nthreads = atoi(argv[1]);
	if (argc > 2) delay = atoi(argv[2]);

    printf("%s Test: %d threads %d interval\n", 
            implementation_name(), nthreads, delay);
    
    for (ii=0; ii<RCU_STRESS_PIPE_LEN; ii++)
    {
        rcu_stress_array[ii].pipe_count = 0;
        rcu_stress_array[ii].mbtest = 0;
    }

    lock = lock_init();

    for (ii=0; ii<MAX_THREADS; ii++)
    {
        n_reads[ii].count = 0;
        n_updates[ii].count = 0;
        thread_data[ii].thread_index = ii;
        thread_data[ii].lock = lock;
        for (jj=0; jj<RCU_STRESS_PIPE_LEN+1; jj++)
        {
		    stress_count[ii].count[jj] = 0;
        }
    }

	rcu_stress_current = &rcu_stress_array[0];
	rcu_stress_current->pipe_count = 0;
	rcu_stress_current->mbtest = 1;
    lock_mb();
	for (ii = 0; ii < nthreads-1; ii++)
    {
		pthread_create(&thread_data[ii].thread_id, NULL,
                rcu_test_read, &thread_data[ii]);
    }
    pthread_create(&thread_data[ii].thread_id, NULL,
            rcu_test_update, &thread_data[ii]);

	lock_mb();
	sleep(1);
	goflag = GOFLAG_RUN;
	lock_mb();
	sleep(delay);
	lock_mb();
	goflag = GOFLAG_STOP;
	lock_mb();

	for (ii=0; ii<nthreads; ii++)
    {
        pthread_join(thread_data[ii].thread_id, NULL);

		tot_reads   += n_reads[ii].count;
		tot_updates += n_updates[ii].count;
        printf("Thread %2d reads: %9lld writes: %9lld stress: ", 
                ii, n_reads[ii].count, n_updates[ii].count);
        for (jj=0; jj<RCU_STRESS_PIPE_LEN+1; jj++)
        {
            printf(" %lld", stress_count[ii].count[jj]);
            sum += stress_count[ii].count[jj];
        }
        printf("\n");
    }
	printf("n_reads: %lld  n_updates: %lld mb_err: %lld\n\n", 
            tot_reads, tot_updates, n_mberror);

    return 0;
}
