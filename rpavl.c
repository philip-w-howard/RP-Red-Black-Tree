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

//******************************************************
// 
// Red/Black Tree implementation by Phil Howard
//
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#ifdef URCU
#include <urcu.h>
#include <urcu-defer.h>
#endif

#include "rbnode.h"
#include "rbtree.h"
#include "lock.h"
#include "rcu.h"

#define HEIGHT(n)  ( (n)==NULL ? 0 : (n)->height )
#define BALANCE(n) ( (n)==NULL ? 0 : HEIGHT((n)->left) - HEIGHT((n)->right))
#define MAX(a,b)  ( (a) > (b) ? (a) : (b) )
#define ABS(a)    ( (a) > 0 ? (a) : -(a) )

//**************************************
//void check_for(rbnode_t *node, rbnode_t *new_node);
//**************************************
static char *toString(rbnode_t *node)
{
    static char buff[100];
    if (node == NULL) return "NULL";
    sprintf(buff, "%d:%ld", node->height, node->key);
    return buff;
}
//*******************************
static int is_left(rbnode_t *node)
{
    if (node->parent->left == node)
        return 1;
    else
        return 0;
}
//*******************************
void rb_create(rbtree_t *tree, void *lock)
{
    write_lock(lock);
    tree->root = NULL;
    tree->restructure_copies = 0;
    tree->restructure_multi_copies = 0;
    tree->swap_copies= 0;
    tree->restructures = 0;
    tree->grace_periods = 0;
    tree->lock = lock;
    write_unlock(lock);
}
//*******************************
static rbnode_t *find_node(rbtree_t *tree, long key)
{
	rbnode_t *node = rp_dereference(tree->root);

	while (node != NULL && key != node->key)
	{
		if (key < node->key) 
            node = rp_dereference(node->left);
		else 
            node = rp_dereference(node->right);
	}

	return node;
}
//*******************************
void *rb_find(rbtree_t *tree, long key)
{
    void *value;

    read_lock(tree->lock);
	rbnode_t *node = find_node(tree, key);

    if (node != NULL) 
        value = node->value;
    else
        value = NULL;

    read_unlock(tree->lock);

    return value;
}
//*******************************
static rbnode_t *restructure(rbtree_t *tree, rbnode_t *grandparent)
{
    rbnode_t *aprime, *bprime, *cprime;
    rbnode_t *parent, *node;
    rbnode_t *greatgrandparent = grandparent->parent;
    int left = 0;

    tree->restructures++;

    if (grandparent->parent != NULL) left = is_left(grandparent);
    //printf("restructure %s\n", toString(node));

    if (BALANCE(grandparent) > 1)
    {
        parent = grandparent->left;
        // NOTE: the following condition MUST be >= to favor diag
        //       if not, the algorithme isn't correct
        if (BALANCE(parent) >= 0)
        {
            node = parent->left;
            // diag left
#if defined(NO_GRACE_PERIOD) || defined(RCU)
            cprime = rbnode_copy(grandparent);
            tree->restructure_copies++;
            bprime = parent;
            aprime = node;
#else
            aprime = node;
            bprime = parent;
            cprime = grandparent;
#endif

            cprime->left = bprime->right;
            if (bprime->right != NULL) bprime->right->parent = cprime;
        
            rp_assign_pointer(bprime->right, cprime);
            cprime->parent = bprime;

            if (greatgrandparent != NULL)
            {
                if (left) {
                    rp_assign_pointer(greatgrandparent->left, bprime);
                } else {
                    rp_assign_pointer(greatgrandparent->right, bprime);
                }

                bprime->parent = greatgrandparent;
            } else {
                bprime->parent = NULL;
                rp_assign_pointer(tree->root, bprime);
            }
#if defined(NO_GRACE_PERIOD) || defined(RCU)
            rp_free(tree->lock, rbnode_free, grandparent);
#endif
        } 
        else 
        {
            // zig left
            node = parent->right;
#if defined(NO_GRACE_PERIOD) || defined(RCU)
            cprime = rbnode_copy(grandparent);
            aprime = rbnode_copy(parent);
            bprime = node;
            tree->restructure_multi_copies++;

            cprime->left = aprime;
            aprime->parent = cprime;
            aprime->right = bprime;
            bprime->parent = aprime;
#else
            aprime = parent;
            bprime = node;
            cprime = grandparent;
#endif
            aprime->right = bprime->left;
            if (bprime->left != NULL) bprime->left->parent = aprime;

            cprime->left = bprime->right;
            if (bprime->right != NULL) bprime->right->parent = cprime;

            rp_assign_pointer(bprime->left, aprime);
            aprime->parent = bprime;

            bprime->right = cprime;
            cprime->parent = bprime;

            if (greatgrandparent != NULL)
            {
                if (left) {
                    rp_assign_pointer(greatgrandparent->left, bprime);
                } else {
                    rp_assign_pointer(greatgrandparent->right, bprime);
                }

                bprime->parent = greatgrandparent;
            } else {
                bprime->parent = NULL;
                rp_assign_pointer(tree->root, bprime);
            }
#if defined(NO_GRACE_PERIOD) || defined(RCU)
            rp_free(tree->lock, rbnode_free, parent);
            rp_free(tree->lock, rbnode_free, grandparent);
#endif
        }
    }
    else
    {
        parent = grandparent->right;
        // NOTE: the following condition MUST be <= to favor diag
        //       if not, the algorithme isn't correct
        if (BALANCE(parent) <= 0)
        {
            // diag right
            node = parent->right;
#if defined(NO_GRACE_PERIOD) || defined(RCU)
            aprime = rbnode_copy(grandparent);
            tree->restructure_copies++;
            bprime = parent;
            cprime = node;
#else
            aprime = grandparent;
            bprime = parent;
            cprime = node;
#endif
            aprime->right = bprime->left;
            if (bprime->left != NULL) bprime->left->parent = aprime;

            rp_assign_pointer(bprime->left, aprime);
            aprime->parent = bprime;

            if (greatgrandparent != NULL)
            {
                if (left) {
                    rp_assign_pointer(greatgrandparent->left, bprime);
                } else {
                    rp_assign_pointer(greatgrandparent->right, bprime);
                }

                bprime->parent = greatgrandparent;
            } else {
                bprime->parent = NULL;
                rp_assign_pointer(tree->root, bprime);
            }
#if defined(NO_GRACE_PERIOD) || defined(RCU)
            rp_free(tree->lock, rbnode_free, grandparent);
#endif
        }
        else
        {
            // zig right
            node = parent->left;
#if defined(NO_GRACE_PERIOD) || defined(RCU)
            aprime = rbnode_copy(grandparent);
            cprime = rbnode_copy(parent);
            bprime = node;
            tree->restructure_multi_copies++;

            aprime->right = cprime;
            cprime->parent = aprime;
            cprime->left = bprime;
            bprime->parent = cprime;
#else
            aprime = grandparent;
            bprime = node;
            cprime = parent;
#endif
            aprime->right = bprime->left;
            if (bprime->left != NULL) bprime->left->parent = aprime;

            cprime->left = bprime->right;
            if (bprime->right != NULL) bprime->right->parent = cprime;

            rp_assign_pointer(bprime->left, aprime);
            aprime->parent = bprime;

            bprime->right = cprime;
            cprime->parent = bprime;

            if (greatgrandparent != NULL)
            {
                if (left) {
                    rp_assign_pointer(greatgrandparent->left, bprime);
                } else {
                    rp_assign_pointer(greatgrandparent->right, bprime);
                }

                bprime->parent = greatgrandparent;
            } else {
                bprime->parent = NULL;
                rp_assign_pointer(tree->root, bprime);
            }
#if defined(NO_GRACE_PERIOD) || defined(RCU)
            rp_free(tree->lock, rbnode_free, parent);
            rp_free(tree->lock, rbnode_free, grandparent);
#endif
        }
    }

    aprime->height = MAX( HEIGHT(aprime->left), HEIGHT(aprime->right) ) + 1;
    cprime->height = MAX( HEIGHT(cprime->left), HEIGHT(cprime->right) ) + 1;
    bprime->height = MAX( HEIGHT(bprime->left), HEIGHT(bprime->right) ) + 1;

    return bprime;
}
// ************* Rebalance ********************
static void rebalance(rbtree_t *tree, rbnode_t *node)
{
    int old_height;
    long old_key;

    while (node != NULL)
    {
        old_height = node->height;
        old_key = node->key;

        if (ABS(BALANCE(node)) > 1) 
            node = restructure(tree, node);
        else
            node->height = MAX( HEIGHT(node->left), HEIGHT(node->right) ) + 1;

        if (old_height==node->height && old_key==node->key) break;

        node = node->parent;
    }
}
//*******************************
int rb_insert(rbtree_t *tree, long key, void *value)
{
    rbnode_t *new_node;

    //printf("rb_insert write_lock\n");
    write_lock(tree->lock);

    //check_for(tree->root, new_node);

	if (tree->root == NULL)
	{
        new_node = rbnode_create(key, value);

		rp_assign_pointer(tree->root, new_node);
	} else {
		rbnode_t *node = tree->root;
		rbnode_t *prev = node;

		while (node != NULL)
		{
			prev = node;
            if (key == node->key)
            {
                write_unlock(tree->lock);
                return 0;
            }
            else if (key <= node->key) 
				node = node->left;
			else 
				node = node->right;
		}

        new_node = rbnode_create(key, value);

        new_node->height = 1;
        new_node->parent = prev;
        if (key <= prev->key) {
			rp_assign_pointer(prev->left, new_node);
		} else  {
			rp_assign_pointer(prev->right, new_node);
		}

        rebalance(tree, new_node->parent);
	}

    //printf("rb_insert write_unlock\n");
    write_unlock(tree->lock);

    return 1;
}
//*******************************
static rbnode_t *leftmost(rbnode_t *node)
{
	if (node == NULL) return NULL;

	while (node->left != NULL)
	{
		node = node->left;
	}
	return node;
}
//*******************************
static rbnode_t *rightmost(rbnode_t *node)
{
	if (node == NULL) return NULL;

	while (node->right != NULL)
	{
		node = node->right;
	}
	return node;
}
//*******************************
void *rb_remove(rbtree_t *tree, long key)
{
	rbnode_t *node;
	rbnode_t *prev = NULL;
	rbnode_t *next = NULL;
	rbnode_t *swap = NULL;
	void *value = NULL;

    write_lock(tree->lock);

	node = find_node(tree, key);

    // not found
	if (node == NULL) 
    {
        //printf("rb_remove not found write_unlock\n");
        write_unlock(tree->lock);
        return NULL;
    }

    prev = node->parent;

    // found it, so cut it out of the tree
    //******************* swap with external node if necessary ***************
	if (node->left != NULL && node->right != NULL)
    {
        // need to do a swap with leftmost on right branch
        swap = leftmost(node->right);

        if (swap == node->right)
        {
            // special case: swap is immediate child of node.
            swap->height = node->height;
            rp_assign_pointer(swap->left, node->left);
            node->left->parent = swap;      // safe: checked above

            if (prev == NULL) {
                rp_assign_pointer(tree->root, swap);
			} else {
                if (prev->left == node) {
                    rp_assign_pointer(prev->left, swap);
                } else {
                    rp_assign_pointer(prev->right, swap);
				}
			}
            swap->parent = prev;

            prev = swap;
            next = swap->right;
        } else {
#if defined(RCU) || defined(NO_GRACE_PERIOD)
            // exchange children of swap and node
			rbnode_t *new_node = rbnode_copy(swap);
            //check_for(tree->root, new_node);
			tree->swap_copies++;

            new_node->height = node->height;

            rp_assign_pointer(new_node->left, node->left);
            node->left->parent = new_node;      // safe: checked above

            rp_assign_pointer(new_node->right, node->right);
            node->right->parent = new_node;     // safe: checked above

            if (prev == NULL)
            {
                rp_assign_pointer(tree->root, new_node);
                new_node->parent = prev;
            } else {
                if (is_left(node))
                    rp_assign_pointer(prev->left, new_node);
                else 
                    rp_assign_pointer(prev->right, new_node);
                new_node->parent = prev;
            }

            // need to make sure bprime is seen before path to b is erased 
            rp_wait_grace_period(tree->lock);
            tree->grace_periods++;

            prev = swap->parent;
            next = swap->right;

            rp_assign_pointer(prev->left, swap->right);
            if (swap->right != NULL) swap->right->parent = prev;

			rp_free(tree->lock, rbnode_free, swap);
#else
            prev = swap->parent;
            next = swap->right;

            // fix-up swap's left child (replacing a NULL child)
            swap->left = node->left;
            node->left->parent = swap;      // safe: checked above
            //node->left = NULL;              // swap->left guaranteed to be NULL
            swap->height = node->height;

            // take swap temporarily out of the tree
            swap->parent->left = swap->right;
            if (swap->right != NULL) swap->right->parent = swap->parent;

            // fix-up swap's right child
            swap->right = node->right;
            node->right->parent = swap;     // safe: checked above

            // put swap in new location
            if (node->parent == NULL)
            {
                tree->root = swap;
                swap->parent = NULL;
            } else {
                if (is_left(node)) {
                    node->parent->left = swap;
                } else {
                    node->parent->right = swap;
				}
                swap->parent = node->parent;
            }
#endif
        }
    } else {
        // the node is guaranteed to have a terminal child
        prev = node->parent;
        if (node->left == NULL)
	    {
            // no left branch; bump right branch up
            next = node->right;
	    } else {
		    next = node->left;
	    }

        if (prev != NULL)
        {
		    if (is_left(node))
            {
			    rp_assign_pointer(prev->left, next);
            } else {
                rp_assign_pointer(prev->right, next);
            }
            if (next != NULL) next->parent = prev;
        } else {
		    rp_assign_pointer(tree->root, next);
		    if (next != NULL) next->parent = NULL;
	    }
    }

    //******************** rebalance *******************
    // need prev == parent of the removed node
    rebalance(tree, prev);

    // save value, free node, return value
    value = node->value;

    rp_free(tree->lock, rbnode_free, node);

    //printf("rb_remove write_unlock\n");
    write_unlock(tree->lock);
	return value;
}
//***************************************
rbnode_t *rb_first_n(rbtree_t *tree)
{
    return leftmost(tree->root);
}
//***************************************
void *rb_first(rbtree_t *tree, long *key)
{
    rbnode_t *node;
    void *value = NULL;

    read_lock(tree->lock);

    node = leftmost(tree->root);
    if (node != NULL)
    {
        *key = node->key;
        value = node->value;
    }

    read_unlock(tree->lock);

    return value;
}
//***************************************
void *rb_last(rbtree_t *tree, long *key)
{
    rbnode_t *node;
    void *value = NULL;

    read_lock(tree->lock);

    node = rightmost(tree->root);
    if (node != NULL)
    {
        *key = node->key;
        value = node->value;
    }

    read_unlock(tree->lock);

    return value;
}
//***************************************
rbnode_t *rb_next_n(rbnode_t *x)
{
    rbnode_t *xr,*y;

    if ((xr = x->right) != NULL) return leftmost(xr);

    y = x->parent;
    while (y != NULL && x==y->right)
    {
        x = y;
        y = y->parent;
    }

    return y;
}
//***************************************
void *rb_next(rbtree_t *tree, long prev_key, long *key)
{
    static __thread char buff[1000];

    rbnode_t *bigger_node = NULL;
    rbnode_t *node;
    void *value;

    buff[0] = 0;

    read_lock(tree->lock);
    
    node = tree->root;

    strcat(buff, "ROOT");

    while (node != NULL)
    {
        if (node->key == prev_key)
        {
            strcat(buff, " =right");
            node = node->right;
        } 
        else if (node->key > prev_key)
        {
            strcat(buff, " left");
            bigger_node = node;
            node = node->left;
        }
        else
        {
            strcat(buff, " right");
            node = node->right;
        }
    }

    if (bigger_node != NULL)
    {
        *key = bigger_node->key;
        value = bigger_node->value;
        value = buff;
    }
    else
    {
        *key = prev_key;
        value = NULL;
    }

    read_unlock(tree->lock);

    return value;
}
//***************************************
void *rb_old_next(rbtree_t *tree, long prev_key, long *key)
{
    rbnode_t *node;
    void *value;

    read_lock(tree->lock);
    
    node = tree->root;

    while (node != NULL)
    {
        if (node->key == prev_key)
        {
            node = leftmost(node->right);
            assert(node != NULL);
            break;
        } 
        else if (node->key > prev_key)
        {
            if (node->left != NULL)
            {
                if (node->left->key == prev_key && node->left->right == NULL) break;
                node = node->left;
            }
            else
            {
                break;
            }
        }
        else
        {
            node = node->right;
        }
    }

    if (node != NULL)
    {
        *key = node->key;
        value = node->value;
    }
    else
    {
        *key = prev_key;
        value = NULL;
    }

    read_unlock(tree->lock);

    return value;
}
//***************************************
void *rb_prev(rbtree_t *tree, long next_key, long *key)
{
    assert(0);
    return NULL;
}
//**************************************
static int rec_invalid_node(rbnode_t *node, int depth);
//**************************************
static void output_list(rbnode_t *node, int depth)
{
    if (depth > 32)
    {
        printf("Depth too deep for a valid tree\n");
        return;
    }

    if (node != NULL)
    {
        output_list(node->left, depth+1);
        printf("depth: %d value: %s", depth, toString(node));
        printf(" l: %s", toString(node->left));
        printf(" r: %s", toString(node->right));
        if (rec_invalid_node(node, depth)) 
            printf(" INVALID NODE: %d\n", rec_invalid_node(node, depth));
        else
            printf("\n");
        output_list(node->right, depth+1);
    }
}
//**************************************
#define MAX_ROW_LENGTH 1000
static int o_nrows = 0;
static char **o_rows = NULL;
//************************************
static void o_resize()
{
    int row;
    unsigned int length = 0;

    for (row=0; row<o_nrows; row++)
    {
        if (strlen(o_rows[row]) > length) length = strlen(o_rows[row]);
    }

    for (row=0; row<o_nrows; row++)
    {
        while (strlen(o_rows[row]) < length) 
        {
            strcat(o_rows[row], " ");
        }
    }
}
//**************************************
void output(rbnode_t *node, int depth)
{
    if (node == NULL) return;

    if (depth >= o_nrows)
    {
        o_nrows++;
        o_rows = (char **)realloc(o_rows, o_nrows*sizeof(char *));
        o_rows[o_nrows-1] = (char *)malloc(MAX_ROW_LENGTH);
        o_rows[o_nrows-1][0] = 0;

        o_resize();
    }


    output(node->left, depth+1);

    strcat(o_rows[depth], toString(node));
    assert( (strlen(o_rows[depth]) < MAX_ROW_LENGTH) );
    o_resize();

    output(node->right, depth+1);
}
//**************************************
void rb_output_list(rbtree_t *tree)
{
    output_list(tree->root, 0);
}//**************************************
void rb_output(rbtree_t *tree)
{
    int ii;

    output(tree->root, 0);

    for (ii=0; ii<o_nrows; ii++)
    {
        printf("%s\n", o_rows[ii]);
        free(o_rows[ii]);
    }

    free(o_rows);
    o_rows = NULL;
    o_nrows = 0;

}
//**************************************
static int rec_invalid_node(rbnode_t *node, int depth)
{
    int invalid;

    if (node == NULL) return 0;

    if (ABS(BALANCE(node)) > 1) return 9;
    if (node->height != MAX(HEIGHT(node->left), HEIGHT(node->right))+1) return 8;

    invalid = rbnode_invalid(node, depth);
    if (invalid) return invalid;

    if (node == NULL) return 0;

    invalid = rec_invalid_node(node->left, depth+1);
    if (invalid) return invalid;

    invalid = rec_invalid_node(node->right, depth+1);
    if (invalid) return invalid;

    return 0;
}
//***************************************
int rb_valid(rbtree_t *tree)
{
    return !rec_invalid_node(tree->root, 0);
}
//***************************************
void check_for(rbnode_t *node, rbnode_t *new_node)
{
    if (node == NULL) return;

    assert(node != new_node);

    check_for(node->left, new_node);
    check_for(node->right, new_node);
}
//****************************************
static int rbn_size(rbnode_t *node)
{
    int size = 0;
    if (node == NULL) return 0;
    size = 1;
    size += rbn_size(node->left);
    size += rbn_size(node->right);

    return size;
}
//****************************************
int rb_size(rbtree_t *tree)
{
    return rbn_size(tree->root);
}
