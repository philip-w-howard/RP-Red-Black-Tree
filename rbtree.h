#ifndef RBTREE_H
#define RBTREE_H

#include "rbnode.h"

typedef struct
{
	rbnode_t *root;
    unsigned long long restructure_copies;
	unsigned long long swap_copies;
    unsigned long long restructures;
    void *lock;
} rbtree_t;

void rb_create(rbtree_t *tree, void *lock);
void *rb_find(rbtree_t *tree, long key);
int rb_insert(rbtree_t *tree, long key, void *value);
void *rb_remove(rbtree_t *tree, long key);
void *rb_first(rbtree_t *tree, long *key);
void *rb_last(rbtree_t *tree, long *key);
void *rb_next(rbtree_t *tree, long prev_key, long *key);
void *rb_prev(rbtree_t *tree, long next_key, long *key);
void rb_output_list(rbtree_t *tree);
void rb_output(rbtree_t *tree);
int rb_valid(rbtree_t *tree);
#endif
