#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <memory.h>

#include "rbnode.h"

#define STACK_SIZE      20

typedef struct
{
    long long band1;
    rbnode_t node;
    long long band2;
} extended_node_t;

static unsigned long Index = 0;
#ifndef URCU
static void *Block[STACK_SIZE];
static int Top = 0;
#endif
//***********************************
static void *rb_alloc()
{
    extended_node_t *ptr;

#ifndef URCU
    if (Top != 0) 
    {
        ptr = Block[--Top];
    }
    else
#endif
    {
        ptr = (extended_node_t *)malloc(sizeof(extended_node_t));
        assert(ptr != NULL);
    }

    ptr->band1 = 0x0BADBAD0;
    ptr->band2 = 0x0DABDAB0;

    //printf("rb_alloc %p\n", ptr);
    return &(ptr->node);
}
//***********************************
void rbnode_free(void *ptr)
{
    //int ii;
    extended_node_t *eptr;
    long long *sptr;

    sptr = (long long *)ptr;
    eptr = (extended_node_t *)&sptr[-1];
    //printf("rb_free %p %p\n", ptr, eptr);
    assert(eptr->band1 == 0x0BADBAD0);
    assert(eptr->band2 == 0x0DABDAB0);
    /*
    memset(eptr, 0x12, sizeof(extended_node_t));

    for (ii=0; ii<Top; ii++)
    {
        assert(eptr != Block[ii]);
    }
    */

#ifndef URCU
    if (Top < STACK_SIZE)
    {
        Block[Top++] = eptr;
    }
    else
#endif
    {
        free(eptr);
    }
}
//***********************************
rbnode_t *rbnode_create(long key, void *value)
{
	rbnode_t *node = (rbnode_t *)rb_alloc();
	node->key = key;
	node->value = value;
	node->color = BLACK;
	node->right = NULL;
	node->left = NULL;
    node->parent = NULL;
	node->index = Index++;

	return node;
}
//***********************************
rbnode_t *rbnode_copy(rbnode_t *node)
{
	rbnode_t *newnode = (rbnode_t *)rb_alloc();
	memcpy(newnode, node, sizeof(rbnode_t));
	newnode->index = Index++;

	if (node->left  != NULL) node->left->parent  = newnode;
	if (node->right != NULL) node->right->parent = newnode;

	return newnode;
}
//**************************************
int rbnode_invalid(rbnode_t *node, int depth)
{
    extended_node_t *eptr;
    long long *sptr;

    if (depth > 32) return 1;
    if (node == NULL) return 0;
    if (node->left  != NULL && node->key < node->left->key)  return 2;
    if (node->right != NULL && node->key > node->right->key) return 3;

    sptr = (long long *)node;
    eptr = (extended_node_t *)&sptr[-1];
    //printf("rb_free %p %p\n", ptr, eptr);
    if (eptr->band1 != 0x0BADBAD0) return 4;
    if (eptr->band2 != 0x0DABDAB0) return 5;

    return 0;
}
