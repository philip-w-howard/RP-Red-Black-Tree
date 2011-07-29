ifeq ($(ARCH_NAME), )
	ARCH_NAME = $(shell uname -m)
endif

ifeq ($(ARCH_NAME), x86_64)
    ARCHFLAGS = -pthread -g
endif

ifeq ($(ARCH_NAME), sun4v)
    ARCHFLAGS = -Wa,-xarch=v8plus
endif

URCUFLAGS = -L/u/pwh/local/lib -DURCU -D_LGPL_SOURCE
FGFLAGS = -DFG_LOCK
RCUFLAGS = -DRCU
NGPFLAGS = $(URCUFLAGS) -DNO_GRACE_PERIOD
MULTIFLAGS = -DMULTIWRITERS
AVLFLAGS = $(MULTIFLAGS) $(FGFLAGS)
STMFLAGS = -DSTM -I/u/pwh/swissTM/swissTM_word/include # -DRPSTM_STATS # -DRP_FINDS -DRP_UPDATE
RPSTMFLAGS = -DRP_STM
CFLAGS = -Wall -I/u/pwh/local/include $(ARCHFLAGS) -O0 # -DDEBUG # -pg 
URCULFLAGS = -lurcu -lwfqueue 
LFLAGS = -lrt $(CFLAGS)
STM_LIB = -lwlpdstm
STMRP_LIB = -lwlpdstm_rp
#STMRP_LFLAGS = stmlib.o
STM_LFLAGS = -L/u/pwh/swissTM/swissTM_word/lib 

CC = gcc

TARGETS = rb_prw rb_rwl_write rb_rwl_read rb_rcu rb_lrcu rb_lock rb_nolock rb_stm rb_rpstm rb_rpstm_rp ngp ccavl rpavl rb_urcu parse csvparse # stmbad # ccavl rpavl rwlravl rwlwavl lockavl nolockavl rcutest ll_rwlr # rb_urcu urcutest 

LL_TARGETS = ll_nolock ll_rp ll_rwlr ll_rwlw ll_lrp ll_prw

all: $(TARGETS)

ll: $(LL_TARGETS)

stuff: aotest

clean:
	rm -f *.o
	rm -f $(TARGETS)

#all: gettimestamp gettimestampmp rcu rcu64 rcu_lock rcu_lock_percpu rcu_nest rcu_nest_qs rcu_qs rcu_ts

objects = rbnode.o \
		  rbtree.o \
		  rbtest.o \

all_objs = $(objects) \
		  lock.o \
		  nolock.o \
		  prwlock.o \
		  rwl_read.o \
		  rwl_write.o \
		  rcu.o \
		  urcu.o \
	
aotest: aotest.c atomic_ops.h
	$(CC) -o aotest $(CFLAGS) aotest.c 

rbtest.o: rbtest.c tests.h
	$(CC) -c rbtest.c $(CFLAGS)

rcu.o: rcu.c 
	$(CC) -c rcu.c $(CFLAGS) -DRCU

rpavl: rbmain.c rpavl.c avl.h rcu.c rbtest.o
	$(CC) -c rbtest.c $(CFLAGS) $(RCUFLAGS)
	$(CC) -c rbnode.c $(CFLAGS) $(RCUFLAGS)
	$(CC) -c rcu.c $(CFLAGS) $(RCUFLAGS)
	$(CC) -c rpavl.c $(CFLAGS) $(RCUFLAGS)
	$(CC) -o rpavl $(LFLAGS) rbmain.c rbtest.o rbnode.o rpavl.o rcu.o $(RCUFLAGS) -DALG_NAME=\"rpavl\" 
stmtest: rbmain.c stmtest.c stm.c
	$(CC) -c stm.c $(CFLAGS) $(STMFLAGS)
	$(CC) -c stmtest.c $(CFLAGS) $(STMFLAGS)
	$(CC) -o stmtest $(LFLAGS) rbmain.c stmtest.o stm.o $(STMFLAGS) $(STM_LFLAGS) $(STM_LIB)

rcu.s: 
	$(CC) -S rcu.c $(CFLAGS) $(RCUFLAGS)

