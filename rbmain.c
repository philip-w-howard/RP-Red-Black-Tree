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

#include "tests.h"
#include "rbmain.h"
#ifdef STM
#include "my_stm.h"
#endif

#define GOFLAG_INIT 0
#define GOFLAG_RUN  1
#define GOFLAG_STOP 2

volatile int goflag = GOFLAG_INIT;

param_t Params = {64, 10000, 1, MODE_READ, NUM_CPUS, 0, 0, 0, 0, 0, 0};

unsigned long init_random_seed()
{
    unsigned long seed;
    struct timespec cur_time;

    clock_gettime(CLOCK_REALTIME, &cur_time);
    seed = cur_time.tv_sec + cur_time.tv_nsec;

//seed = 12348234767;

    return seed;
}

unsigned long get_random(unsigned long *seed)
{
    unsigned int val = *seed;
    val = val*214013+2531011;
    *seed = val;
    return val>>5 & 0x7FFFFFFF;
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

unsigned long get_random1(unsigned long *seed)
{
    unsigned int val = *seed;
    val = (val*1103515245+12345)>>5;
    *seed = val;
    return val;
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
        printf("Affinity result %d %d %d %d\n", result, errno, cpu_number, core);
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

void *thread_func(void *arg)
{
    thread_data_t *thread_data = (thread_data_t *)arg;
    int insert_percent = thread_data->insert_percent;
    int delete_percent = thread_data->delete_percent;
    int thread_index = thread_data->thread_index;
    unsigned long random_seed = init_random_seed();

    unsigned long int_value;

    unsigned long long n_reads = 0;
    unsigned long long n_read_fails = 0;
    unsigned long long n_writes = 0;
    unsigned long long n_write_fails = 0;

    set_affinity(thread_index);
    lock_thread_init(thread_data->lock, thread_index);
    lock_mb();

	while (goflag == GOFLAG_INIT)
		poll(NULL, 0, 10);

    switch (thread_data->mode)
    {
        case MODE_NOOP:
            while (goflag == GOFLAG_RUN) 
            {
                poll(NULL, 0, 10);
            }
            break;
        case MODE_READ:
            while (goflag == GOFLAG_RUN) 
            {
                if (Read(&random_seed, &Params))
                    n_read_fails++;
                else
                    n_reads++;
            }
            break;
        case MODE_WRITE:
            while (goflag == GOFLAG_RUN) 
            {
                if (Write(&random_seed, &Params))
                    n_write_fails++;
                else
                    n_writes++;
            }
            break;
        case MODE_RANDOM:
            while (goflag == GOFLAG_RUN) 
            {
                int_value = get_random(&random_seed) % UPDATE_MAX;
                if (int_value < delete_percent)
                {
                    if (Delete(&random_seed, &Params))
                        n_writes++;
                    else
                        n_write_fails++;
                }
                else if (int_value < insert_percent+delete_percent)
                {
                    if (Insert(&random_seed, &Params))
                        n_write_fails++;
                    else
                        n_writes++;
                }
                else 
                {
                    if (RRead(&random_seed, &Params))
                        n_read_fails++;
                    else
                        n_reads++;
                }
            }
            break;
    }

    lock_thread_close(thread_data->lock, thread_index);

    thread_data->done = 1;

    return get_thread_stats(n_reads, n_read_fails, n_writes, n_write_fails, 0, 0); 
}

/*
 * Mainprogram.
 */

void usage(int argc, char *argv[], char *bad_arg)
{
    if (bad_arg != NULL) fprintf(stderr, "Invalid param %s\n", bad_arg);

	fprintf(stderr, 
       "Usage: %s [c:<CPUS>] [d:<delete %%>] [i:<insert %%] [m:<READ | WRITE | RAND>] [R:<run time>] [s:<tree size>] [S:<tree scale>] [w:<writers>] [r:<readers>] [u:<update %%>]\n",

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
                Params.delete_percent = atoi(value);
                if (Params.delete_percent < 0) usage(argc, argv, argv[ii]);
                break;
            case 'i':
                Params.insert_percent = atoi(value);
                if (Params.insert_percent < 0) usage(argc, argv, argv[ii]);
                break;
            case 'm':
                if (strcmp(value, "READ")==0)
                    Params.mode = MODE_READ;
                else if (strcmp(value, "WRITE") == 0)
                    Params.mode = MODE_WRITE;
                else if (strcmp(value, "NOOP") == 0)
                    Params.mode = MODE_NOOP;
                else if (strcmp(value, "RAND") == 0)
                    Params.mode = MODE_RANDOM;
                else
                    usage(argc, argv, argv[ii]);
                break;
            case 'p':
                Params.poll_rcu = atoi(value);
                if (Params.poll_rcu < 0) usage(argc, argv, argv[ii]);
                break;
            case 'R':
                Params.runtime = atoi(value);
                if (Params.runtime < 1) usage(argc, argv, argv[ii]);
                break;
            case 'r':
                Params.readers = atoi(value);
                if (Params.readers < 0) usage(argc, argv, argv[ii]);
                break;

            case 's':
                Params.size = atoi(value);
                if (Params.size < 1) usage(argc, argv, argv[ii]);

                if (Params.scale < Params.size*100) Params.scale = Params.size*100;
                break;
            case 'S':
                Params.scale = atoi(value);
                if (Params.scale < 10) usage(argc, argv, argv[ii]);
                break;
            case 'u':
                Params.update_percent = atoi(value);
                if (Params.update_percent < 0) usage(argc, argv, argv[ii]);
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

void thread_stuck(int id)
{
    fprintf(stderr, "Thread %d failed to terminate\n", id);
    //sleep(100000);
    //exit(-4);
}
int main(int argc, char *argv[])
{
    void *vstats;
    unsigned long long *stats;
	int ii, jj;
    unsigned long long tot_stats[MAX_STATS];
    thread_data_t thread_data[MAX_THREADS];
    void *lock;
    time_t stop_time;
    void *my_data;

    parse_args(argc, argv);

    for (ii=0; ii<MAX_STATS; ii++)
    {
        tot_stats[ii] = 0;
    }

    printf("Test: %s %d readers %d writers %d mode %d %d %d\n", 
            argv[0], Params.size,
            Params.readers, Params.writers, Params.mode, Params.scale, 
            NUM_CPUS);

    lock = lock_init();
    lock_thread_init(lock, 0);
    my_data = Init_Data(Params.size, lock, &Params);

    printf("pre tree size: %d\n", Size(my_data));

    for (ii=0; ii<MAX_THREADS; ii++)
    {
        thread_data[ii].thread_index = ii;
        thread_data[ii].update_percent = Params.update_percent;
        thread_data[ii].delete_percent = Params.delete_percent;
        thread_data[ii].insert_percent = Params.insert_percent;

        if (ii < Params.writers)
        {
            if (Params.mode == MODE_READ)
                thread_data[ii].mode = MODE_WRITE;
            else
                thread_data[ii].mode = Params.mode;
        }
        else
            thread_data[ii].mode = Params.mode;

        thread_data[ii].lock = lock;
        thread_data[ii].done = 0;
    }

	for (ii = 0; ii < Params.readers+Params.writers; ii++)
    {
		pthread_create(&thread_data[ii].thread_id, NULL,
                thread_func, &thread_data[ii]);
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
    //rb_output(&my_data);

	sleep(1);
    printf("init done\n");
	sleep(1);
	goflag = GOFLAG_RUN;
	lock_mb();
	sleep(Params.runtime);
	lock_mb();
	goflag = GOFLAG_STOP;
	lock_mb();
    stop_time = time(NULL);

	for (ii=0; ii<Params.readers+Params.writers; ii++)
    {
        while (!thread_data[ii].done)
        {
            if (time(NULL) > stop_time+2)
            {
                thread_stuck(ii);
                break;
            }
        }
        if (thread_data[ii].done)
        {
            pthread_join(thread_data[ii].thread_id, &vstats);
            stats = (unsigned long long *)vstats;
            printf("Thr %2d ", ii);

            for (jj=1; jj<stats[0]+1; jj++)
            {
                printf(" %'7lld", stats[jj]);
                tot_stats[jj] += stats[jj];
            }
            printf("\n");
        }
    }

    printf("post tree size: %d\n", Size(my_data));

	printf("n_reads: ");
    for (jj=1; jj<9; jj++)
    {
        printf(" %'7lld", tot_stats[jj]);
    }
    printf("\n\n");

    Output_Stats(my_data);

#ifdef STM
//	wlpdstm_print_stats();
#endif
    return 0;
}
