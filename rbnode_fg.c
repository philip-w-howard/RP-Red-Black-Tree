#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <memory.h>

#include "rbnode_fg.h"
#include "lock.h"

#define STACK_SIZE      20

static unsigned long Index = 0;
#ifndef URCU
static void *Block[STACK_SIZE];
static int Top = 0;
#endif
//***********************************
static void *rb_alloc()
{
    rbnode_t *ptr;

#ifndef URCU
    if (Top != 0) 
    {
        ptr = Block[--Top];
    }
    else
#endif
    {
        ptr = (rbnode_t *)malloc(sizeof(rbnode_t));
        assert(ptr != NULL);
    }

    return ptr;
}
//***********************************
void rbnode_free(void *ptr)
{
#ifndef URCU
    if (Top < STACK_SIZE)
    {
        Block[Top++] = ptr;
    }
    else
#endif
    {
        free(ptr);
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
    node->lock = lock_init();

	return node;
}
//***********************************
rbnode_t *rbnode_copy(rbnode_t *node)
{
	rbnode_t *newnode = (rbnode_t *)rb_alloc();
	memcpy(newnode, node, sizeof(rbnode_t));
	newnode->index = Index++;
    node->lock = lock_init();

	if (node->left  != NULL) 
    {
        //write_lock(node->left->lock);
        node->left->parent  = newnode;
        //write_unlock(node->left->lock);
    }
	if (node->right != NULL) 
    {
        //write_lock(node->right->lock);
        node->right->parent = newnode;
        //write_unlock(node->right->lock);
    }

	return newnode;
}
//**************************************
int rbnode_invalid(rbnode_t *node, int depth)
{
    if (depth > 32) return 1;
    if (node == NULL) return 0;
    if (node->left  != NULL && node->key < node->left->key)  return 2;
    if (node->right != NULL && node->key > node->right->key) return 3;

    return 0;
}
