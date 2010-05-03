
ifndef __sparc__
ARCHFLAGS = -pthread -g
else
ARCHFLAGS = -Wa,-xarch=v8plus
endif

URCUFLAGS = -L/u/pwh/local/lib -DURCU -D_LGPL_SOURCE
RCUFLAGS = -DRCU
AVLFLAGS = -DRCU -DMULTIWRITERS -DRCU_USE_MUTEX
CFLAGS = -Wall -I/u/pwh/local/include $(ARCHFLAGS) # -O1 -pg 
LFLAGS = -lrt $(CFLAGS)

CC = gcc

TARGETS = rb_rwl_write rb_rwl_read rb_rcu rbl_rcu ngp rb_lock rb_nolock ccavl parse # rcutest rb_fg # rb_urcu urcutest 
all: $(TARGETS)

stuff: aotest

clean:
	rm -f *.o
	rm -f $(TARGETS)

#all: gettimestamp gettimestampmp rcu rcu64 rcu_lock rcu_lock_percpu rcu_nest rcu_nest_qs rcu_qs rcu_ts

objects = rbnode.o \
		  rbtree.o \

all_objs = $(objects) \
		  lock.o \
		  nolock.o \
		  rwl_read.o \
		  rwl_write.o \
		  rcu.o \
		  urcu.o \
	
aotest: aotest.c atomic_ops.h
	$(CC) -o aotest $(CFLAGS) aotest.c 

rcu.o: rcu.c 
	$(CC) -c rcu.c $(CFLAGS) -DRCU

urcu.o: urcu.c 
	$(CC) -c urcu.c $(CFLAGS) $(URCUFLAGS)

ccavl: avlmain.c ccavl.c avl.h rcu.c
	$(CC) -c rcu.c $(CFLAGS) $(AVLFLAGS)
	$(CC) -c ccavl.c $(CFLAGS) $(AVLFLAGS)
	$(CC) -o ccavl $(LFLAGS) avlmain.c ccavl.o rcu.o $(AVLFLAGS)

rb_fg: rbmain.c rbnode_fg.c rbtree_fg.c rwl_write.o
	$(CC) -c rbnode_fg.c $(CFLAGS) 
	$(CC) -c rbtree_fg.c $(CFLAGS) 
	$(CC) -o rb_fg $(LFLAGS) rbmain.c rbnode_fg.o rbtree_fg.o rwl_write.o 

rb_rwl_write: rbmain.c rbnode.c rbtree.c rwl_write.o
	$(CC) -c rbnode.c $(CFLAGS) 
	$(CC) -c rbtree.c $(CFLAGS) 
	$(CC) -o rb_rwl_write $(LFLAGS) rbmain.c $(objects) rwl_write.o 

rb_rwl_read: rbmain.c rbnode.c rbtree.c rwl_read.o
	$(CC) -c rbnode.c $(CFLAGS) 
	$(CC) -c rbtree.c $(CFLAGS) 
	$(CC) -o rb_rwl_read $(LFLAGS) rbmain.c $(objects) rwl_read.o

rbl_rcu: rbmain.c rbnode.c rbltree.c rcu.c
	$(CC) -c rbnode.c $(CFLAGS) 
	$(CC) -c rcu.c $(CFLAGS) $(RCUFLAGS) 
	$(CC) -c rbltree.c $(CFLAGS) $(RCUFLAGS) 
	$(CC) -o rbl_rcu $(LFLAGS) rbmain.c $(RCUFLAGS) rbnode.o rbltree.o rcu.o

ngp: rbmain.c rbnode.c rbtree.c rcu.c
	$(CC) -c rbnode.c $(CFLAGS) 
	$(CC) -c rcu.c $(CFLAGS) $(RCUFLAGS) 
	$(CC) -c rbtree.c $(CFLAGS) -DNO_GRACE_PERIOD
	$(CC) -o ngp $(LFLAGS) rbmain.c $(RCUFLAGS) rbnode.o rbtree.o rcu.o

rb_rcu: rbmain.c rbnode.c rbtree.c rcu.c
	$(CC) -c rbnode.c $(CFLAGS) 
	$(CC) -c rcu.c $(CFLAGS) -DRCU 
	$(CC) -c rbtree.c $(CFLAGS) -DRCU 
	$(CC) -o rb_rcu $(LFLAGS) rbmain.c $(RCUFLAGS) $(objects) rcu.o

rcutest: rcutest.c rcu.o
	$(CC) -o rcutest $(CFLAGS) rcutest.c -DRCU rcu.o

rb_urcu: rbmain.c rbnode.c rbtree.c urcu.o
	$(CC) -c rbnode.c $(CFLAGS) 
	$(CC) -c rbtree.c $(CFLAGS) $(URCUFLAGS)
	$(CC) -o rb_urcu $(LFLAGS) rbmain.c $(objects) urcu.o -lurcu -lurcu-defer 

rb_lock: rbmain.c rbnode.c rbtree.c lock.o
	$(CC) -c rbnode.c $(CFLAGS) 
	$(CC) -c rbtree.c $(CFLAGS) 
	$(CC) -o rb_lock $(LFLAGS) rbmain.c $(objects) lock.o

rb_nolock: rbmain.c rbnode.c rbtree.c nolock.o
	$(CC) -c rbnode.c $(CFLAGS) 
	$(CC) -c rbtree.c $(CFLAGS) 
	$(CC) -o rb_nolock $(LFLAGS) rbmain.c $(objects) nolock.o

urcutest: urcutest.c
	$(CC) -o urcutest $(LFLAGS) $(URCUFLAGS) urcutest.c -lurcu -lurcu-defer 

parse: parse.c 
	$(CC) -g -o parse -Wall parse.c