rbtree.s: 
	$(CC) -S rbtree.c $(CFLAGS) $(RCUFLAGS)

stm.s: 
	$(CC) -S stm.c $(CFLAGS) $(STMFLAGS)

stm_rbtree.s: 
	$(CC) -S stm_rbtree.c $(CFLAGS) $(STMFLAGS)

rb_stm: rbmain.c stm_rbtree.c rbtest.c rbnode.c stm.c rbtree.c Makefile
	$(CC) -c stmlib.c $(CFLAGS) $(STMFLAGS)
	$(CC) -c stm.c $(CFLAGS) $(STMFLAGS)
	$(CC) -c rbnode.c $(CFLAGS) $(STMFLAGS)
	$(CC) -c stm_rbtree.c $(CFLAGS) $(STMFLAGS)
	$(CC) -c rbtest.c $(CFLAGS) $(STMFLAGS)
	$(CC) -o rb_stm $(LFLAGS) rbmain.c rbnode.o stm_rbtree.o rbtest.o stm.o $(STMFLAGS) $(STM_LFLAGS) $(STM_LIB)

rb_rpstm: rbmain.c stm_rbtree.c rbtest.c rbnode.c stm.c rbtree.c Makefile
	$(CC) -c stmlib.c $(CFLAGS) $(STMFLAGS)
	$(CC) -c stm.c $(CFLAGS) $(STMFLAGS)
	$(CC) -c rbnode.c $(CFLAGS) $(STMFLAGS)
	$(CC) -c stm_rbtree.c $(CFLAGS) $(STMFLAGS)
	$(CC) -c rbtest.c $(CFLAGS) $(STMFLAGS)
	$(CC) -o rb_rpstm $(LFLAGS) rbmain.c rbnode.o stm_rbtree.o rbtest.o stm.o $(STMFLAGS) $(STM_LFLAGS)  $(RPSTMFLAGS) $(STMRP_LIB)

rb_rpstm_rp: rbmain.c stm_rbtree.c rbtest.c rbnode.c stm.c rbtree.c Makefile
	$(CC) -c stmlib.c $(CFLAGS) $(STMFLAGS) $(RPSTMFLAGS)
	$(CC) -c stm.c $(CFLAGS) $(STMFLAGS) $(RPSTMFLAGS)
	$(CC) -c rbnode.c $(CFLAGS) $(STMFLAGS) $(RPSTMFLAGS)
	$(CC) -c stm_rbtree.c $(CFLAGS) $(STMFLAGS) $(RPSTMFLAGS)
	$(CC) -c rbtest.c $(CFLAGS) $(STMFLAGS) $(RPSTMFLAGS)
	$(CC) -o rb_rpstm_rp $(LFLAGS) rbmain.c rbnode.o stm_rbtree.o rbtest.o stm.o $(STMFLAGS) $(STM_LFLAGS)  $(RPSTMFLAGS) $(STMRP_LIB)

stmbad: stmbad.c stm.c rbnode.c Makefile
	$(CC) -c stm.c $(CFLAGS) $(STMFLAGS)
	$(CC) -c rbnode.c $(CFLAGS) $(STMFLAGS)
	$(CC) -o stmbad $(LFLAGS) stmbad.c stm.o rbnode.o $(STMFLAGS) $(STM_LFLAGS) 

rwlravl: rbmain.c rpavl.c avl.h rwl_read.o rbtest.c
	$(CC) -c rbtest.c $(CFLAGS)
	$(CC) -c rbnode.c $(CFLAGS) 
	$(CC) -c rpavl.c $(CFLAGS)
	$(CC) -o rwlravl $(LFLAGS) rbmain.c rbtest.o rbnode.o rpavl.o rwl_read.o

lockavl: rbmain.c rpavl.c avl.h lock.o rbtest.c
	$(CC) -c rbtest.c $(CFLAGS)
	$(CC) -c rbnode.c $(CFLAGS) 
	$(CC) -c rpavl.c $(CFLAGS)
	$(CC) -o lockavl $(LFLAGS) rbmain.c rbtest.o rbnode.o rpavl.o lock.o

