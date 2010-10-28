#ifndef MY_STM_H
#define MY_STM_H

#include <stm.h>

void DO_STORE(Word *a, Word b);

#define RB_START_RO_TX()   BEGIN_TRANSACTION
#define RB_START_TX()      BEGIN_TRANSACTION
#define RB_COMMIT()        END_TRANSACTION
#define LOAD(a)            ((typeof((a)) )wlpdstm_read_word((Word *)&(a)))
//#define STORE(a,b)         DO_STORE((Word *)&(a), (Word)(b))
#define STORE(a,b)         wlpdstm_write_word((Word *)&(a), (Word)(b))

#endif

