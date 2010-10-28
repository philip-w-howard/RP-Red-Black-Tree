/* wlpdstm_global_init();
 wlpdstm_thread_init();
 wlpdstm_tx_malloc(size);
 wlpdstm_tx_free(ptr, size);
 wlpdstm_print_stats();
*/
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

#include "my_stm.h"
#include "lock.h"
#include "rbnode.h"

#define NSTATS      9
#define STAT_READ   6
#define STAT_WRITE  7
#define STAT_RSPIN  8
#define STAT_WSPIN  9

static __thread __attribute__((__aligned__(CACHE_LINE_SIZE)))
    unsigned long long Thread_Stats[NSTATS+1];

static pthread_mutex_t Lock = PTHREAD_MUTEX_INITIALIZER;

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
char *implementation_name() { return "STM"; }
//**********************************************
void lock_thread_init(void *lock, int thread_id)
{
    int ii;
    static int first = 1;

    pthread_mutex_lock(&Lock);

    if (first)
    {
        wlpdstm_global_init();
        first = 0;
    }

    pthread_mutex_unlock(&Lock);

    for (ii=1; ii<NSTATS; ii++)
    {
        Thread_Stats[ii] = 0;
    }
    Thread_Stats[0] = NSTATS;

    wlpdstm_thread_init();
}

void lock_thread_close(void *arg, int thread_id) {}
void *lock_init() { return NULL; }
void read_lock(void *lock) { Thread_Stats[STAT_READ]++; }
void read_unlock(void *lock) {}
void write_lock(void *lock) { Thread_Stats[STAT_WRITE]++; }
void write_unlock(void *lock) {}
//void rcu_synchronize(void *lock) {}
void rcu_free(void *lock, void (*func)(void *ptr), void *ptr) { func(ptr); }

void DO_STORE(Word *a, Word b)
{
    printf("STORE(%p, %llX)\n", a, b);
    wlpdstm_write_word(a, b);
}
