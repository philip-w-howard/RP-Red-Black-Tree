#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <memory.h>

#include "rbnode.h"

#define STACK_SIZE      20

static unsigned long Index = 0;
static void *Block[STACK_SIZE];
static int Top = 0;
//***********************************
static void *rb_alloc(size_t size)
{
    if (Top != 0) 
    {
        return Block[--Top];
    }
    else
    {
        void *ptr =  malloc(size);
        return ptr;
    }
}
//***********************************
void rbnode_free(void *ptr)
{
    assert(ptr != NULL);

    if (Top < STACK_SIZE)
    {
        Block[Top++] = ptr;
    }
    else
    {
        free(ptr);
    }
}
//***********************************
rbnode_t *rbnode_create(unsigned long key, void *value)
{
	rbnode_t *node = (rbnode_t *)rb_alloc(sizeof(rbnode_t));
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
	rbnode_t *newnode = (rbnode_t *)rb_alloc(sizeof(rbnode_t));
	memcpy(newnode, node, sizeof(rbnode_t));
	newnode->index = Index++;

	if (node->left  != NULL) node->left->parent  = newnode;
	if (node->right != NULL) node->right->parent = newnode;

	return newnode;
}
