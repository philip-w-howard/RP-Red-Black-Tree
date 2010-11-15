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
MULTIFLAGS = -DMULTIWRITERS
AVLFLAGS = $(RCUFLAGS) $(MULTIFLAGS) -DRCU_USE_MUTEX $(FGFLAGS)
STMFLAGS = -DSTM -I/u/pwh/swissTM/swissTM_word/include # -DRP_STM
CFLAGS = -Wall -I/u/pwh/local/include $(ARCHFLAGS) -O3 # -pg 
LFLAGS = -lrt $(CFLAGS)
STM_LFLAGS = -L/u/pwh/swissTM/swissTM_word/lib -lwlpdstm

CC = gcc

TARGETS = rb_rwl_write rb_rwl_read rb_rcu rb_lock rb_nolock ll_rwlr rb_stm parse # ngp rbl_rcu ccavl rpavl rwlravl rwlwavl lockavl nolockavl fgl rcumulti rcutest rb_fg # rb_urcu urcutest 
all: $(TARGETS)

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

urcu.o: urcu.c 
	$(CC) -c urcu.c $(CFLAGS) $(URCUFLAGS)

rpavl: rbmain.c rpavl.c avl.h rcu.c rbtest.o
	$(CC) -c rbnode.c $(CFLAGS) $(RCUFLAGS)
	$(CC) -c rcu.c $(CFLAGS) $(RCUFLAGS)
	$(CC) -c rpavl.c $(CFLAGS) $(RCUFLAGS)
	$(CC) -o rpavl $(LFLAGS) rbmain.c rbtest.o rbnode.o rpavl.o rcu.o $(RCUFLAGS)

stmtest: rbmain.c stmtest.c stm.c
	$(CC) -c stm.c $(CFLAGS) $(STMFLAGS)
	$(CC) -c stmtest.c $(CFLAGS) $(STMFLAGS)
	$(CC) -o stmtest $(LFLAGS) rbmain.c stmtest.o stm.o $(STMFLAGS) $(STM_LFLAGS) 

rb_stm: rbmain.c stm_rbtree.c rbtest.c rbnode.c stm.c rbtree.c 
	$(CC) -c stm.c $(CFLAGS) $(STMFLAGS)
	$(CC) -c rbnode.c $(CFLAGS) $(STMFLAGS)
	$(CC) -c stm_rbtree.c $(CFLAGS) $(STMFLAGS)
	$(CC) -c rbtest.c $(CFLAGS) $(STMFLAGS)
	$(CC) -o rb_stm $(LFLAGS) rbmain.c rbtest.o rbnode.o stm_rbtree.o stm.o $(STMFLAGS) $(STM_LFLAGS) 

rwlravl: rbmain.c rpavl.c avl.h rwl_read.o rbtest.o
	$(CC) -c rbnode.c $(CFLAGS) 
	$(CC) -c rpavl.c $(CFLAGS)
	$(CC) -o rwlravl $(LFLAGS) rbmain.c rbtest.o rbnode.o rpavl.o rwl_read.o

lockavl: rbmain.c rpavl.c avl.h lock.o rbtest.o
	$(CC) -c rbnode.c $(CFLAGS) 
	$(CC) -c rpavl.c $(CFLAGS)
	$(CC) -o lockavl $(LFLAGS) rbmain.c rbtest.o rbnode.o rpavl.o lock.o

nolockavl: rbmain.c rpavl.c avl.h nolock.o rbtest.o
	$(CC) -c rbnode.c $(CFLAGS) 
	$(CC) -c rpavl.c $(CFLAGS)
	$(CC) -o nolockavl $(LFLAGS) rbmain.c rbtest.o rbnode.o rpavl.o nolock.o

rwlwavl: rbmain.c rpavl.c avl.h rwl_write.o rbtest.o
	$(CC) -c rbnode.c $(CFLAGS) 
	$(CC) -c rpavl.c $(CFLAGS)
	$(CC) -o rwlwavl $(LFLAGS) rbmain.c rbtest.o rbnode.o rpavl.o rwl_write.o

ccavl: rbmain.c ccavl.c avl.h rcu.c rbtest.o
	$(CC) -c rbnode.c $(CFLAGS) $(AVLFLAGS)
	$(CC) -c rcu.c $(CFLAGS) $(AVLFLAGS)
	$(CC) -c ccavl.c $(CFLAGS) $(AVLFLAGS)
	$(CC) -o ccavl $(LFLAGS) rbmain.c rbtest.o rbnode.o ccavl.o rcu.o $(AVLFLAGS)

fgl: rbmain.c rbnode.c rblfgtree.c rcu.c rbtest.o
	$(CC) -c rbnode.c $(CFLAGS) $(FGFLAGS)
	$(CC) -c rblfgtree.c $(CFLAGS) $(FGFLAGS)
	$(CC) -c rcu.c $(CFLAGS) $(RCUFLAGS) $(FGFLAGS)
	$(CC) -o fgl $(LFLAGS) $(FGFLAGS) rbmain.c rbtest.o rblfgtree.o rbnode.o rcu.o 