nolockavl: rbmain.c rpavl.c avl.h nolock.o rbtest.c
	$(CC) -c rbtest.c $(CFLAGS)
	$(CC) -c rbnode.c $(CFLAGS) 
	$(CC) -c rpavl.c $(CFLAGS)
	$(CC) -o nolockavl $(LFLAGS) rbmain.c rbtest.o rbnode.o rpavl.o nolock.o

rwlwavl: rbmain.c rpavl.c avl.h rwl_write.o rbtest.c
	$(CC) -c rbtest.c $(CFLAGS)
	$(CC) -c rbnode.c $(CFLAGS) 
	$(CC) -c rpavl.c $(CFLAGS)
	$(CC) -o rwlwavl $(LFLAGS) rbmain.c rbtest.o rbnode.o rpavl.o rwl_write.o

ccavl: rbmain.c ccavl.c avl.h rbtest.c rwl_write.o Makefile
	$(CC) -c rbtest.c $(CFLAGS) $(AVLFLAGS)
	$(CC) -c rbnode.c $(CFLAGS) $(AVLFLAGS)
	$(CC) -c ccavl.c $(CFLAGS) $(AVLFLAGS)
	$(CC) -o ccavl $(LFLAGS) rbmain.c rbtest.o rbnode.o ccavl.o rwl_write.o $(AVLFLAGS) -DALG_NAME=\"ccavl\"

rb_rwl_write: rbmain.c rbnode.c rbtree.c rwl_write.o rbtest.c
	$(CC) -c rbtest.c $(CFLAGS)
	$(CC) -c rbnode.c $(CFLAGS) 
	$(CC) -c rbtree.c $(CFLAGS) 
	$(CC) -o rb_rwl_write $(LFLAGS) rbmain.c $(objects) rwl_write.o 

rb_rwl_read: rbmain.c rbnode.c rbtree.c rwl_read.o rbtest.c
	$(CC) -c rbtest.c $(CFLAGS)
	$(CC) -c rbnode.c $(CFLAGS) 
	$(CC) -c rbtree.c $(CFLAGS) 
	$(CC) -o rb_rwl_read $(LFLAGS) rbmain.c $(objects) rwl_read.o

ll_rp: rbmain.c lltest.c rcu.c
	$(CC) -c lltest.c $(CFLAGS) $(RCUFLAGS) 
	$(CC) -c rcu.c $(CFLAGS) $(RCUFLAGS) 
	$(CC) -o ll_rp  $(LFLAGS) $(RCUFLAGS) rbmain.c lltest.o rcu.o

ll_lrp: rbmain.c lltest.c rcu.c
	$(CC) -c lltest.c $(CFLAGS) $(RCUFLAGS) 
	$(CC) -c rcu.c $(CFLAGS) $(RCUFLAGS) -DLINEARIZABLE
	$(CC) -o ll_lrp  $(LFLAGS) $(RCUFLAGS) rbmain.c lltest.o rcu.o

ll_nolock: rbmain.c lltest.c nolock.c
	$(CC) -c lltest.c $(CFLAGS)
	$(CC) -c nolock.c $(CFLAGS) 
	$(CC) -o ll_nolock  $(LFLAGS) rbmain.c lltest.o nolock.o

ll_rwlr: rbmain.c lltest.c rwl_read.c
	$(CC) -c lltest.c $(CFLAGS)
	$(CC) -c rwl_read.c $(CFLAGS) 
	$(CC) -o ll_rwlr  $(LFLAGS) rbmain.c lltest.o rwl_read.o

ll_rwlw: rbmain.c lltest.c rwl_write.c
	$(CC) -c lltest.c $(CFLAGS)
	$(CC) -c rwl_write.c $(CFLAGS) 
	$(CC) -o ll_rwlw  $(LFLAGS) rbmain.c lltest.o rwl_write.o

ll_prw: rbmain.c rbnode.c rbtree.c prwlock.o lltest.c
	$(CC) -c lltest.c $(CFLAGS)
	$(CC) -c rwl_read.c $(CFLAGS) 
	$(CC) -o ll_prw  $(LFLAGS) rbmain.c lltest.o prwlock.o

