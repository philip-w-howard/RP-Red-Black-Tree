#ifndef __LOCK_H
#define __LOCK_H
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

//#include <atomic_ops.h>
#include <pthread.h>
#include "atomic_ops.h"

#ifndef CACHE_LINE_SIZE
#define CACHE_LINE_SIZE     128
#endif

char *implementation_name();

void lock_thread_init(void *arg, int thread_id);
void lock_thread_close(void *arg, int thread_id);
void *lock_init();
void read_lock(void *lock);
void read_unlock(void *lock);
void write_lock(void *lock);
void write_unlock(void *lock);

// optional routine
void upgrade_lock(void *lock);

#if defined(RCU) || defined(RP_STM)
void rw_lock(void *lock);
void rw_unlock(void *lock);
#else
#define rw_lock read_lock
#define rw_unlock read_unlock
#endif

unsigned long long *get_thread_stats(
        unsigned long long n_reads,   unsigned long long n_read_fails,
        unsigned long long n_writes, unsigned long long n_write_fails,
        unsigned long long other1, unsigned long long other2);

#endif

