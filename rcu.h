#ifndef __RCU_H
#define __RCU_H

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

#include <pthread.h>
#include "atomic_ops.h"

#ifndef CACHE_LINE_SIZE
#define CACHE_LINE_SIZE     128
#endif

#if defined(RCU) && defined(URCU)

//#define rp_free(l, f, p) defer_rcu(f,p)
void rp_wait_grace_period(void *lock);
void rp_free(void *lock, void (*func)(void *ptr), void *ptr);

#elif defined(RCU) || defined(NO_GRACE_PERIOD)

/* Assume DEC Alpha is dead.  Long live DEC Alpha. */
#define rp_dereference(p) (*(volatile typeof(p) *)&(p))
#define rp_assign_pointer(p, v) ({ lock_mb(); (p) = (v); })
void rp_wait_grace_period(void *lock);
void rp_free(void *lock, void (*func)(void *ptr), void *ptr);
int rp_poll(void *lock);

#elif defined(RP_STM)

void rp_free(void *lock, void (*func)(void *ptr), void *ptr);
#define rp_dereference(p) (*(volatile typeof(p) *)&(p))
//#define rp_assign_pointer(p, v) ({ lock_mb(); (p) = (v); })

#else

#define rp_assign_pointer(p,v) ({(p) = (v);})
#define rp_dereference(p) (*(volatile typeof(p) *)&(p))
#define rp_free(l,f,a) ({ (f)( (a) ); })

#endif
#endif  // __RCU_H