rb_fg: rbmain.c rbnode_fg.c rbtree_fg.c rwl_write.o rbtest.o
	$(CC) -c rbnode_fg.c $(CFLAGS) 
	$(CC) -c rbtree_fg.c $(CFLAGS) 
	$(CC) -o rb_fg $(LFLAGS) rbmain.c rbtest.o rbnode_fg.o rbtree_fg.o rwl_write.o 

rb_rwl_write: rbmain.c rbnode.c rbtree.c rwl_write.o rbtest.o
	$(CC) -c rbnode.c $(CFLAGS) 
	$(CC) -c rbtree.c $(CFLAGS) 
	$(CC) -o rb_rwl_write $(LFLAGS) rbmain.c $(objects) rwl_write.o 

rb_rwl_read: rbmain.c rbnode.c rbtree.c rwl_read.o rbtest.o
	$(CC) -c rbnode.c $(CFLAGS) 
	$(CC) -c rbtree.c $(CFLAGS) 
	$(CC) -o rb_rwl_read $(LFLAGS) rbmain.c $(objects) rwl_read.o

ll_rwlr: rbmain.c lltest.o rwl_read.o
	$(CC) -o ll_rwlr $(LFLAGS) rbmain.c rwl_read.o lltest.o

rbl_rcu: rbmain.c rbnode.c rbltree.c rcu.c rbtest.o
	$(CC) -c rbnode.c $(CFLAGS) $(MULTIFLAGS)
	$(CC) -c rcu.c $(CFLAGS) $(RCUFLAGS) $(MULTIFLAGS)
	$(CC) -c rbltree.c $(CFLAGS) $(RCUFLAGS) $(MULTIFLAGS)
	$(CC) -o rbl_rcu $(LFLAGS) rbmain.c rbtest.o $(RCUFLAGS) rbnode.o rbltree.o rcu.o $(MULTIFLAGS)

ngp: rbmain.c rbnode.c rbtree.c rcu.c rbtest.o
	$(CC) -c rbnode.c $(CFLAGS) 
	$(CC) -c rcu.c $(CFLAGS) $(RCUFLAGS) 
	$(CC) -c rbtree.c $(CFLAGS) -DNO_GRACE_PERIOD
	$(CC) -o ngp $(LFLAGS) rbmain.c rbtest.o $(RCUFLAGS) rbnode.o rbtree.o rcu.o

rcumulti: rbmain.c rbnode.c rbtree.c rcu.c rbtest.o
	$(CC) -c rbnode.c $(CFLAGS) $(RCUFLAGS) $(MULTIFLAGS)
	$(CC) -c rcu.c $(CFLAGS) $(RCUFLAGS) $(MULTIFLAGS)
	$(CC) -c rbtree.c $(CFLAGS) $(RCUFLAGS) $(MULTIFLAGS)
	$(CC) -o rcumulti $(LFLAGS) rbmain.c $(RCUFLAGS) $(MULTIFLAGS) $(objects) rcu.o

rb_rcu: rbmain.c rbnode.c rbtree.c rcu.c rbtest.o
	$(CC) -c rbnode.c $(CFLAGS) 
	$(CC) -c rcu.c $(CFLAGS) -DRCU 
	$(CC) -c rbtree.c $(CFLAGS) -DRCU 
	$(CC) -o rb_rcu $(LFLAGS) rbmain.c $(RCUFLAGS) $(objects) rcu.o

rcutest: rcutest.c rcu.o rbtest.o
	$(CC) -o rcutest $(CFLAGS) rcutest.c -DRCU rcu.o

rb_urcu: rbmain.c rbnode.c rbtree.c urcu.o rbtest.o
	$(CC) -c rbnode.c $(CFLAGS) 
	$(CC) -c rbtree.c $(CFLAGS) $(URCUFLAGS)
	$(CC) -o rb_urcu $(LFLAGS) rbmain.c $(objects) urcu.o -lurcu -lurcu-defer 

rb_lock: rbmain.c rbnode.c rbtree.c lock.o rbtest.o
	$(CC) -c rbnode.c $(CFLAGS) 
	$(CC) -c rbtree.c $(CFLAGS) 
	$(CC) -o rb_lock $(LFLAGS) rbmain.c $(objects) lock.o

rb_nolock: rbmain.c rbnode.c rbtree.c nolock.o rbtest.o
	$(CC) -c rbnode.c $(CFLAGS) 
	$(CC) -c rbtree.c $(CFLAGS) 
	$(CC) -o rb_nolock $(LFLAGS) rbmain.c $(objects) nolock.o

urcutest: urcutest.c rbtest.o
	$(CC) -o urcutest $(LFLAGS) $(URCUFLAGS) urcutest.c -lurcu -lurcu-defer 

parse: parse.c 
	$(CC) -g -o parse -Wall parse.c
