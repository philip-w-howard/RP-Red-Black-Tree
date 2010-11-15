#ifndef __AVL_H
#define __AVL_H

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

#include <pthread.h>

typedef unsigned long long version_t;

typedef struct avl_node_s
{
    volatile int height;
    volatile version_t changeOVL;       // version
    long key;
    volatile void *value;
    volatile struct avl_node_s *parent;
    volatile struct avl_node_s *left;
    volatile struct avl_node_s *right;
    void *lock;
    unsigned long long index;
} avl_node_t;

extern avl_node_t *Retry;


void avl_create(avl_node_t *tree, void *lock);
void *avl_find(avl_node_t *tree, long key);
int avl_insert(avl_node_t *tree, long key, void *value);
void *avl_remove(avl_node_t *tree, long key);
avl_node_t *avl_first_n(avl_node_t *tree);
void *avl_first(avl_node_t *tree, long *key);
//void *avl_last(avl_node_t *tree, long *key);
avl_node_t *avl_next_n(avl_node_t *node);
void *avl_next(avl_node_t *tree, long prev_key, long *key);
//void *avl_prev(avl_node_t *tree, long next_key, long *key);
void avl_output_list(avl_node_t *tree);
void avl_output(avl_node_t *tree);
int avl_valid(avl_node_t *tree);
int avl_size(avl_node_t *tree);

#endif
