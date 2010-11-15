#ifndef MY_STM_H
#define MY_STM_H

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
#ifdef RP_STM
#define RP_FREE(a,b,c)      wlpdstm_rp_free((a), (c))
#else
#define RP_FREE(a,b,c)      b(c)
#endif
#endif

