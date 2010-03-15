CFLAGS = -Wall -pthread -g # -O1 -pg -ltcmalloc

all: rb_rwl_write rb_rwl_read rb_rcu rb_lock rb_nolock parse

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
	
rb_rwl_write: rbmain.c $(all_objs) 
	cc -c rbtree.c $(CFLAGS) 
	cc -o rb_rwl_write $(CFLAGS) rbmain.c $(objects) rwl_write.o

rb_rwl_read: rbmain.c $(all_objs) 
	cc -c rbtree.c $(CFLAGS) 
	cc -o rb_rwl_read $(CFLAGS) rbmain.c $(objects) rwl_read.o

rb_rcu: rbmain.c $(all_objs) 
	cc -c rbtree.c $(CFLAGS) -DRCU
	cc -o rb_rcu $(CFLAGS) rbmain.c $(objects) rcu.o

rb_lock: rbmain.c $(all_objs) 
	cc -c rbtree.c $(CFLAGS) 
	cc -o rb_lock $(CFLAGS) rbmain.c $(objects) lock.o

rb_nolock: rbmain.c $(all_objs) 
	cc -c rbtree.c $(CFLAGS) 
	cc -o rb_nolock $(CFLAGS) rbmain.c $(objects) nolock.o

parse: parse.c Makefile
	cc -g -o parse -Wall parse.c
