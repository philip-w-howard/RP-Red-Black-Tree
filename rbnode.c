#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <memory.h>

#include "rbnode.h"
#ifdef FG_LOCK
#include "lock.h"
#endif
#ifdef STM
#include <stm.h>
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
#ifdef STM
        wlpdstm_tx_free(ptr, sizeof(extended_node_t));
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
	node->key = key;
	node->value = value;
	node->color = BLACK;
	node->right = NULL;
	node->left = NULL;
    node->parent = NULL;
	node->index = Index++;

#ifdef FG_LOCK
    if (node->lock == NULL) node->lock = lock_init();
#endif

    node->height = 1;
    node->changeOVL = 0;

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

