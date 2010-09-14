
ARCHFLAGS = -pthread -g
#ARCHFLAGS = -Wa,-xarch=v8plus

URCUFLAGS = -L/u/pwh/local/lib -DURCU -D_LGPL_SOURCE
FGFLAGS = -DFG_LOCK
RCUFLAGS = -DRCU
MULTIFLAGS = -DMULTIWRITERS
AVLFLAGS = $(RCUFLAGS) $(MULTIFLAGS) -DRCU_USE_MUTEX $(FGFLAGS)
CFLAGS = -Wall -I/u/pwh/local/include $(ARCHFLAGS) -O1 # -pg 
LFLAGS = -lrt $(CFLAGS)

CC = gcc

TARGETS = rcumulti fgl rb_rwl_write rb_rwl_read rb_rcu rbl_rcu ngp rb_lock rb_nolock ccavl rpavl rwlravl rwlwavl lockavl nolockavl parse # rcutest rb_fg # rb_urcu urcutest 
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

rpavl: rbmain.c rpavl.c avl.h rcu.c
	$(CC) -c rbnode.c $(CFLAGS) $(RCUFLAGS)
	$(CC) -c rcu.c $(CFLAGS) $(RCUFLAGS)
	$(CC) -c rpavl.c $(CFLAGS) $(RCUFLAGS)
	$(CC) -o rpavl $(LFLAGS) rbmain.c rbnode.o rpavl.o rcu.o $(RCUFLAGS)

rwlravl: rbmain.c rpavl.c avl.h rwl_read.o
	$(CC) -c rbnode.c $(CFLAGS) 
	$(CC) -c rpavl.c $(CFLAGS)
	$(CC) -o rwlravl $(LFLAGS) rbmain.c rbnode.o rpavl.o rwl_read.o

lockavl: rbmain.c rpavl.c avl.h lock.o
	$(CC) -c rbnode.c $(CFLAGS) 
	$(CC) -c rpavl.c $(CFLAGS)
	$(CC) -o lockavl $(LFLAGS) rbmain.c rbnode.o rpavl.o lock.o

nolockavl: rbmain.c rpavl.c avl.h nolock.o
	$(CC) -c rbnode.c $(CFLAGS) 
	$(CC) -c rpavl.c $(CFLAGS)
	$(CC) -o nolockavl $(LFLAGS) rbmain.c rbnode.o rpavl.o nolock.o

rwlwavl: rbmain.c rpavl.c avl.h rwl_write.o
	$(CC) -c rbnode.c $(CFLAGS) 
	$(CC) -c rpavl.c $(CFLAGS)
	$(CC) -o rwlwavl $(LFLAGS) rbmain.c rbnode.o rpavl.o rwl_write.o

ccavl: rbmain.c ccavl.c avl.h rcu.c
	$(CC) -c rbnode.c $(CFLAGS) $(AVLFLAGS)
	$(CC) -c rcu.c $(CFLAGS) $(AVLFLAGS)
	$(CC) -c ccavl.c $(CFLAGS) $(AVLFLAGS)
	$(CC) -o ccavl $(LFLAGS) rbmain.c rbnode.o ccavl.o rcu.o $(AVLFLAGS)

fgl: rbmain.c rbnode.c rblfgtree.c rcu.c
	$(CC) -c rbnode.c $(CFLAGS) $(FGFLAGS)
	$(CC) -c rblfgtree.c $(CFLAGS) $(FGFLAGS)
	$(CC) -c rcu.c $(CFLAGS) $(RCUFLAGS) $(FGFLAGS)
	$(CC) -o fgl $(LFLAGS) $(FGFLAGS) rbmain.c rblfgtree.o rbnode.o rcu.o 

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
	$(CC) -c rbnode.c $(CFLAGS) $(MULTIFLAGS)
	$(CC) -c rcu.c $(CFLAGS) $(RCUFLAGS) $(MULTIFLAGS)
	$(CC) -c rbltree.c $(CFLAGS) $(RCUFLAGS) $(MULTIFLAGS)
	$(CC) -o rbl_rcu $(LFLAGS) rbmain.c $(RCUFLAGS) rbnode.o rbltree.o rcu.o $(MULTIFLAGS)

ngp: rbmain.c rbnode.c rbtree.c rcu.c
	$(CC) -c rbnode.c $(CFLAGS) 
	$(CC) -c rcu.c $(CFLAGS) $(RCUFLAGS) 
	$(CC) -c rbtree.c $(CFLAGS) -DNO_GRACE_PERIOD
	$(CC) -o ngp $(LFLAGS) rbmain.c $(RCUFLAGS) rbnode.o rbtree.o rcu.o

rcumulti: rbmain.c rbnode.c rbtree.c rcu.c
	$(CC) -c rbnode.c $(CFLAGS) $(RCUFLAGS) $(MULTIFLAGS)
	$(CC) -c rcu.c $(CFLAGS) $(RCUFLAGS) $(MULTIFLAGS)
	$(CC) -c rbtree.c $(CFLAGS) $(RCUFLAGS) $(MULTIFLAGS)
	$(CC) -o rcumulti $(LFLAGS) rbmain.c $(RCUFLAGS) $(MULTIFLAGS) $(objects) rcu.o

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
