#ifndef __LOCK_H
#define __LOCK_H
//#include <atomic_ops.h>
#include <pthread.h>
#include "atomic_ops.h"

#ifndef CACHE_LINE_SIZE
#define CACHE_LINE_SIZE     128
#endif

char *implementation_name();

void lock_thread_init(void *arg, int thread_id);
void lock_thread_close(void *arg, int thread_id);
void *lock_init();
void read_lock(void *lock);
void read_unlock(void *lock);
void write_lock(void *lock);
void write_unlock(void *lock);

// optional routine
void upgrade_lock(void *lock);

#ifdef RCU
void rw_lock(void *lock);
void rw_unlock(void *lock);
#else
#define rw_lock read_lock
#define rw_unlock read_unlock
#endif

unsigned long long *get_thread_stats(unsigned long long n_reads,
        unsigned long long n_inserts, unsigned long long n_insert_fails,
        unsigned long long n_deletes, unsigned long long n_delete_fails);

#endif

