UCFLAGS = -Wall -pthread -g -O1 -I/u/pwh/local/include -L/u/pwh/local/lib -DURCU -D_LGPL_SOURCE -DRCU #-pg 
CFLAGS = -Wall -pthread -g -O1 -I/u/pwh/local/include # -pg 

all: rb_rwl_write rb_rwl_read rb_rcu rb_lock rb_nolock rb_urcu parse urcutest

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
	
rcu.o: rcu.c 
	cc -c rcu.c $(CFLAGS) -DRCU

urcu.o: urcu.c 
	cc -c urcu.c $(UCFLAGS) 

rb_rwl_write: rbmain.c $(all_objs) 
	cc -c rbnode.c $(CFLAGS) 
	cc -c rbtree.c $(CFLAGS) 
	cc -o rb_rwl_write $(CFLAGS) rbmain.c $(objects) rwl_write.o

rb_rwl_read: rbmain.c $(all_objs) 
	cc -c rbnode.c $(CFLAGS) 
	cc -c rbtree.c $(CFLAGS) 
	cc -o rb_rwl_read $(CFLAGS) rbmain.c $(objects) rwl_read.o

rb_rcu: rbmain.c $(all_objs) 
	cc -c rbnode.c $(CFLAGS) 
	cc -c rbtree.c $(CFLAGS) -DRCU 
	cc -o rb_rcu $(CFLAGS) rbmain.c -DRCU $(objects) rcu.o

rb_urcu: rbmain.c $(all_objs) 
	cc -c rbnode.c $(CFLAGS) 
	cc -c rbtree.c $(UCFLAGS)
	cc -o rb_urcu $(UCFLAGS) rbmain.c $(objects) urcu.o -lurcu -lurcu-defer 

rb_lock: rbmain.c $(all_objs) 
	cc -c rbnode.c $(CFLAGS) 
	cc -c rbtree.c $(CFLAGS) 
	cc -o rb_lock $(CFLAGS) rbmain.c $(objects) lock.o

rb_nolock: rbmain.c $(all_objs) 
	cc -c rbnode.c $(CFLAGS) 
	cc -c rbtree.c $(CFLAGS) 
	cc -o rb_nolock $(CFLAGS) rbmain.c $(objects) nolock.o

urcutest: urcutest.c
	cc -o urcutest $(UCFLAGS) urcutest.c -lurcu -lurcu-defer 

parse: parse.c Makefile
	cc -g -o parse -Wall parse.c
