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

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <memory.h>

#include "rbnode.h"
#ifdef FG_LOCK
#include "lock.h"
#endif
#ifdef STM
#include "my_stm.h"
#endif

#define STACK_SIZE      20

typedef struct
{
    long long band1;
    rbnode_t node;
    long long band2;
} extended_node_t;

static unsigned long Index = 0;
#if !defined(URCU) && !defined(STM)
static void *Block[STACK_SIZE];
static int Top = 0;
#endif

#ifdef MULTIWRITERS
#include <pthread.h>
static pthread_mutex_t Alloc_Lock = PTHREAD_MUTEX_INITIALIZER;
#endif

//***********************************
static void *rb_alloc()
{
    extended_node_t *ptr;

#ifdef MULTIWRITERS
    pthread_mutex_lock(&Alloc_Lock);
#endif

#if !defined(URCU) && !defined(STM)
    if (Top != 0) 
    {
        ptr = Block[--Top];
    }
    else
#endif
    {
#ifdef STM
        ptr = (extended_node_t *)wlpdstm_tx_malloc(sizeof(extended_node_t));
//#warning "NOT USING WLPDSTM_TX_MALLOC"
        //ptr = (extended_node_t *)malloc(sizeof(extended_node_t));
#else
        ptr = (extended_node_t *)malloc(sizeof(extended_node_t));
#endif
        assert(ptr != NULL);
#ifdef FG_LOCK
        ptr->node.lock = NULL;
#endif
    }

    ptr->band1 = 0x0BADBAD0;
    ptr->band2 = 0x0DABDAB0;

#ifdef MULTIWRITERS
    pthread_mutex_unlock(&Alloc_Lock);
#endif

    //printf("rb_alloc %p\n", ptr);
    return &(ptr->node);
}
//***********************************
static void check_stack(void *ptr)
{
    int ii;
    for (ii=0; ii<Top; ii++)
    {
        assert(Block[ii] != ptr);
    }
}
//***********************************
void rbnode_free(void *ptr)
{
    //int ii;
    extended_node_t *eptr;
    long long *sptr;

#ifdef MULTIWRITERS
    pthread_mutex_lock(&Alloc_Lock);
#endif

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

#if !defined(URCU) && !defined(STM)
    check_stack(eptr);
    if (Top < STACK_SIZE)
    {
        Block[Top++] = eptr;
    }
    else
#endif
    {
#ifdef FG_LOCK
        if (eptr->node.lock != NULL) free(eptr->node.lock);
#endif
#ifdef RP_STM
        //printf("actual free of node key %ld: %p\n", eptr->node.key, &(eptr->node));
        wlpdstm_free(eptr);
#elif defined(STM)
        wlpdstm_tx_free(eptr, sizeof(extended_node_t));
#else
        free(eptr);
#endif
    }

#ifdef MULTIWRITERS
    pthread_mutex_unlock(&Alloc_Lock);
#endif
}
//***********************************
rbnode_t *rbnode_create(long key, void *value)
{
	rbnode_t *node = (rbnode_t *)rb_alloc();
#ifdef STM
	STORE(node->key, key);
	STORE(node->value, value);
	STORE(node->color, BLACK);
	STORE(node->right, NULL);
	STORE(node->left, NULL);
    STORE(node->parent, NULL);
#else
	node->key = key;
	node->value = value;
	node->color = BLACK;
	node->right = NULL;
	node->left = NULL;
    node->parent = NULL;
#endif

    // not multithread or TX safe, but index isn't important anyway
	node->index = Index++;

#ifdef FG_LOCK
    if (node->lock == NULL) node->lock = lock_init();
#endif

#ifdef STM
    STORE(node->height, 1);
    STORE(node->changeOVL, 0);
#else
    node->height = 1;
    node->changeOVL = 0;
#endif

	return node;
}
//***********************************
rbnode_t *rbnode_copy(rbnode_t *node)
{
	rbnode_t *newnode = (rbnode_t *)rb_alloc();
#ifdef STM
    STORE(newnode->key, LOAD(node->key));
    STORE(newnode->value, LOAD(node->value));
    STORE(newnode->left, LOAD(node->left));
    STORE(newnode->right, LOAD(node->right));
    STORE(newnode->parent, LOAD(node->parent));
    STORE(newnode->color, LOAD(node->color));

    // the following aren't used except for FG and AVL: lock, height, changeOVL

	if (LOAD(node->left)  != NULL)  STORE(LOAD(node->left)->parent, newnode);
	if (LOAD(node->right)  != NULL) STORE(LOAD(node->right)->parent, newnode);
#else
	memcpy(newnode, node, sizeof(rbnode_t));
#endif
    // NOTE: not multi-writer safe, but we'll ignore that. Index isn't that important
	newnode->index = Index++;

#ifdef STM
	if (LOAD(node->left)  != NULL) STORE(LOAD(node->left)->parent, newnode);
	if (LOAD(node->right) != NULL) STORE(LOAD(node->right)->parent, newnode);
#elif !defined(NO_GRACE_PERIOD)
	if (node->left  != NULL) node->left->parent = newnode;
	if (node->right != NULL) node->right->parent = newnode;
#endif

#ifdef FG_LOCK
    newnode->lock = lock_init();
#endif

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
    if (node->left  != NULL && node->left->parent != node)   return 4;
    if (node->right != NULL && node->right->parent != node)  return 5;

    //if (ABS(height(hode->left) = height(node->right)) > 1) return 6;
    //if (node->height != 1+MAX(height(node->left), height(node->right))) return 7;
    //if (node->changeOVL & 0x07LL) return 8;

    sptr = (long long *)node;
    eptr = (extended_node_t *)&sptr[-1];
    //printf("rb_free %p %p\n", ptr, eptr);
    if (eptr->band1 != 0x0BADBAD0) return 6;
    if (eptr->band2 != 0x0DABDAB0) return 7;

    return 0;
}

