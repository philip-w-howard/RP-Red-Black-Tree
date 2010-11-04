#ifndef __RCU_H
#define __RCU_H

#include <pthread.h>
#include "atomic_ops.h"

#ifndef CACHE_LINE_SIZE
#define CACHE_LINE_SIZE     128
#endif

#if defined(RCU) && defined(URCU)

//#define rp_free(l, f, p) defer_rcu(f,p)
void rp_wait_grace_period(void *lock);
void rp_free(void *lock, void (*func)(void *ptr), void *ptr);
#define rp_poss(a) ({ 0; })

#elif defined(RCU) || defined(NO_GRACE_PERIOD)

/* Assume DEC Alpha is dead.  Long live DEC Alpha. */
#define rp_dereference(p) (*(volatile typeof(p) *)&(p))
#define rp_assign_pointer(p, v) ({ lock_mb(); (p) = (v); })
void rp_synchronize(void *lock);
void rp_free(void *lock, void (*func)(void *ptr), void *ptr);
int rp_poll(void *lock);

#elif defined(RP_STM)

void rp_free(void *lock, void (*func)(void *ptr), void *ptr);
#define rp_dereference(p) (*(volatile typeof(p) *)&(p))
#define rp_assign_pointer(p, v) ({ lock_mb(); (p) = (v); })

#else

#define rp_assign_pointer(p,v) ({(p) = (v);})
#define rp_dereference(p) (*(volatile typeof(p) *)&(p))
#define rp_free(l,f,a) ({ (f)( (a) ); })
#define rp_poss(a) ({ 0; })

#endif
#endif  // __RCU_H

