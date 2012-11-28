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

//**************************************
//void check_for(rbnode_t *node, rbnode_t *new_node);
//**************************************
static char *toString(rbnode_t *node)
{
    static char buff[100];
    char *color = "BRb";
    if (node == NULL) return "NULL";
    sprintf(buff, "%c%ld", color[node->color], node->key);
    return buff;
}
//*******************************
static int is_left(rbnode_t *node)
{
    if (node==NULL || node->parent==NULL) return 0;

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
static rbnode_t *sibling(rbnode_t *node)
{
    if (node->parent->left == node)
        return node->parent->right;
    else
        return node->parent->left;
}
//*******************************
static rbnode_t *fix_parent(rbtree_t *tree, rbnode_t *node, rbnode_t *parent, int left)
{
    rbnode_t *gp_prime;

#ifdef NO_GRACE_PERIOD
    gp_prime = rbnode_copy(parent);
#else
    gp_prime = parent;
#endif

    if (node != NULL) node->parent = gp_prime;
    if (gp_prime != NULL)
    {
        if (left) {
            rp_assign_pointer(gp_prime->left, node);
        } else {
            rp_assign_pointer(gp_prime->right, node);
        }
#ifdef NO_GRACE_PERIOD
        if (gp_prime->parent == NULL) {
            rp_assign_pointer(tree->root, gp_prime);
        } else {
            if (is_left(parent))
                rp_assign_pointer(gp_prime->parent->left, gp_prime);
            else
                rp_assign_pointer(gp_prime->parent->right, gp_prime);
        }

        if (gp_prime->left != NULL) gp_prime->left->parent = gp_prime;
        if (gp_prime->right != NULL) gp_prime->right->parent = gp_prime;

        rp_free(tree->lock, rbnode_free, parent);
#endif
    } else {
        rp_assign_pointer(tree->root, node);
    }

    return gp_prime;
}
//*******************************
static void diag_left(rbtree_t *tree, rbnode_t *grandparent, rbnode_t *parent, rbnode_t *node,
                        rbnode_t **a, rbnode_t **b, rbnode_t **c)
{
    rbnode_t *aprime, *bprime, *cprime;
    rbnode_t *greatgrandparent = grandparent->parent;
    int left = 0;
    rbnode_t *three;

    if (grandparent->parent != NULL) left = is_left(grandparent);

#if defined(NO_GRACE_PERIOD)
    cprime = rbnode_copy(grandparent);
    bprime = rbnode_copy(parent);
    aprime = node;
    three = rbnode_copy(parent->right);
#elif defined(RCU)
    cprime = rbnode_copy(grandparent);
    bprime = parent;
    aprime = node;
    three = parent->right;
#else
    aprime = node;
    bprime = parent;
    cprime = grandparent;
    three = parent->right;
#endif
    cprime->left = three;
    if (three != NULL) three->parent = cprime;
        
    bprime->right = cprime;
    cprime->parent = bprime;

    fix_parent(tree, bprime, greatgrandparent, left);
#if defined(NO_GRACE_PERIOD)
    aprime->parent = bprime;
    if (cprime->right != NULL) cprime->right->parent = cprime;

    if (three != NULL) 
    {
        if (three->left != NULL) three->left->parent = three;
        if (three->right != NULL) three->right->parent = three;

        rp_free(tree->lock, rbnode_free, parent->right);
    }

    rp_free(tree->lock, rbnode_free, parent);
    rp_free(tree->lock, rbnode_free, grandparent);
#elif defined(RCU)
    rp_free(tree->lock, rbnode_free, grandparent);
#endif

    *a = aprime;
    *b = bprime;
    *c = cprime;
}
//*******************************
static void zig_left(rbtree_t *tree, rbnode_t *grandparent, rbnode_t *parent, rbnode_t *node,
                        rbnode_t **a, rbnode_t **b, rbnode_t **c)
{
    rbnode_t *aprime, *bprime, *cprime;
    rbnode_t *two, *three;
    rbnode_t *greatgrandparent = grandparent->parent;
    int left = 0;

    if (grandparent->parent != NULL) left = is_left(grandparent);

#if defined(NO_GRACE_PERIOD)
    cprime = rbnode_copy(grandparent);
    aprime = rbnode_copy(parent);
    bprime = rbnode_copy(node);
    two = rbnode_copy(bprime->left);
    three = rbnode_copy(bprime->right);
#elif defined(RCU)
#ifdef SLOW
    cprime = grandparent;
    aprime = parent;
    bprime = rbnode_copy(node);
    two = bprime->left;
    three = bprime->right;
#else // !SLOW
    cprime = rbnode_copy(grandparent);
    aprime = rbnode_copy(parent);
    bprime = node;
    two = bprime->left;
    three = bprime->right;
#endif
#else // don't do copy-on-update
    aprime = parent;
    bprime = node;
    cprime = grandparent;
    two = bprime->left;
    three = bprime->right;
#endif

#if defined(RCU) && defined(SLOW)
    bprime->right = cprime;
    cprime->parent = bprime;

    bprime->left = aprime;
    aprime->parent = bprime;

    fix_parent(tree, bprime, greatgrandparent, left);
    rp_wait_grace_period(tree->lock);

    aprime->right = two;
    if (two != NULL) two->parent = aprime;

    cprime->left = three;
    if (three != NULL) three->parent = cprime;
#else
    aprime->right = two;
    if (two != NULL) two->parent = aprime;

    cprime->left = three;
    if (three != NULL) three->parent = cprime;

    bprime->left = aprime;
    aprime->parent = bprime;

    bprime->right = cprime;
    cprime->parent = bprime;

    fix_parent(tree, bprime, greatgrandparent, left);
#endif

#if defined(NO_GRACE_PERIOD)
    if (aprime->left != NULL) aprime->left->parent = aprime;
    if (cprime->right != NULL) cprime->right->parent = cprime;

    if(two != NULL) 
    {
        if (two->left != NULL) two->left->parent = two;
        if (two->right != NULL) two->right->parent = two;
        rp_free(tree->lock, rbnode_free, node->left);
    }

    if(three != NULL) 
    {
        if (three->left != NULL) three->left->parent = three;
        if (three->right != NULL) three->right->parent = three;
        rp_free(tree->lock, rbnode_free, node->right); 
    }
    rp_free(tree->lock, rbnode_free, node);
    rp_free(tree->lock, rbnode_free, parent);
    rp_free(tree->lock, rbnode_free, grandparent);
#elif defined(RCU)
#ifdef SLOW
    rp_free(tree->lock, rbnode_free, node);
#else
    rp_free(tree->lock, rbnode_free, parent);
    rp_free(tree->lock, rbnode_free, grandparent);
#endif
#endif

    *a = aprime;
    *b = bprime;
    *c = cprime;
}
//*******************************
static void diag_right(rbtree_t *tree, rbnode_t *grandparent, rbnode_t *parent, rbnode_t *node,
                        rbnode_t **a, rbnode_t **b, rbnode_t **c)
{
    rbnode_t *aprime, *bprime, *cprime;
    rbnode_t *two;
    rbnode_t *greatgrandparent = grandparent->parent;
    int left = 0;

    if (grandparent->parent != NULL) left = is_left(grandparent);

#if defined(NO_GRACE_PERIOD)
    aprime = rbnode_copy(grandparent);
    bprime = rbnode_copy(parent);
    cprime = node;
    two = rbnode_copy(parent->left);
#elif defined(RCU)
    aprime = rbnode_copy(grandparent);
    bprime = parent;
    cprime = node;
    two = parent->left;
#else
    aprime = grandparent;
    bprime = parent;
    cprime = node;
    two = parent->left;
#endif
    aprime->right = two;
    if (two != NULL) two->parent = aprime;

    bprime->left = aprime;
    aprime->parent = bprime;

    fix_parent(tree, bprime, greatgrandparent, left);
#if defined(NO_GRACE_PERIOD)
    cprime->parent = bprime;
    if (aprime->left != NULL) aprime->left->parent = aprime;

    if (two != NULL) 
    {
        if (two->left != NULL) two->left->parent = two;
        if (two->right != NULL) two->right->parent = two;
        rp_free(tree->lock, rbnode_free, parent->left);
    }

    rp_free(tree->lock, rbnode_free, parent);
    rp_free(tree->lock, rbnode_free, grandparent);
#elif defined(RCU)
    rp_free(tree->lock, rbnode_free, grandparent);
#endif

    *a = aprime;
    *b = bprime;
    *c = cprime;
}
//*******************************
static void zig_right(rbtree_t *tree, rbnode_t *grandparent, rbnode_t *parent, rbnode_t *node,
                        rbnode_t **a, rbnode_t **b, rbnode_t **c)
{
    rbnode_t *aprime, *bprime, *cprime;
    rbnode_t *two, *three;
    rbnode_t *greatgrandparent = grandparent->parent;
    int left = 0;

    if (grandparent->parent != NULL) left = is_left(grandparent);

#if defined(NO_GRACE_PERIOD)
    aprime = rbnode_copy(grandparent);
    bprime = rbnode_copy(node);
    cprime = rbnode_copy(parent);
    two = rbnode_copy(bprime->left);
    three = rbnode_copy(bprime->right);
#elif defined(RCU)
    aprime = rbnode_copy(grandparent);
    cprime = rbnode_copy(parent);
    bprime = node;
    two = bprime->left;
    three = bprime->right;
#else
    aprime = grandparent;
    bprime = node;
    cprime = parent;
    two = bprime->left;
    three = bprime->right;
#endif
    aprime->right = two;
    if (two != NULL) two->parent = aprime;

    cprime->left = three;
    if (three != NULL) three->parent = cprime;

    bprime->left = aprime;
    aprime->parent = bprime;

    bprime->right = cprime;
    cprime->parent = bprime;

    fix_parent(tree, bprime, greatgrandparent, left);
#if defined(NO_GRACE_PERIOD)
    if (aprime->left != NULL) aprime->left->parent = aprime;
    if (cprime->right != NULL) cprime->right->parent = cprime;

    if (two != NULL) 
    {
        if (two->left != NULL) two->left->parent = two;
        if (two->right != NULL) two->right->parent = two;
        rp_free(tree->lock, rbnode_free, node->left);
    }

    if (three != NULL) 
    {
        if (three->left != NULL) three->left->parent = three;
        if (three->right != NULL) three->right->parent = three;
        rp_free(tree->lock, rbnode_free, node->right);    
    }

    rp_free(tree->lock, rbnode_free, node);
    rp_free(tree->lock, rbnode_free, parent);
    rp_free(tree->lock, rbnode_free, grandparent);
#elif defined(RCU)
    rp_free(tree->lock, rbnode_free, parent);
    rp_free(tree->lock, rbnode_free, grandparent);
#endif

    *a = aprime;
    *b = bprime;
    *c = cprime;
}
//*******************************
#define DIAG_LEFT  1
#define ZIG_LEFT   2
#define DIAG_RIGHT 3
#define ZIG_RIGHT  4
static int restructure(rbtree_t *tree, rbnode_t *grandparent, rbnode_t *parent, rbnode_t *node,
                        rbnode_t **a, rbnode_t **b, rbnode_t **c)
{
    //NOSTATS tree->restructures++;

    if (grandparent->left == parent && parent->left == node)
    {
        diag_left(tree, grandparent, parent, node, a, b, c);
        return DIAG_LEFT;
    } 
    else if (grandparent->left == parent && parent->right == node)
    {
        zig_left(tree, grandparent, parent, node, a, b, c);
        return ZIG_LEFT;
    }
    else if (parent->right == node && grandparent->right == parent)
    {
        diag_right(tree, grandparent, parent, node, a, b, c);
        return DIAG_RIGHT;
    }
    else
    {
        zig_right(tree, grandparent, parent, node, a, b, c);
        return ZIG_RIGHT;
    }

    return 0;
}
//*******************************
static void recolor(rbtree_t *tree, rbnode_t *node)
{
    rbnode_t *parent = node->parent;
    rbnode_t *grandparent = parent->parent;
    rbnode_t *w = sibling(parent);
    rbnode_t *a, *b, *c;

    if (w == NULL || w->color == BLACK)
    {
        // case 1
        restructure(tree, grandparent, parent, node, &a, &b, &c);

        a->color = RED;
        b->color = BLACK;
        c->color = RED;
    } else if ( w!=NULL && w->color == RED) {
        // case 2
        w->color = BLACK;
        parent->color = BLACK;

        // condition makes sure root stays black
        if (grandparent->parent != NULL) grandparent->color = RED;

        if (grandparent->parent != NULL && grandparent->parent->color == RED)
        {
            recolor(tree, grandparent);
        }
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

        new_node->color = BLACK;
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

        new_node->color = RED;
        new_node->parent = prev;
        if (key <= prev->key) {
			rp_assign_pointer(prev->left, new_node);
		} else  {
			rp_assign_pointer(prev->right, new_node);
		}

        if (prev->color == RED) recolor(tree, new_node);
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
static void double_black(rbtree_t *tree, rbnode_t *r);
static void double_black_node(rbtree_t *tree, rbnode_t *x, rbnode_t *y, rbnode_t *r)
{
    rbnode_t *z;
    rbnode_t *a, *b, *c;
    int rest_type;

    if (y->color == BLACK)
    {
        z = NULL;
        if (y->left != NULL && y->left->color == RED)
            z = y->left;
        else if (y->right != NULL && y->right->color == RED)
            z = y->right;
        if (z != NULL)
        {
            // case 1
			int x_color = x->color;
            restructure(tree, x, y, z, &a, &b, &c);
            b->color = x_color;
            a->color = BLACK;
            c->color = BLACK;
            if (r != NULL) r->color = BLACK;
            return;
        } else {
            // case 2
            if (r != NULL) r->color = BLACK;
            y->color = RED;
            if (x->color == RED)
                x->color = BLACK;
            else
            {
                x->color = BLACK_BLACK;
                double_black(tree, x);
            }
            return;
        }
    }
    else // if (y->color == RED)
    {
        // case 3
        if (is_left(y))
            z = y->left;
        else 
            z = y->right;
        rest_type = restructure(tree, x,y,z, &a, &b, &c);
#if defined(RCU) || defined(NO_GRACE_PERIOD)
		// in RCU versions, some nodes get replaced. Fix up so pointers 
        // point to correct new nodes.
        // NOTE: this is guaranteed to be a DIAG restructure, but we'll code
        //       for all cases just for completeness.
        assert (rest_type==DIAG_LEFT || rest_type==DIAG_RIGHT);
        switch(rest_type)
        {
            case DIAG_LEFT:
                x = c;
                y = b;
                z = a;
                break;
            case ZIG_LEFT:
                x = c;
                y = a;
                z = b;
                break;
            case DIAG_RIGHT:
                x = a;
                y = b;
                z = c;
                break;
            case ZIG_RIGHT:
                x = a;
                y = c;
                z = b;
                break;
        }
#endif

		y->color = BLACK;
        x->color = RED;

        if (x->left == r)
            y = x->right;
        else
            y = x->left;

        assert(r == sibling(y));
        double_black_node(tree, x, y, r);
    }
}
//*******************************
static void double_black(rbtree_t *tree, rbnode_t *r)
{
    rbnode_t *x, *y;

    x = r->parent;
	if (x == NULL)
	{
		r->color = BLACK;
	} else {
		y = sibling(r);
		double_black_node(tree, x,y,r);
	}
}
//*******************************
#ifdef NO_GRACE_PERIOD
static void copy_to_leftmost(rbnode_t *node, rbnode_t **node_right, rbnode_t **swap_prime)
{
    rbnode_t *right;
    rbnode_t *curr, *next;

    right = rbnode_copy(node->right);
    curr = right;
    while (curr->left != NULL)
    {
        next = rbnode_copy(curr->left);
        next->parent = curr;
        curr->left = next;
        curr = next;
    }

    // if not the special case of leftmost == right, then delete the node
    // at the end of the chain (i.e. pull swap out of the tree)
    if (curr != right) curr->parent->left = curr->right;
    *swap_prime = curr;
    *node_right = right;
}
#endif
//*******************************
#ifdef NO_GRACE_PERIOD
// fixup the parent pointers of the non-copied childred to point to the 
// new nodes
static void fixup_to_leftmost(rbnode_t *node)
{
    node->left->parent = node;
    if (node->right != NULL) node->right->parent = node;
    node = node->right;

    while (node != NULL)
    {
        if (node->right != NULL) node->right->parent = node;
        node = node->left;
    }
}
#endif
//*******************************
#ifdef NO_GRACE_PERIOD
// rp_free all the duplicates that we made with the exception of node. We
// use node later and it is freed at the end.
static void cleanup_to_leftmost(rbtree_t *tree, rbnode_t *node)
{
    rbnode_t *next;
    node = node->right;

    while (node != NULL)
    {
        next = node->left;
        rp_free(tree->lock, rbnode_free, node);
        node = next;
    }
}
#endif
//*******************************
void *rb_remove(rbtree_t *tree, long key)
{
	rbnode_t *node;
	rbnode_t *prev = NULL;
	rbnode_t *next = NULL;
	rbnode_t *swap = NULL;
	void *value = NULL;
    int temp_color;

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
#if defined(NO_GRACE_PERIOD)
        rbnode_t *n_prev, *n_next;
        rbnode_t *swap_right;
        rbnode_t *swap_right_prime;

        // need to copy all nodes on path from node to swap
        rbnode_t *node_right;
        copy_to_leftmost(node, &node_right, &swap);

        // save prev, next for later
        // need to do a swap with leftmost on right branch
        if (swap == node_right)
            n_prev = swap;
        else
            n_prev = swap->parent;
        n_next = swap->right;

        // get colors right
        temp_color = swap->color;
        swap->color = node->color;
        node->color = temp_color;

        // move swap up into top of subtree
        swap->left = node->left;
        // treat special case of swap == node->right as special
        if (swap != node_right)
        {
            swap_right = swap->right;
            if (swap_right != NULL)
            {
                swap_right_prime = rbnode_copy(swap->right);
                n_next = swap_right_prime;
                swap->parent->left = swap_right_prime;
                swap_right_prime->parent = swap->parent;
            }
            swap->right = node_right;
            node_right->parent = swap;
        }
        // fixup swap_prime->left->parent after we link swap_prime into tree

        prev = fix_parent(tree, swap, prev, is_left(node));
        fixup_to_leftmost(swap);
        cleanup_to_leftmost(tree, node);
        if (swap != node_right && swap_right != NULL) 
        {
            rp_free(tree->lock, rbnode_free, swap_right);
        }

        // save prev, next for later
        // need to do a swap with leftmost on right branch
        prev = n_prev;
        next = n_next;
#else
        // need to do a swap with leftmost on right branch
        swap = leftmost(node->right);
        temp_color = swap->color;
        swap->color = node->color;
        node->color = temp_color;

        if (swap == node->right)
        {
            // special case: no copies required
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
#if defined(RCU) 
            // exchange children of swap and node
			rbnode_t *new_node = rbnode_copy(swap);
            //check_for(tree->root, new_node);
			//NOSTATS tree->swap_copies++;

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
            //NOSTATS tree->grace_periods++;

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
#endif      // RCU
        }
#endif      // NO_GRACE_PERIOD
    } else {
        // the node is guaranteed to have a terminal child
#if defined(NO_GRACE_PERIOD)
        rbnode_t *o_next;
#endif
        prev = node->parent;
        if (node->left == NULL)
	    {
            // no left branch; bump right branch up
            next = node->right;
	    } else {
		    next = node->left;
	    }

#if defined(NO_GRACE_PERIOD)
        o_next = next;
        next = rbnode_copy(next);
#endif

        prev = fix_parent(tree, next, prev, is_left(node));

#if defined(NO_GRACE_PERIOD)
        if (next != NULL) 
        {
            if (next->left != NULL) next->left->parent = next;
            if (next->right != NULL) next->right->parent = next;
            rp_free(tree->lock, rbnode_free, o_next);
        }
#endif
    }

    //******************** rebalance *******************
    // need node==deleted node, prev,next == parent,child of node in its swapped position
    if (node->color == RED || (next!=NULL && next->color == RED))
    {
        // case 1
        if (next != NULL) next->color = BLACK;
    } 
    else if (node->color == BLACK)
    {
        if (prev->left == next)
        {
            double_black_node(tree, prev, prev->right, next);
        }
        else
        {
            double_black_node(tree, prev, prev->left, next);
        }
    }

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
// O(N) traversal
rbnode_t *rb_next(rbnode_t *x)
{
    rbnode_t *xr,*y;
#ifdef INSTRUMENTED
#error this is INSTRUMENTED
#endif

    if ((xr = rp_dereference(x->right)) != NULL) return leftmost(xr);

    y = rp_dereference(x->parent);
    while (y != NULL && y->right != NULL && 
           x->key==rp_dereference(y->right)->key)
    {
        x = y;
        y = rp_dereference(y->parent);
    }

    return y;
}
//***************************************
// O(N log(N)) traversal
void *rb_next_nln(rbtree_t *tree, long prev_key, long *key)
{
    static __thread char buff[1000];

    rbnode_t *bigger_node = NULL;
    rbnode_t *node;
    void *value;

    buff[0] = 0;

    read_lock(tree->lock);
    
    node = rp_dereference(tree->root);

    strcat(buff, "ROOT");

    while (node != NULL)
    {
        if (node->key == prev_key)
        {
            strcat(buff, " =right");
            node = rp_dereference(node->right);
        } 
        else if (node->key > prev_key)
        {
            strcat(buff, " left");
            bigger_node = node;
            node = rp_dereference(node->left);
        }
        else
        {
            strcat(buff, " right");
            node = rp_dereference(node->right);
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
        if (rbnode_invalid(node, depth)) 
            printf(" INVALID NODE: %d\n", rbnode_invalid(node, depth));
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
    printf("OUTPUT LIST\n");
    output_list(tree->root, 0);
    printf("\n");
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
