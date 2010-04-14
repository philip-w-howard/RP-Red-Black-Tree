UCFLAGS = -Wall -O1 -I/u/pwh/local/include -L/u/pwh/local/lib -DURCU -D_LGPL_SOURCE -DRCU #-pg 
# UCFLAGS = -Wall -pthread -g -O1 -I/u/pwh/local/include -L/u/pwh/local/lib -DURCU -D_LGPL_SOURCE -DRCU #-pg 
#CFLAGS = -Wall -O1 -Wa,-xarch=v8plus -I/u/pwh/local/include # -pg -pthreads -g
CFLAGS = -Wall -pthread -g -I/u/pwh/local/include # -O1 -pg 

CC = gcc

all: rb_rwl_write rb_rwl_read rb_rcu rb_lock rb_nolock avl parse # rcutest rb_fg # rb_urcu urcutest 

stuff: aotest

clean:
	rm *.o

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
	$(CC) -c urcu.c $(UCFLAGS) 

avl: avlmain.c avl.c avl.h rcu.o
	$(CC) -c avl.c $(CFLAGS) 
	$(CC) -o avl $(CFLAGS) avlmain.c avl.o rcu.o

rb_fg: rbmain.c rbnode_fg.c rbtree_fg.c rwl_write.o
	$(CC) -c rbnode_fg.c $(CFLAGS) 
	$(CC) -c rbtree_fg.c $(CFLAGS) 
	$(CC) -o rb_fg $(CFLAGS) rbmain.c rbnode_fg.o rbtree_fg.o rwl_write.o 

rb_rwl_write: rbmain.c rbnode.c rbtree.c rwl_write.o
	$(CC) -c rbnode.c $(CFLAGS) 
	$(CC) -c rbtree.c $(CFLAGS) 
	$(CC) -o rb_rwl_write $(CFLAGS) rbmain.c $(objects) rwl_write.o 

rb_rwl_read: rbmain.c rbnode.c rbtree.c rwl_read.o
	$(CC) -c rbnode.c $(CFLAGS) 
	$(CC) -c rbtree.c $(CFLAGS) 
	$(CC) -o rb_rwl_read $(CFLAGS) rbmain.c $(objects) rwl_read.o

rb_rcu: rbmain.c rbnode.c rbtree.c rcu.o
	$(CC) -c rbnode.c $(CFLAGS) 
	$(CC) -c rbtree.c $(CFLAGS) -DRCU 
	$(CC) -o rb_rcu $(CFLAGS) rbmain.c -DRCU $(objects) rcu.o

rcutest: rcutest.c rcu.o
	$(CC) -o rcutest $(CFLAGS) rcutest.c -DRCU rcu.o

rb_urcu: rbmain.c rbnode.c rbtree.c urcu.o
	$(CC) -c rbnode.c $(CFLAGS) 
	$(CC) -c rbtree.c $(UCFLAGS)
	$(CC) -o rb_urcu $(UCFLAGS) rbmain.c $(objects) urcu.o -lurcu -lurcu-defer 

rb_lock: rbmain.c rbnode.c rbtree.c lock.o
	$(CC) -c rbnode.c $(CFLAGS) 
	$(CC) -c rbtree.c $(CFLAGS) 
	$(CC) -o rb_lock $(CFLAGS) rbmain.c $(objects) lock.o

rb_nolock: rbmain.c rbnode.c rbtree.c nolock.o
	$(CC) -c rbnode.c $(CFLAGS) 
	$(CC) -c rbtree.c $(CFLAGS) 
	$(CC) -o rb_nolock $(CFLAGS) rbmain.c $(objects) nolock.o

urcutest: urcutest.c
	$(CC) -o urcutest $(UCFLAGS) urcutest.c -lurcu -lurcu-defer 

parse: parse.c 
	$(CC) -g -o parse -Wall parse.c
