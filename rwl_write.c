#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "lock.h"
#include "atomic_ops.h"

#define RWL_READ_INC    2
#define RWL_ACTIVE_WRITER_FLAG   1

char *implementation_name()
{
    return "RWL_write";
}

#define NSTATS      9
#define STAT_READ   6
#define STAT_WRITE  7
#define STAT_RSPIN  8
#define STAT_WSPIN  9

static __thread __attribute__((__aligned__(CACHE_LINE_SIZE)))
    unsigned long long Thread_Stats[NSTATS+1];

typedef struct
{
    AO_t write_requests;
    AO_t write_completions;
    AO_t reader_count_and_flag;
} rwl_lock_t;

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
//*********************************************
void lock_status(void *vlock, char *text)
{
    rwl_lock_t *lock = (rwl_lock_t *)vlock;

    AO_t temp1 = AO_load(&(lock->write_requests));
    AO_t temp2 = AO_load(&(lock->write_completions));
    AO_t temp3 = AO_load(&(lock->reader_count_and_flag));
    printf("%s %lX %lX %lX %lX\n", text, (unsigned long)vlock, temp1, temp2, temp3);
}
//**********************************************
void lock_thread_init(void *lock, int thread_id)
{
    int ii;

    for (ii=1; ii<NSTATS; ii++)
    {
        Thread_Stats[ii] = 0;
    }
    Thread_Stats[0] = NSTATS;
}

void lock_thread_close(void *arg, int thread_id) {}
//**********************************************
void *lock_init()
{
    rwl_lock_t *lock = (rwl_lock_t *)malloc(sizeof(rwl_lock_t));
    assert(lock != NULL);

    AO_store(&lock->write_requests, 0);
    AO_store(&lock->write_completions, 0);
    AO_store(&lock->reader_count_and_flag, 0);

    return lock;
}
//**********************************************
void read_lock(void *vlock)
{
    rwl_lock_t *lock = (rwl_lock_t *)vlock;

    Thread_Stats[STAT_READ]++;

    //lock_status(vlock, "pre read_lock");
    while ( AO_load(&(lock->write_requests)) != 
            AO_load(&(lock->write_completions)))
    {
        // wait
        Thread_Stats[STAT_RSPIN]++;
        //backoff_delay();
    }

    //backoff_reset();
    AO_fetch_and_add_full(&(lock->reader_count_and_flag), RWL_READ_INC);
    while (AO_load(&(lock->reader_count_and_flag)) & RWL_ACTIVE_WRITER_FLAG)
    {
        // wait
        Thread_Stats[STAT_RSPIN]++;
        //backoff_delay();
    }
    //lock_status(vlock, "post read_lock");
}
//**********************************************
void read_unlock(void *vlock)
{
    rwl_lock_t *lock = (rwl_lock_t *)vlock;

    //assert((AO_load(&lock->reader_count_and_flag) & RWL_ACTIVE_WRITER_FLAG) == 0);
    AO_fetch_and_add_full(&(lock->reader_count_and_flag), -RWL_READ_INC);
    //lock_status(vlock, "post read_UNlock");
 }
//**********************************************
void write_lock(void *vlock)
{
    rwl_lock_t *lock = (rwl_lock_t *)vlock;

    unsigned int previous_writers;

    //lock_status(vlock, "pre write_lock");
    previous_writers = AO_fetch_and_add_full(&lock->write_requests, 1);

    Thread_Stats[STAT_WRITE]++;
    //backoff_reset();
    while (previous_writers != AO_load(&lock->write_completions))
    {
        // wait
        Thread_Stats[STAT_WSPIN]++;
        //backoff_delay();
    }

    //backoff_reset();
    while (!AO_compare_and_swap_full(&lock->reader_count_and_flag, 0, RWL_ACTIVE_WRITER_FLAG))
    {
        // wait
        Thread_Stats[STAT_WSPIN]++;
        //backoff_delay();
    }

    //assert((AO_load(&lock->reader_count_and_flag) & RWL_ACTIVE_WRITER_FLAG) != 0);
    //lock_status(vlock, "post write_lock");
}
//**********************************************
void upgrade_lock(void *vlock)
{
    rwl_lock_t *lock = (rwl_lock_t *)vlock;

    unsigned int previous_writers;

    //lock_status(vlock, "pre upgrade_lock");
    previous_writers = AO_fetch_and_add_full(&lock->write_requests, 1);

    Thread_Stats[STAT_WRITE]++;
    //backoff_reset();
    while (previous_writers != AO_load(&lock->write_completions))
    {
        // wait
        Thread_Stats[STAT_WSPIN]++;
        //backoff_delay();
    }

    //backoff_reset();
    while (!AO_compare_and_swap_full(&lock->reader_count_and_flag, 
                                     RWL_READ_INC, 
                                     RWL_ACTIVE_WRITER_FLAG))
    {
        // wait
        Thread_Stats[STAT_WSPIN]++;
        //backoff_delay();
    }

    //assert((AO_load(&lock->reader_count_and_flag) & RWL_ACTIVE_WRITER_FLAG) != 0);
    //lock_status(vlock, "post upgrade_lock");
}
//**********************************************
void write_unlock(void *vlock)
{
    rwl_lock_t *lock = (rwl_lock_t *)vlock;

   //assert((AO_load(&lock->reader_count_and_flag) & RWL_ACTIVE_WRITER_FLAG) != 0);
    AO_fetch_and_add_full(&lock->reader_count_and_flag, -RWL_ACTIVE_WRITER_FLAG);
    AO_fetch_and_add_full(&lock->write_completions, 1);
    //lock_status(vlock, "post write UNlock");

    assert( (AO_load(&lock->write_completions) <= AO_load(&lock->write_requests) ) );
}
