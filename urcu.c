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

//#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <atomic_ops.h>
#include <memory.h>
#include <pthread.h>

#include <urcu.h>
//#include <urcu-defer.h>

#include "lock.h"
#include "rbnode.h"

#define container_of(ptr, type, member) ({\
        const typeof( ((type *)0)->member) *__mptr = (ptr);\
        (type *)( (char *)__mptr - offsetof(type,member) );})

char *implementation_name()
{
    return "URCU";
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
    //volatile __attribute__((__aligned__(CACHE_LINE_SIZE))) 
    //    block_list_t block;
    __attribute__((__aligned__(CACHE_LINE_SIZE))) 
        pthread_mutex_t lock;
} urcu_lock_t;

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

void read_lock(void *lock)
{

    Thread_Stats[STAT_READ]++;
    rcu_read_lock();
}

void read_unlock(void *lock)
{
    rcu_read_unlock();
}

void write_lock(void *lock)
{
    urcu_lock_t *urcu_lock = (urcu_lock_t *)lock;
    Thread_Stats[STAT_WRITE]++;
    pthread_mutex_lock( &urcu_lock->lock );
}

void write_unlock(void *lock)
{
    urcu_lock_t *urcu_lock = (urcu_lock_t *)lock;
    pthread_mutex_unlock( &urcu_lock->lock );
}

void *lock_init()
{
    urcu_lock_t *lock;
    //*********************************************
    // the following line, if uncommented, will make RCU run much slower
    // at higher thread counts. This is true even thought the malloc'd memory
    // is never accessed.
    lock = (urcu_lock_t *)malloc(sizeof(urcu_lock_t));
    //*********************************************
    pthread_mutex_init(&lock->lock, NULL);

    return lock;
}

void lock_thread_init(void *lock, int thread_id)
{
    int ii;

    // initialize per thread counters
    for (ii=1; ii<=NSTATS; ii++)
    {
        Thread_Stats[ii] = 0;
    }
    Thread_Stats[0] = NSTATS;

    rcu_register_thread();
    //rcu_defer_register_thread();
}

void lock_thread_close(void *lock, int thread_id)
{
    rcu_unregister_thread();
    //rcu_defer_unregister_thread();
}

void rcu_synchronize(void *lock)
{
    //urcu_lock_t *urcu_lock = (urcu_lock_t *)lock;
    //int head;

    synchronize_rcu();

    Thread_Stats[STAT_SYNC]++;
    
#ifdef REMOVE
    // since a grace period just expired, we might as well clear out the
    // delete buffer
    head = urcu_lock->block.head;
    while (head > 0)
    {
        void (*func)(void *ptr);

        head--;
        //rbnode_free(rcu_lock->block.block[head]);
        func = urcu_lock->block.block[head].func;
        func(urcu_lock->block.block[head].block);
    }

    urcu_lock->block.head = 0;
#endif
}

#ifdef REMOVE
void nonu_rcu_free(void *lock, void (*func)(void *ptr), void *ptr)
{
    urcu_lock_t *urcu_lock = (urcu_lock_t *)lock;

    Thread_Stats[STAT_FREE]++;

    if (urcu_lock->block.head >= RCU_MAX_BLOCKS-1) rcu_synchronize(lock);


    urcu_lock->block.block[urcu_lock->block.head].block = ptr;
    urcu_lock->block.block[urcu_lock->block.head].func = func;
    urcu_lock->block.head++;
}
#endif

static void free_func(struct rcu_head *head)
{
    rbnode_t *node = container_of(head, rbnode_t, urcu_head);
    node->func(node);
}

void rcu_free(void *lock, void (*func)(void *ptr), void *ptr)
{
    rbnode_t *node = (rbnode_t *)ptr;
    node->func = func;
//    call_rcu(&node->urcu_head, free_func);
}
