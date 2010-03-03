#ifndef RBITER_H
#define RBITER_H

#include "rbnode.h"
#include "rbtree.h"

typedef struct rbtree_iter_s
{
	rbnode_t *node;
	struct rbtree_iter_s *next;
    int depth;
} rbtree_iter_t;

void rb_iterator_init(rbtree_iter_t *iter, rbtree_t *tree);
void *rbtree_iter_next(rbtree_iter_t *iter);
int brtree_iter_has_next(rbtree_iter_t *iter);


#endif