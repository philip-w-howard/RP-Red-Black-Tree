#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

#include "my_stm.h"
#include "lock.h"
#include "rbnode.h"

#ifdef RP_STM
#include <memory.h>
#include <assert.h>
#include "atomic_ops.h"
#endif

#define NSTATS      13
#define STAT_READ   7
#define STAT_WRITE  8
#define STAT_SPINS  9
#define STAT_SYNC   10
#define STAT_RWSPINS  11
//#define STM_OP_FAILS  11
#define STAT_FREE   12
#define STAT_FREE_SYNC 13
//#define STM_START 14
//#define STM_COMMIT 15
//#define STM_WRITE  16

static __thread __attribute__((__aligned__(CACHE_LINE_SIZE)))
    unsigned long long Thread_Stats[NSTATS+1];

static pthread_mutex_t Lock = PTHREAD_MUTEX_INITIALIZER;

#ifdef RP_STM

#define RCU_MAX_BLOCKS      20
#define BLOCKS_FOR_FREE     (RCU_MAX_BLOCKS-2)

#define RWL_READ_INC        2
#define RWL_ACTIVE_WRITER_FLAG 1

typedef struct epoch_s
{
    pthread_t thread_id;
    __attribute__((__aligned__(CACHE_LINE_SIZE))) long long epoch;
    volatile __attribute__((__aligned__(CACHE_LINE_SIZE))) 
        struct epoch_s *next;
} epoch_list_t;

typedef struct
{
    void *block;
    void (*func)(void *ptr);
} block_item_t;
typedef struct 
{
    block_item_t block[RCU_MAX_BLOCKS];
    volatile int head;
} block_list_t;

typedef struct
{
    volatile __attribute__((__aligned__(CACHE_LINE_SIZE))) 
        epoch_list_t *epoch_list;
    volatile __attribute__((__aligned__(CACHE_LINE_SIZE))) 
        AO_t rp_epoch;
//    volatile __attribute__((__aligned__(CACHE_LINE_SIZE))) 
//        block_list_t block;
    __attribute__((__aligned__(CACHE_LINE_SIZE))) 
        AO_t write_requests;
    __attribute__((__aligned__(CACHE_LINE_SIZE))) 
        AO_t write_completions;
    __attribute__((__aligned__(CACHE_LINE_SIZE))) 
        AO_t reader_count_and_flag;
    __attribute__((__aligned__(CACHE_LINE_SIZE))) 
        pthread_mutex_t rp_writer_lock;
} rp_lock_t;

static __thread __attribute__((__aligned__(CACHE_LINE_SIZE))) 
        epoch_list_t *Thread_Epoch;

static __thread __attribute__((__aligned__(CACHE_LINE_SIZE)))
    block_list_t To_Be_Freed;

#endif

static __thread int Stm_Write = 0;
//**********************************************
/*
void stm_start()
{
    Thread_Stats[STM_START]++;
    //Stm_Write = 0;
}
void stm_op_grand_failed()
{
    Thread_Stats[STM_OP_FAILS]++;
}
void stm_op_failed()
{
    Thread_Stats[STM_WRITE]++;
}
void stm_write()
{
    //Stm_Write = 1;
}
void rp_stm_commit()
{
    //if (Stm_Write) Thread_Stats[STM_WRITE]++;
    //Stm_Write = 0;
    Thread_Stats[STM_COMMIT]++;
}
*/
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
char *implementation_name() { return "STM"; }
//**********************************************
void lock_thread_init(void *lock, int thread_id)
{
    int ii;
    static int first = 1;
#ifdef RP_STM
    rp_lock_t *rp_lock = (rp_lock_t *)lock;
#endif

    pthread_mutex_lock(&Lock);

    if (first)
    {
        wlpdstm_global_init();
        first = 0;
    }

    pthread_mutex_unlock(&Lock);

    for (ii=1; ii<NSTATS; ii++)
    {
        Thread_Stats[ii] = 0;
    }
    Thread_Stats[0] = NSTATS;

    wlpdstm_thread_init();

#ifdef RP_STM
    // create a thread private epoch 
    Thread_Epoch = (epoch_list_t *)malloc(sizeof(epoch_list_t));
    
    // init the per thread to-be-freed list
    To_Be_Freed.head = 0;

	/* guard against multiple thread start-ups and grace periods */
	write_lock(lock);

    Thread_Epoch->thread_id = pthread_self();
    Thread_Epoch->epoch = AO_load(&rp_lock->rp_epoch);  // Thread is in the current epoch
    Thread_Epoch->next = rp_lock->epoch_list;

    // add the new epoch into the list
    rp_lock->epoch_list = Thread_Epoch;

	// Let other rp_wait_grace_period() instances move ahead.
	write_unlock(lock);
#endif
}

