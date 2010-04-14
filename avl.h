#ifndef __AVL_H
#define __AVL_H

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

#endif
