//#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <memory.h>
#include <pthread.h>
#include <assert.h>

#include "atomic_ops.h"
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
#define STAT_RWSPINS  10
#define STAT_FREE   11

static __thread __attribute__((__aligned__(CACHE_LINE_SIZE)))
    unsigned long long Thread_Stats[NSTATS+1];

#define RCU_MAX_BLOCKS      20
#define RWL_READ_INC        2
#define RWL_ACTIVE_WRITER_FLAG 1

typedef struct epoch_s
{
    __attribute__((__aligned__(CACHE_LINE_SIZE))) long long epoch;
    volatile __attribute__((__aligned__(CACHE_LINE_SIZE))) 
        struct epoch_s *next;
} epoch_list_t;

typedef struct
{
    void *block;
    void (*func)(void *ptr);
} block_item_t;
typedef struct 
{
    block_item_t block[RCU_MAX_BLOCKS];
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
        AO_t write_requests;
    __attribute__((__aligned__(CACHE_LINE_SIZE))) 
        AO_t write_completions;
    __attribute__((__aligned__(CACHE_LINE_SIZE))) 
        AO_t reader_count_and_flag;
    __attribute__((__aligned__(CACHE_LINE_SIZE))) 
        pthread_mutex_t rcu_writer_lock;
} rcu_lock_t;

static __thread __attribute__((__aligned__(CACHE_LINE_SIZE))) 
        epoch_list_t *Thread_Epoch;
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
#warning("RCU need a memory barrier on this architecture");
    lock_mb();
#endif

}

void read_unlock(void *lock)
{
    rcu_lock_t *rcu_lock = (rcu_lock_t *)lock;

    Thread_Epoch->epoch = rcu_lock->rcu_epoch;
}

#ifdef RCU_USE_MUTEX
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

void rw_lock(void *lock)
{
    write_lock(lock);
}
void rw_unlock(void *lock)
{
    write_unlock(lock);
}
#else
//**********************************************
void rw_lock(void *vlock)
{
    rcu_lock_t *lock = (rcu_lock_t *)vlock;

    Thread_Stats[STAT_READ]++;

    //lock_status(vlock, "read");
    //backoff_reset();
    while ( AO_load(&(lock->write_requests)) != 
            AO_load(&(lock->write_completions)))
    {
        // wait
        //lock_status(vlock, "rspin1");
        Thread_Stats[STAT_RWSPINS]++;
        //backoff_delay();
    }

    //backoff_reset();
    AO_fetch_and_add_full(&(lock->reader_count_and_flag), RWL_READ_INC);
    //lock_status(vlock, "read2");
    while (AO_load(&(lock->reader_count_and_flag)) & RWL_ACTIVE_WRITER_FLAG)
    {
        //int temp = AO_load(&lock->reader_count_and_flag);
        //printf("read_lock lspin %X\n", temp);
        // wait
        //lock_status(vlock, "rspin2");
        Thread_Stats[STAT_RWSPINS]++;
        //backoff_delay();
    }
    //lock_status(vlock, "read locked");
}
//**********************************************
void rw_unlock(void *vlock)
{
    rcu_lock_t *lock = (rcu_lock_t *)vlock;

    //assert((AO_load(&lock->reader_count_and_flag) & RWL_ACTIVE_WRITER_FLAG) == 0);
    AO_fetch_and_add_full(&(lock->reader_count_and_flag), -RWL_READ_INC);
}
//**********************************************
void write_lock(void *vlock)
{
    rcu_lock_t *lock = (rcu_lock_t *)vlock;

    unsigned int previous_writers;

    //lock_status(vlock, "writer");

    previous_writers = AO_fetch_and_add_full(&lock->write_requests, 1);

    Thread_Stats[STAT_WRITE]++;
    //backoff_reset();
    while (previous_writers != AO_load(&lock->write_completions))
    {
        // wait
        //lock_status(vlock, "wspin1");
        Thread_Stats[STAT_RWSPINS]++;
        //backoff_delay();
    }

    //backoff_reset();
    while (!AO_compare_and_swap_full(&lock->reader_count_and_flag, 0, RWL_ACTIVE_WRITER_FLAG))
    {
        // wait
        //lock_status(vlock, "wspin2");
        Thread_Stats[STAT_RWSPINS]++;
        //backoff_delay();
    }

    //lock_status(vlock, "writer locked");

    /*
    Thread_Stats[STAT_WRITE]++;
    while (!AO_compare_and_swap_full(&lock->reader_count_and_flag, 
                0, RWL_ACTIVE_WRITER_FLAG))
    {
        // wait
        //lock_status(vlock, "wspin3");
        Thread_Stats[STAT_RWSPINS]++;
    }
    //assert((AO_load(&lock->reader_count_and_flag) & RWL_ACTIVE_WRITER_FLAG) != 0);
    */
}
//**********************************************
void write_unlock(void *vlock)
{
    rcu_lock_t *lock = (rcu_lock_t *)vlock;

   //assert((AO_load(&lock->reader_count_and_flag) & RWL_ACTIVE_WRITER_FLAG) != 0);
    AO_fetch_and_add_full(&lock->reader_count_and_flag, -1);
    AO_fetch_and_add_full(&lock->write_completions, 1);
}
#endif

void *lock_init()
{
    rcu_lock_t *lock;
    lock = (rcu_lock_t *)malloc(sizeof(rcu_lock_t));

    lock->epoch_list = NULL;
    lock->rcu_epoch = 0;

    lock->block.head = 0;
    pthread_mutex_init(&lock->rcu_writer_lock, NULL);

    AO_store(&lock->write_requests, 0);
    AO_store(&lock->write_completions, 0);
    AO_store(&lock->reader_count_and_flag, 0);

    return lock;
}

void lock_thread_init(void *lock, int thread_id)
{
    int ii;
    rcu_lock_t *rcu_lock = (rcu_lock_t *)lock;

    // initialize per thread counters
    for (ii=1; ii<=NSTATS; ii++)
    {
        Thread_Stats[ii] = 0;
    }
    Thread_Stats[0] = NSTATS;

    // create a thread private epoch 
    Thread_Epoch = (epoch_list_t *)malloc(sizeof(epoch_list_t));
    
	/* guard against multiple thread start-ups and grace periods */
	pthread_mutex_lock(&rcu_lock->rcu_writer_lock);

    Thread_Epoch->epoch = rcu_lock->rcu_epoch;  // Thread is in the current epoch
    Thread_Epoch->next = rcu_lock->epoch_list;

    // add the new epoch into the list
    rcu_lock->epoch_list = Thread_Epoch;

	// Let other synchronize_rcu() instances move ahead.
	pthread_mutex_unlock(&rcu_lock->rcu_writer_lock);
}

void lock_thread_close(void *arg, int thread_id) {}

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
    head = rcu_lock->block.head;
    //printf("RCU is freeing %d blocks\n", head);
    while (head > 0)
    {
        void (*func)(void *ptr);

        head--;
        //rbnode_free(rcu_lock->block.block[head]);
        func = rcu_lock->block.block[head].func;
        func(rcu_lock->block.block[head].block);
    }

    rcu_lock->block.head = 0;
}

void rcu_free(void *lock, void (*func)(void *ptr), void *ptr)
{
    rcu_lock_t *rcu_lock = (rcu_lock_t *)lock;

    Thread_Stats[STAT_FREE]++;

    if (rcu_lock->block.head >= RCU_MAX_BLOCKS) rcu_synchronize(lock);


    rcu_lock->block.block[rcu_lock->block.head].block = ptr;
    rcu_lock->block.block[rcu_lock->block.head].func = func;
    rcu_lock->block.head++;
}