void lock_thread_close(void *arg, int thread_id) {}
void *lock_init()
{
#ifdef RP_STM
    rp_lock_t *lock;
    lock = (rp_lock_t *)malloc(sizeof(rp_lock_t));

    assert(sizeof(long long) == sizeof(AO_t));

    lock->epoch_list = NULL;
    lock->rp_epoch = 0;

    //lock->block.head = 0;
    pthread_mutex_init(&lock->rp_writer_lock, NULL);

    AO_store(&lock->write_requests, 0);
    AO_store(&lock->write_completions, 0);
    AO_store(&lock->reader_count_and_flag, 0);

    return lock;
#else
    return NULL;
#endif
}
#ifdef RP_STM
void read_lock(void *lock)
{
    rp_lock_t *rp_lock = (rp_lock_t *)lock;

    Thread_Stats[STAT_READ]++;
    Thread_Epoch->epoch = AO_load(&rp_lock->rp_epoch);
    Thread_Epoch->epoch++;

    // the following statement, though useless, is required if we want
    // to avoid a memory barrier. 
    // Note that the if statement should never return true, so the mb
    // will never execute. Accessing Thread_Epoch is what gives us the
    // ordering guarantees we need (on an X86, anyway)
#if defined(__i386__) || defined(__x86_64__)
    if ( (Thread_Epoch->epoch & 0x0001) == 0) lock_mb();
#else
#warning("RCU need a memory barrier on this architecture");
    lock_mb();
#endif

}
void read_unlock(void *lock)
{
    Thread_Epoch->epoch--;
}

#ifdef RCU_USE_MUTEX
void write_lock(void *lock)
{
    rp_lock_t *rp_lock = (rp_lock_t *)lock;

    Thread_Stats[STAT_WRITE]++;
    pthread_mutex_lock(&rp_lock->rp_writer_lock);
}

void write_unlock(void *lock)
{
    rp_lock_t *rp_lock = (rp_lock_t *)lock;

    pthread_mutex_unlock(&rp_lock->rp_writer_lock);
}

void rw_lock(void *lock)
{
    write_lock(lock);
}
void rw_unlock(void *lock)
{
    write_unlock(lock);
}
#else
//**********************************************
void rw_lock(void *vlock)
{
    rp_lock_t *lock = (rp_lock_t *)vlock;

    Thread_Stats[STAT_READ]++;

    //lock_status(vlock, "read");
    //backoff_reset();
    while ( AO_load(&(lock->write_requests)) != 
            AO_load(&(lock->write_completions)))
    {
        // wait
        //lock_status(vlock, "rspin1");
        Thread_Stats[STAT_RWSPINS]++;
        //backoff_delay();
    }

    //backoff_reset();
    AO_fetch_and_add_full(&(lock->reader_count_and_flag), RWL_READ_INC);
    //lock_status(vlock, "read2");
    while (AO_load(&(lock->reader_count_and_flag)) & RWL_ACTIVE_WRITER_FLAG)
    {
        //int temp = AO_load(&lock->reader_count_and_flag);
        //printf("read_lock lspin %X\n", temp);
        // wait
        //lock_status(vlock, "rspin2");
        Thread_Stats[STAT_RWSPINS]++;
        //backoff_delay();
    }
    //lock_status(vlock, "read locked");
}
//**********************************************
void rw_unlock(void *vlock)
{
    rp_lock_t *lock = (rp_lock_t *)vlock;

    //assert((AO_load(&lock->reader_count_and_flag) & RWL_ACTIVE_WRITER_FLAG) == 0);
    AO_fetch_and_add_full(&(lock->reader_count_and_flag), -RWL_READ_INC);
}
//**********************************************
void write_lock(void *vlock)
{
    rp_lock_t *lock = (rp_lock_t *)vlock;

    unsigned int previous_writers;

    //lock_status(vlock, "writer");

    previous_writers = AO_fetch_and_add_full(&lock->write_requests, 1);

    Thread_Stats[STAT_WRITE]++;
    //backoff_reset();
    while (previous_writers != AO_load(&lock->write_completions))
    {
        // wait
        //lock_status(vlock, "wspin1");
        Thread_Stats[STAT_RWSPINS]++;
        //backoff_delay();
    }

    //backoff_reset();
    while (!AO_compare_and_swap_full(&lock->reader_count_and_flag, 0, RWL_ACTIVE_WRITER_FLAG))
    {
        // wait
        //lock_status(vlock, "wspin2");
        Thread_Stats[STAT_RWSPINS]++;
        //backoff_delay();
    }

    //lock_status(vlock, "writer locked");

    /*
    Thread_Stats[STAT_WRITE]++;
    while (!AO_compare_and_swap_full(&lock->reader_count_and_flag, 
                0, RWL_ACTIVE_WRITER_FLAG))
    {
        // wait
        //lock_status(vlock, "wspin3");
        Thread_Stats[STAT_RWSPINS]++;
    }
    //assert((AO_load(&lock->reader_count_and_flag) & RWL_ACTIVE_WRITER_FLAG) != 0);
    */
}
//**********************************************
void write_unlock(void *vlock)
{
    rp_lock_t *lock = (rp_lock_t *)vlock;

   assert((AO_load(&lock->reader_count_and_flag) & RWL_ACTIVE_WRITER_FLAG) != 0);
    AO_fetch_and_add_full(&lock->reader_count_and_flag, -1);
    AO_fetch_and_add_full(&lock->write_completions, 1);
}
#endif

