//Copyright (c) 2010 Philip W. Howard
//
//Permission is hereby granted, free of charge, to any person obtaining a copy
//of this software and associated documentation files (the "Software"), to deal
//in the Software without restriction, including without limitation the rights
//to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//copies of the Software, and to permit persons to whom the Software is
//furnished to do so, subject to the following conditions:
//
//The above copyright notice and this permission notice shall be included in
//all copies or substantial portions of the Software.
//
//THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
//THE SOFTWARE.

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

char *implementation_name() { return "PRWLOCK"; }
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
    pthread_rwlock_t *lock = (pthread_rwlock_t *)malloc(sizeof(pthread_rwlock_t));

    // PTHREAD_RWLOCK_PREFER_READER_NP, WRITER, WRITER_NONRECURSIVE
    pthread_rwlock_init(lock, PTHREAD_RWLOCK_PREFER_READER_NP);
    //printf("lock init %p\n", lock);

    return lock; 
}
void read_lock(void *lock)
{
    Thread_Stats[STAT_READ]++;
    //printf("read lock %p\n", lock);
    pthread_rwlock_rdlock( (pthread_rwlock_t *)lock);
}
void read_unlock(void *lock) 
{
    //printf("read unlock %p\n", lock);
    pthread_rwlock_unlock( (pthread_rwlock_t *)lock);
}
void write_lock(void *lock)
{
    Thread_Stats[STAT_WRITE]++;
    //printf("write lock %p\n", lock);
    pthread_rwlock_wrlock( (pthread_rwlock_t *)lock);
}
void write_unlock(void *lock)
{
    //printf("write unlock %p\n", lock);
    pthread_rwlock_unlock( (pthread_rwlock_t *)lock);
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
