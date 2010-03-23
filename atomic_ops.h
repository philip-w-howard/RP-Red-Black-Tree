typedef unsigned long AO_t;

#include <urcu/compiler.h>
#include <urcu/arch.h>
#include <urcu/system.h>
#include <urcu/uatomic_arch.h>

#define AO_load(a) uatomic_read(a)
#define AO_store(a,v) uatomic_set(a,v)
#define AO_fetch_and_add_full(a,v) uatomic_add_return(a,v)
#define AO_nop_full() smp_mb()
#define AO_compare_and_swap_full(addr, old, _new) uatomic_cmpxchg(addr, old, _new)
