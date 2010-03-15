#include <stdlib.h>
#include <assert.h>

#include "lock.h"

#define RWL_READ_INC    2
#define RWL_ACTIVE_WRITER_FLAG   1

char *implementation_name()
{
    return "RWL_read";
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
    volatile AO_t write_requests;
    volatile AO_t write_completions;
    volatile AO_t reader_count_and_flag;
} rwl_lock_t;

//static volatile rwl_lock_t RWL_Lock;

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
    AO_fetch_and_add_full(&(lock->reader_count_and_flag), RWL_READ_INC);
    while (AO_load(&(lock->reader_count_and_flag)) & RWL_ACTIVE_WRITER_FLAG)
    {
        // wait
        Thread_Stats[STAT_RSPIN]++;
    }
    //assert((AO_load(&lock->reader_count_and_flag) & RWL_ACTIVE_WRITER_FLAG) == 0);
}
//**********************************************
void read_unlock(void *vlock)
{
    rwl_lock_t *lock = (rwl_lock_t *)vlock;

    //assert((AO_load(&lock->reader_count_and_flag) & RWL_ACTIVE_WRITER_FLAG) == 0);
    AO_fetch_and_add_full(&(lock->reader_count_and_flag), -RWL_READ_INC);
}
//**********************************************
void write_lock(void *vlock)
{
    rwl_lock_t *lock = (rwl_lock_t *)vlock;

    Thread_Stats[STAT_WRITE]++;
    while (!AO_compare_and_swap_full(&lock->reader_count_and_flag, 
                0, RWL_ACTIVE_WRITER_FLAG))
    {
        // wait
        Thread_Stats[STAT_WSPIN]++;
    }
    //assert((AO_load(&lock->reader_count_and_flag) & RWL_ACTIVE_WRITER_FLAG) != 0);
}
//**********************************************
void write_unlock(void *vlock)
{
    rwl_lock_t *lock = (rwl_lock_t *)vlock;

   //assert((AO_load(&lock->reader_count_and_flag) & RWL_ACTIVE_WRITER_FLAG) != 0);
    AO_fetch_and_add_full(&lock->reader_count_and_flag, -1);
}
//**********************************************
/*
void rcu_synchronize(void *lock)
{
    assert(0);
}

*/
void rcu_free(void *lock, void (*func)(void *ptr), void *ptr) {func(ptr);}
