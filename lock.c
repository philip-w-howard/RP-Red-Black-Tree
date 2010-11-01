#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#include "lock.h"

#define NSTATS      9
#define STAT_READ   6
#define STAT_WRITE  7
#define STAT_RSPIN  8
#define STAT_WSPIN  9

static __thread __attribute__((__aligned__(CACHE_LINE_SIZE)))
    unsigned long long Thread_Stats[NSTATS+1];

char *implementation_name() { return "LOCK"; }
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
void *lock_init()
{
    pthread_mutex_t *lock = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));

    pthread_mutex_init(lock, NULL);
    //printf("lock init %p\n", lock);

    return lock; 
}
void read_lock(void *lock)
{
    Thread_Stats[STAT_READ]++;
    //printf("read lock %p\n", lock);
    pthread_mutex_lock( (pthread_mutex_t *)lock);
}
void read_unlock(void *lock) 
{
    //printf("read unlock %p\n", lock);
    pthread_mutex_unlock( (pthread_mutex_t *)lock);
}
void write_lock(void *lock)
{
    Thread_Stats[STAT_WRITE]++;
    //printf("write lock %p\n", lock);
    pthread_mutex_lock( (pthread_mutex_t *)lock);
}
void write_unlock(void *lock)
{
    //printf("write unlock %p\n", lock);
    pthread_mutex_unlock( (pthread_mutex_t *)lock);
}
//void rp_wait_grace_period(void *lock) {}
void rp_free(void *lock, void (*func)(void *ptr), void *ptr) {func(ptr);}

//**********************************************
unsigned long long *get_thread_stats(unsigned long long a, unsigned long long b,
        unsigned long long c, unsigned long long d, unsigned long long e,
        unsigned long long f)
{
    Thread_Stats[1] = a;
    Thread_Stats[2] = b;
    Thread_Stats[3] = c;
    Thread_Stats[4] = d;
    Thread_Stats[5] = e;
    Thread_Stats[6] = f;

    return Thread_Stats;
}
