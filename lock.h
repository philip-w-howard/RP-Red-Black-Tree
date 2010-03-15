#ifndef __RCU_H
#define __RCU_H
#include <atomic_ops.h>
#include <pthread.h>

#ifndef CACHE_LINE_SIZE
#define CACHE_LINE_SIZE     128
#endif

char *implementation_name();

#define lock_mb()        AO_nop_full()

void lock_thread_init(void *arg, int thread_id);
void *lock_init();
void read_lock(void *lock);
void read_unlock(void *lock);
void write_lock(void *lock);
void write_unlock(void *lock);
void rcu_synchronize(void *lock);
void rcu_free(void *lock, void (*func)(void *ptr), void *ptr);

#ifdef RCU
#define rcu_assign_pointer(p, v) ({ lock_mb(); (p) = (v); })
#else
#define rcu_assign_pointer(p,v) ({(p) = (v);})
#endif

/* Assume DEC Alpha is dead.  Long live DEC Alpha. */
#define rcu_dereference(p) (*(volatile typeof(p) *)&(p))

unsigned long long *get_thread_stats(unsigned long long n_reads,
        unsigned long long n_inserts, unsigned long long n_insert_fails,
        unsigned long long n_deletes, unsigned long long n_delete_fails);

#endif

