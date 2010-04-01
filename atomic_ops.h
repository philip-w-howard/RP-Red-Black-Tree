#ifndef __PWH_ATOMIC_OPS
#define __PWH_ATOMIC_OPS

#ifdef __sparc__

#include <sys/atomic.h>

typedef uint32_t AO_t;

#define lock_mb() ({ asm volatile("membar #StoreStore"); } )

#define AO_load(a) (*(a))
#define AO_store(a,v) ({*(a) = (v);} )
#define AO_fetch_and_add_full(a,v) \
    ({AO_t ______tmp_pwh; \
      membar_exit(); \
     ______tmp_pwh = atomic_add_32_nv((a),(v));\
     membar_enter();\
     (______tmp_pwh - (v)); })

#define AO_nop_full() {membar_consumer(); membar_producer();}
#define AO_compare_and_swap_full(addr, old, _new) \
    (atomic_cas_32((addr), (old), (_new)) == (old))

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