#else   // not RP_STM
void read_lock(void *lock) { Thread_Stats[STAT_READ]++; }
void read_unlock(void *lock) {}
void write_lock(void *lock) { Thread_Stats[STAT_WRITE]++; }
void write_unlock(void *lock) {}
//void rp_wait_grace_period(void *lock) {}
void rp_free(void *lock, void (*func)(void *ptr), void *ptr) { func(ptr); }
#endif

void DO_STORE(Word *a, Word b)
{
    //printf("STORE(%p, %llX)\n", a, b);
    wlpdstm_write_word(a, b);
}
//**********************************************
#ifdef RP_STM
void rp_wait_grace_period(void *lock)
{
    volatile epoch_list_t *list;
    rp_lock_t *rp_lock = (rp_lock_t *)lock;
    int head;
    AO_t epoch;
    pthread_t self;
    //static int max_head = 0;

    Thread_Stats[STAT_SYNC]++;
    
	// Advance to a new grace-period number, enforce ordering.
    epoch = AO_fetch_and_add_full(&rp_lock->rp_epoch, 2);

    //lock_mb();    // included as part of fetch_and_add, above

    // fetch_and_add returns the PREVIOUS value. We'll re-add one to get
    // the current epoch. We could also do AO_load, but a concurrent 
    // rp_wait_grace_period might move us into a new epoch. Therefore, this is faster
    // and safer.
    epoch += 2;

	/*
	 * Wait until all active threads are out of their RCU read-side
	 * critical sections or have seen the current epoch.
	 */

    self = pthread_self();
    list = rp_lock->epoch_list;
    while (list != NULL)
    {
        while (list->thread_id != self && 
               list->epoch < epoch && 
               (list->epoch & 0x01)) 
        {
            Thread_Stats[STAT_SPINS]++;
            // wait
            lock_mb();
            //assert(rp_lock->block.head < (RCU_MAX_BLOCKS-1));
            //if (max_head < rp_lock->block.head) max_head = rp_lock->block.head;
            //printf("head: %d %d\n", rp_lock->block.head, max_head);
        }
        list = list->next;
    }

    //write_lock(lock);

    //printf("rp_wait_grace_period is freeing memory\n");
    // since a grace period just expired, we might as well clear out the
    // delete buffer
    //head = rp_lock->block.head;
    head = To_Be_Freed.head;
    //printf("RCU is freeing %d blocks\n", head);
    while (head > 0)
    {
        void (*func)(void *ptr);

        head--;
        //func = rp_lock->block.block[head].func;
        //func(rp_lock->block.block[head].block);
        func = To_Be_Freed.block[head].func;
        func(To_Be_Freed.block[head].block);
    }

    //rp_lock->block.head = 0;
    To_Be_Freed.head = 0;

    //write_unlock(lock);
}

void rp_free(void *lock, void (*func)(void *ptr), void *ptr)
{
    //rp_lock_t *rp_lock = (rp_lock_t *)lock;
    int head;

    func = rbnode_free;

    assert(ptr != NULL);

    //write_lock(lock);
    //if (rp_lock->block.head >= BLOCKS_FOR_FREE) 
    if (To_Be_Freed.head >= BLOCKS_FOR_FREE) 
    {
        Thread_Stats[STAT_FREE_SYNC]++;
        //write_unlock(lock);
        rp_wait_grace_period(lock);
        //write_lock(lock);
    }

    Thread_Stats[STAT_FREE]++;
    head = To_Be_Freed.head;
    To_Be_Freed.block[head].block = ptr;
    To_Be_Freed.block[head].func = func;
    head++;
    To_Be_Freed.head = head;

    //write_unlock(lock);
}

//*****************************************************
#ifdef REMOVE
int rp_poll(void *lock)
{
    rp_lock_t *rp_lock = (rp_lock_t *)lock;

    if (rp_lock->block.head > BLOCKS_FOR_FREE) 
    {
        rp_wait_grace_period(lock);
        return 1;
    }

    return 0;
}
#endif
#endif

