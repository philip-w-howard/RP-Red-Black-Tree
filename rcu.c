//#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <atomic_ops.h>
#include <memory.h>
#include <pthread.h>

#include "lock.h"
#include "rbnode.h"

char *implementation_name()
{
    return "RCU";
}

#define NSTATS      11
#define STAT_READ   6
#define STAT_WRITE  7
#define STAT_SPINS  8
#define STAT_SYNC   9
#define STAT_MIN_E  10
#define STAT_FREE   11

static __thread __attribute__((__aligned__(CACHE_LINE_SIZE)))
    unsigned long long Thread_Stats[NSTATS+1];

#define RCU_MAX_BLOCKS      20

typedef struct epoch_s
{
    __attribute__((__aligned__(CACHE_LINE_SIZE))) long long epoch;
    volatile __attribute__((__aligned__(CACHE_LINE_SIZE))) 
        struct epoch_s *next;
} epoch_list_t;

typedef struct 
{
    void *block[RCU_MAX_BLOCKS];
    int head;
} block_list_t;

typedef struct
{
    volatile __attribute__((__aligned__(CACHE_LINE_SIZE))) 
        epoch_list_t *epoch_list;
    volatile __attribute__((__aligned__(CACHE_LINE_SIZE))) 
        long long rcu_epoch;
    volatile __attribute__((__aligned__(CACHE_LINE_SIZE))) 
        block_list_t block;
    __attribute__((__aligned__(CACHE_LINE_SIZE))) 
        pthread_mutex_t rcu_writer_lock;
} rcu_lock_t;

static __thread __attribute__((__aligned__(CACHE_LINE_SIZE))) 
        epoch_list_t My_Thread_Epoch;
static __thread __attribute__((__aligned__(CACHE_LINE_SIZE))) 
        epoch_list_t *Thread_Epoch;
rcu_lock_t My_Lock;
//**********************************************
unsigned long long *get_thread_stats(unsigned long long a, unsigned long long b,
        unsigned long long c, unsigned long long d, unsigned long long e)
{
    Thread_Stats[1] = a;
    Thread_Stats[2] = b;
    Thread_Stats[3] = c;
    Thread_Stats[4] = d;
    Thread_Stats[5] = e;

    return Thread_Stats;
}

void read_lock(void *lock)
{
    rcu_lock_t *rcu_lock = (rcu_lock_t *)lock;

    Thread_Stats[STAT_READ]++;
    Thread_Epoch->epoch = rcu_lock->rcu_epoch + 1;

    // the following statement, though useless, is required if we want
    // to avoid a memory barrier. 
    // Note that the if statement should never return true, so the mb
    // will never execute. Accessing Thread_Epoch is what gives us the
    // ordering guarantees we need (on an X86, anyway)
#if defined(__i386__) || defined(__x86_64__)
    if ( (Thread_Epoch->epoch & 0x0001) == 0) lock_mb();
#else
#error("RCU need a memory barrier on this architecture");
    //lock_mb();
#endif

}

void read_unlock(void *lock)
{
    rcu_lock_t *rcu_lock = (rcu_lock_t *)lock;

    Thread_Epoch->epoch = rcu_lock->rcu_epoch;
}

void write_lock(void *lock)
{
    rcu_lock_t *rcu_lock = (rcu_lock_t *)lock;

    Thread_Stats[STAT_WRITE]++;
    pthread_mutex_lock(&rcu_lock->rcu_writer_lock);
}

void write_unlock(void *lock)
{
    rcu_lock_t *rcu_lock = (rcu_lock_t *)lock;

    pthread_mutex_unlock(&rcu_lock->rcu_writer_lock);
}

void *lock_init()
{
    rcu_lock_t *lock;
    //*********************************************
    // the following line, if uncommented, will make RCU run much slower
    // at higher thread counts. This is true even thought the malloc'd memory
    // is never accessed.
    //lock = (rcu_lock_t *)malloc(sizeof(rcu_lock_t));
    //*********************************************
    lock = &My_Lock;
    memset(lock, 0xAB, sizeof(rcu_lock_t));

    lock->epoch_list = NULL;
    lock->rcu_epoch = 0;

    lock->block.head = 0;
    pthread_mutex_init(&lock->rcu_writer_lock, NULL);

    return lock;
}

void lock_thread_init(void *lock, int thread_id)
{
    //*********************************************
    // the following line, if uncommented, will make RCU run much slower
    // at higher thread counts. This is true even thought the malloc'd memory
    // is never accessed.
    //epoch_list_t *epoch = (epoch_list_t *)malloc(sizeof(epoch_list_t));
    //*********************************************
    int ii;
    rcu_lock_t *rcu_lock = (rcu_lock_t *)lock;

    //Thread_Epoch = epoch;
    Thread_Epoch = &My_Thread_Epoch;

    // initialize per thread counters
    for (ii=1; ii<=NSTATS; ii++)
    {
        Thread_Stats[ii] = 0;
    }
    Thread_Stats[0] = NSTATS;

    // create a thread private epoch 
    //epoch = (epoch_list_t *)malloc(sizeof(epoch_list_t));
    //if (epoch == NULL)
    //{
        //exit(-1);
    //}
    
	/* guard against multiple thread start-ups and grace periods */
	pthread_mutex_lock(&rcu_lock->rcu_writer_lock);

    // init the thread local epoch indirectly
    //Thread_Epoch = &(epoch->epoch.epoch);   // Thread_Epoch points into the
                                            // thread's epoch data structure
                                            // just malloc'd
    Thread_Epoch->epoch = rcu_lock->rcu_epoch;  // Thread is in the current epoch
    Thread_Epoch->next = rcu_lock->epoch_list;

    // add the new epoch into the list
    rcu_lock->epoch_list = Thread_Epoch;

	// Let other synchronize_rcu() instances move ahead.
	pthread_mutex_unlock(&rcu_lock->rcu_writer_lock);
}

void rcu_synchronize(void *lock)
{
    volatile epoch_list_t *list;
    rcu_lock_t *rcu_lock = (rcu_lock_t *)lock;
    int head;

    Thread_Stats[STAT_SYNC]++;
    
	// Advance to a new grace-period number, enforce ordering.
    rcu_lock->rcu_epoch += 2;
    lock_mb();

	/*
	 * Wait until all active threads are out of their RCU read-side
	 * critical sections or have seen the current epoch.
	 */

    list = rcu_lock->epoch_list;
    while (list != NULL)
    {
        while (list->epoch < rcu_lock->rcu_epoch && 
            (list->epoch & 0x01)) 
        {
            Thread_Stats[STAT_SPINS]++;
            // wait
            lock_mb();
        }
        list = list->next;
    }

    // since a grace period just expired, we might as well clear out the
    // delete buffer
    //tail = rcu_lock->block.tail;
    head = rcu_lock->block.head;
    while (head > 0)
    {
        head--;
        //rbnode_free(rcu_lock->block.block[head]);
        free(rcu_lock->block.block[head]);
    }

    rcu_lock->block.head = 0;
}

void rcu_free(void *lock, void *ptr)
{
    rcu_lock_t *rcu_lock = (rcu_lock_t *)lock;

    Thread_Stats[STAT_FREE]++;

    if (rcu_lock->block.head >= RCU_MAX_BLOCKS-1) rcu_synchronize(lock);


    rcu_lock->block.block[rcu_lock->block.head] = ptr;
    rcu_lock->block.head++;
}
