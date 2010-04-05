#ifndef __RCU_H
#define __RCU_H

#include <pthread.h>
#include "atomic_ops.h"

#ifndef CACHE_LINE_SIZE
#define CACHE_LINE_SIZE     128
#endif

#if defined(RCU) && defined(URCU)

//#define rcu_free(l, f, p) defer_rcu(f,p)
void rcu_synchronize(void *lock);
void rcu_free(void *lock, void (*func)(void *ptr), void *ptr);

#elif defined(RCU)

/* Assume DEC Alpha is dead.  Long live DEC Alpha. */
#define rcu_dereference(p) (*(volatile typeof(p) *)&(p))
#define rcu_assign_pointer(p, v) ({ lock_mb(); (p) = (v); })
void rcu_synchronize(void *lock);
void rcu_free(void *lock, void (*func)(void *ptr), void *ptr);

#else

#define rcu_assign_pointer(p,v) ({(p) = (v);})
#define rcu_dereference(p) (*(volatile typeof(p) *)&(p))
#define rcu_free(l,f,a) ({ (f)( (a) ); })

#endif
#endif  // __RCU_H