ngp: rbmain.c rbnode.c rbtree.c urcu.c rbtest.c 
	$(CC) -c rbtest.c $(CFLAGS) $(NGPFLAGS)
	$(CC) -c rbnode.c $(CFLAGS)  $(NGPFLAGS)
	$(CC) -c urcu.c $(CFLAGS)  $(NGPFLAGS)
	$(CC) -c rbtree.c $(CFLAGS)  $(NGPFLAGS)
	$(CC) -o ngp $(LFLAGS) $(NGPFLAGS) rbmain.c rbtest.o rbnode.o rbtree.o urcu.o -DALG_NAME=\"ngp\" $(URCULFLAGS)

rb_rcu: rbmain.c rbnode.c rbtree.c rcu.c rbtest.c
	$(CC) -c rbnode.c $(CFLAGS) $(RCUFLAGS)
	$(CC) -c rcu.c $(CFLAGS) $(RCUFLAGS)
	$(CC) -c rbtree.c $(CFLAGS) $(RCUFLAGS)
	$(CC) -c rbtest.c $(CFLAGS) $(RCUFLAGS)
	$(CC) -o rb_rcu $(LFLAGS) $(RCUFLAGS) rbmain.c rbnode.o rbtree.o rbtest.o rcu.o

rcutest: rcutest.c rcu.o rbtest.o
rb_lrcu: rbmain.c rbnode.c rbtree.c rcu.c rbtest.c
	$(CC) -c rbnode.c $(CFLAGS) $(RCUFLAGS)
	$(CC) -c rcu.c $(CFLAGS) $(RCUFLAGS) -DLINEARIZABLE
	$(CC) -c rbtree.c $(CFLAGS) $(RCUFLAGS)
	$(CC) -c rbtest.c $(CFLAGS) $(RCUFLAGS)
	$(CC) -o rb_lrcu $(LFLAGS) rbmain.c $(RCUFLAGS) rbnode.o rbtree.o rbtest.o rcu.o

rcutest: rcutest.c rcu.o rbtest.o
	$(CC) -o rcutest $(CFLAGS) rcutest.c -DRCU rcu.o

rb_urcu: rbmain.c rbnode.c rbtree.c urcu.c rbtest.c
	$(CC) -c urcu.c   $(CFLAGS) $(URCUFLAGS)
	$(CC) -c rbtest.c $(CFLAGS) $(URCUFLAGS)
	$(CC) -c rbnode.c $(CFLAGS) $(URCUFLAGS) 
	$(CC) -c rbtree.c $(CFLAGS) $(URCUFLAGS)
	$(CC) -o rb_urcu $(LFLAGS)  $(URCUFLAGS) rbmain.c $(objects) urcu.o $(URCULFLAGS)

rb_prw: rbmain.c rbnode.c rbtree.c prwlock.o rbtest.c
	$(CC) -c rbtest.c $(CFLAGS) 
	$(CC) -c rbnode.c $(CFLAGS) 
	$(CC) -c rbtree.c $(CFLAGS) 
	$(CC) -o rb_prw $(LFLAGS) rbmain.c $(objects) prwlock.o

rb_lock: rbmain.c rbnode.c rbtree.c lock.o rbtest.c
	$(CC) -c rbtest.c $(CFLAGS) 
	$(CC) -c rbnode.c $(CFLAGS) 
	$(CC) -c rbtree.c $(CFLAGS) 
	$(CC) -o rb_lock $(LFLAGS) rbmain.c $(objects) lock.o

rb_nolock: rbmain.c rbnode.c rbtree.c nolock.o rbtest.c
	$(CC) -c rbtest.c $(CFLAGS) 
	$(CC) -c rbnode.c $(CFLAGS) 
	$(CC) -c rbtree.c $(CFLAGS) 
	$(CC) -o rb_nolock $(LFLAGS) rbmain.c $(objects) nolock.o

urcutest: urcutest.c rbtest.o
	$(CC) -o urcutest $(LFLAGS) $(URCUFLAGS) urcutest.c -lurcu # -lurcu-defer 

parse: parse.c 
	$(CC) -g -o parse -Wall parse.c
