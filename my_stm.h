#ifndef MY_STM_H
#define MY_STM_H

#include <stm.h>

void DO_STORE(Word *a, Word b);
//void stm_start();
//void stm_write();
//void stm_op_failed();
//void stm_op_grand_failed();

#define RB_START_RO_TX(a)  BEGIN_TRANSACTION

#ifdef RP_STM
//#define RB_START_TX(a)     read_lock((a)); BEGIN_TRANSACTION
#define RB_START_TX(a)     BEGIN_TRANSACTION
//#define RB_COMMIT(a)       read_unlock((a)); END_TRANSACTION
#define RB_COMMIT(a)       END_TRANSACTION

#else
#define RB_START_TX(a)     BEGIN_TRANSACTION
#define RB_COMMIT(a)       END_TRANSACTION
#endif

#define LOAD(a)            ((typeof((a)) )wlpdstm_read_word((Word *)&(a)))
//#define STORE(a,b)         DO_STORE((Word *)&(a), (Word)(b))
#define STORE(a,b)         wlpdstm_write_word((Word *)&(a), (Word)(b))
#ifdef RP_STM
#define STORE_MB(a,b)      wlpdstm_write_word_mb((Word *)&(a), (Word)(b))
#else
#define STORE_MB    STORE
#endif

#define RB_WAIT_GP(a)       wlpdstm_wait_grace_period((a))
#define RP_FREE(a,b,c)      wlpdstm_rp_free((a), (c))
#endif

