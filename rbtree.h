#ifndef RBTREE_H
#define RBTREE_H

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

#include "rbnode.h"

typedef struct
{
	rbnode_t *root;
    unsigned long long restructure_copies;
    unsigned long long restructure_multi_copies;
	unsigned long long swap_copies;
    unsigned long long restructures;
    unsigned long long grace_periods;
    void *lock;
} rbtree_t;

void rb_create(rbtree_t *tree, void *lock);
void *rb_find(rbtree_t *tree, long key);
int rb_insert(rbtree_t *tree, long key, void *value);
void *rb_remove(rbtree_t *tree, long key);
rbnode_t *rb_first_n(rbtree_t *tree);
void *rb_first(rbtree_t *tree, long *key);
//void *rb_last(rbtree_t *tree, long *key);
rbnode_t *rb_next(rbnode_t *node);
void *rb_next_nln(rbtree_t *tree, long prev_key, long *key);
//void *rb_prev(rbtree_t *tree, long next_key, long *key);
void rb_output_list(rbtree_t *tree);
void rb_output(rbtree_t *tree);
int rb_valid(rbtree_t *tree);
int rb_size(rbtree_t *tree);
#endif
