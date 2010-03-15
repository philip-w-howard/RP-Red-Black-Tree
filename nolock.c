#include <stdlib.h>
#include <stdio.h>

#include "lock.h"

#define NSTATS      7
#define STAT_READ   6
#define STAT_WRITE  7
#define STAT_RSPIN  8
#define STAT_WSPIN  9

static __thread __attribute__((__aligned__(CACHE_LINE_SIZE)))
    unsigned long long Thread_Stats[NSTATS+1];

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
char *implementation_name() { return "NOLOCK"; }
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
void *lock_init() { return NULL; }
void read_lock(void *lock) { Thread_Stats[STAT_READ]++; }
void read_unlock(void *lock) {}
void write_lock(void *lock) { Thread_Stats[STAT_WRITE]++; }
void write_unlock(void *lock) {}
//void rcu_synchronize(void *lock) {}
void rcu_free(void *lock, void (*func)(void *ptr), void *ptr) {func(ptr);}

