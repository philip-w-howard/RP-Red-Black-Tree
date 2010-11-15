#ifndef __PWH_ATOMIC_OPS
#define __PWH_ATOMIC_OPS

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

#ifdef __sparc__

#include <sys/atomic.h>

typedef unsigned long long AO_t;

#define lock_mb() ({ asm volatile("membar #StoreStore"); } )

#define AO_load(a) (*(a))
#define AO_store(a,v) ({*(a) = (v);} )
#define AO_fetch_and_add_full(a,v) \
    ({AO_t ______tmp_pwh; \
      membar_exit(); \
     ______tmp_pwh = atomic_add_64_nv((a),(v));\
     membar_enter();\
     (______tmp_pwh - (v)); })

#define AO_nop_full() {membar_consumer(); membar_producer();}
#define AO_compare_and_swap_full(addr, old, _new) \
    (atomic_cas_64((addr), (old), (_new)) == (old))

#elif defined(USE_URCU)

#define lock_mb() mb()

#include <urcu/compiler.h>
#include <urcu/arch.h>
#include <urcu/system.h>
#include <urcu/uatomic_arch.h>

typedef unsigned long AO_t;

#define AO_load(a) uatomic_read(a)
#define AO_store(a,v) uatomic_set(a,v)
#define AO_fetch_and_add_full(a,v) uatomic_add_return(a,v)
#define AO_nop_full() smp_mb()
#define AO_compare_and_swap_full(addr, old, _new) uatomic_cmpxchg(addr, old, _new)

#else

#define lock_mb()        AO_nop_full()

#include <atomic_ops.h>

#endif

#endif  // __ATOMIC_OPS

