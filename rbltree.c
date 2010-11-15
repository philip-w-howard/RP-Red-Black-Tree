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

#define IMPLEMENTED     1

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
    if (node->parent->left == node)
        return 1;
    else
        return 0;
}
//*******************************
void rb_create(rbtree_t *tree, void *lock)
{
#ifdef MULTIWRITERS
    read_lock(lock);
#else
    write_lock(lock);
#endif
    tree->root = NULL;
    tree->restructure_copies = 0;
    tree->swap_copies= 0;
    tree->restructures = 0;
    tree->lock = lock;
#ifdef MULTIWRITERS
    read_unlock(lock);
#else
    write_unlock(lock);
#endif
}
//*******************************
static rbnode_t *find_leaf(rbnode_t *node, long key)
{
    rbnode_t *last = node;

	while (node != NULL && key != node->key)
	{
        last = node;

        if (key < node->key)
        {
            node = rcu_dereference(node->left);
        }
        else if (key > node->key)
        {
            node = rcu_dereference(node->right);
        }
    }

    //assert(node==NULL || (node->left==NULL && node->right==NULL));
    if (node==NULL) return last;
    return node;
}
//*******************************
void *rb_find(rbtree_t *tree, long key)
{
    void *value;

    key *= 10;

    read_lock(tree->lock);
	rbnode_t *node = find_leaf(tree->root, key);

    if (node != NULL && node->key==key)
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
static void close_up(rbtree_t *tree, rbnode_t *node);
//**********
static void restructure(rbtree_t *tree, rbnode_t *grandparent, rbnode_t *parent, rbnode_t *node,
                        rbnode_t **a, rbnode_t **b, rbnode_t **c)
{
    rbnode_t *aprime, *bprime, *cprime;
    rbnode_t *greatgrandparent = grandparent->parent;
    int left = 0;

    tree->restructures++;

    if (grandparent->parent != NULL) left = is_left(grandparent);
    //printf("restructure %s\n", toString(node));

    if (grandparent->left == parent && parent->left == node)
    {
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
        
        rcu_assign_pointer(bprime->right, cprime);
        cprime->parent = bprime;

        if (greatgrandparent != NULL)
        {
            if (left) {
                rcu_assign_pointer(greatgrandparent->left, bprime);
            } else {
                rcu_assign_pointer(greatgrandparent->right, bprime);
            }

            bprime->parent = greatgrandparent;
        } else {
            bprime->parent = NULL;
            rcu_assign_pointer(tree->root, bprime);
        }
#if defined(NO_GRACE_PERIOD) || defined(RCU)
        rcu_free(tree->lock, rbnode_free, grandparent);
#endif
    } 
    else if (grandparent->left == parent && parent->right == node)
    {
        // zig left
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

        rcu_assign_pointer(bprime->left, aprime);
        aprime->parent = bprime;

        bprime->right = cprime;
        cprime->parent = bprime;

        if (greatgrandparent != NULL)
        {
            if (left) {
                rcu_assign_pointer(greatgrandparent->left, bprime);
            } else {
                rcu_assign_pointer(greatgrandparent->right, bprime);
            }

            bprime->parent = greatgrandparent;
        } else {
            bprime->parent = NULL;
            rcu_assign_pointer(tree->root, bprime);
        }
#if defined(NO_GRACE_PERIOD) || defined(RCU)
        rcu_free(tree->lock, rbnode_free, parent);
        rcu_free(tree->lock, rbnode_free, grandparent);
#endif
        if (aprime->right == NULL) close_up(tree, aprime);
        if (cprime->left == NULL) close_up(tree, cprime);

    }
    else if (parent->right == node && grandparent->right == parent)
    {
        // diag right
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

        rcu_assign_pointer(bprime->left, aprime);
        aprime->parent = bprime;

        if (greatgrandparent != NULL)
        {
            if (left) {
                rcu_assign_pointer(greatgrandparent->left, bprime);
            } else {
                rcu_assign_pointer(greatgrandparent->right, bprime);
            }

            bprime->parent = greatgrandparent;
        } else {
            bprime->parent = NULL;
            rcu_assign_pointer(tree->root, bprime);
        }
#if defined(NO_GRACE_PERIOD) || defined(RCU)
        rcu_free(tree->lock, rbnode_free, grandparent);
#endif
    }
    else
    {
        // zig right
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

        rcu_assign_pointer(bprime->left, aprime);
        aprime->parent = bprime;

        bprime->right = cprime;
        cprime->parent = bprime;

        if (greatgrandparent != NULL)
        {
            if (left) {
                rcu_assign_pointer(greatgrandparent->left, bprime);
            } else {
                rcu_assign_pointer(greatgrandparent->right, bprime);
            }

            bprime->parent = greatgrandparent;
        } else {
            bprime->parent = NULL;
            rcu_assign_pointer(tree->root, bprime);
        }
#if defined(NO_GRACE_PERIOD) || defined(RCU)
        rcu_free(tree->lock, rbnode_free, parent);
        rcu_free(tree->lock, rbnode_free, grandparent);
#endif
        if (aprime->right == NULL) close_up(tree, aprime);
        if (cprime->left == NULL) close_up(tree, cprime);
    }

    *a = bprime->left;
    *b = bprime;
    *c = bprime->right;
}
//*******************************
static void recolor(rbtree_t *tree, rbnode_t *node)
{
    rbnode_t *parent = node->parent;
    rbnode_t *grandparent = parent->parent;
    rbnode_t *w;
    rbnode_t *a, *b, *c;

    if (grandparent == NULL)
    {
        // parent is the root, so just up the black count of the whole tree
        parent->color = BLACK;
    }
    else
    {
        w = sibling(parent);

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

            if (grandparent->parent!=NULL && grandparent->parent->color == RED)
            {
                recolor(tree, grandparent);
            }
        }
    }
}
//*******************************
int rb_insert(rbtree_t *tree, long key, void *value)
{
    rbnode_t *new_node;
    rbnode_t *node;

    key *= 10;

    //printf("rb_insert write_lock\n");
#ifdef MULTIWRITERS
    read_lock(tree->lock);
#else
    write_lock(tree->lock);
#endif

    //check_for(tree->root, new_node);

    node = find_leaf(tree->root, key);
	if (node == NULL)
	{
        new_node = rbnode_create(key, value);
        new_node->color = BLACK;
		rcu_assign_pointer(tree->root, new_node);
#ifdef MULTIWRITERS
        read_unlock(tree->lock);
#else
        write_unlock(tree->lock);
#endif
        return 1;
	} else {
        rbnode_t *new_sib;

        if (node->key == key)
        {
#ifdef MULTIWRITERS
            read_unlock(tree->lock);
#else
            write_unlock(tree->lock);
#endif
            return 0;
        }
        new_node = rbnode_create(key, value);
        new_sib = rbnode_copy(node);
        
        new_node->color = RED;
        new_sib->color = RED;

        new_node->parent = node;
        new_sib->parent = node;
        if (key < node->key)
        {
            node->key -= 5;
            rcu_assign_pointer(node->right, new_sib);
            rcu_assign_pointer(node->left,  new_node);
        }
        else
        {
            node->key += 5;
            rcu_assign_pointer(node->right, new_node);
            rcu_assign_pointer(node->left,  new_sib);
        }

        if (node->color == RED) 
        {
            rbnode_t *sib = sibling(node);
            assert(sib != NULL && sib->color==RED);
            assert(node->parent != NULL && node->parent->color == BLACK);
            node->color = BLACK;
            sib->color = BLACK;
            node->parent->color = RED;

            if (node->parent != NULL &&
                node->parent->parent != NULL && 
                node->parent->parent->color==RED)
            {
                recolor(tree, node->parent);
            }
        }
	}

    //printf("rb_insert write_unlock\n");
#ifdef MULTIWRITERS
    read_unlock(tree->lock);
#else
    write_unlock(tree->lock);
#endif

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
        restructure(tree, x,y,z, &a, &b, &c);

#ifdef RCU
		// in RCU version, x always gets replaced. Figure out if it's c or a
		if (c == z)
			x = a;
		else
			x = c;
#endif

		y->color = BLACK;
        x->color = RED;

        if (x->left == r)
            y = x->right;
        else
            y = x->left;

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
static void close_up(rbtree_t *tree, rbnode_t *node)
{
    rbnode_t *parent = node->parent;
    rbnode_t *child;

    assert(node->left==NULL || node->right==NULL);

    if (node->left == NULL)
        child = node->right;
    else
        child = node->left;

    if (parent == NULL)
    {
        tree->root = child;
        child->parent = NULL;
        child->color = BLACK;
    } else {
        if (is_left(node))
            parent->left = child;
        else
            parent->right = child;
        child->parent = parent;

        if (node->color==RED && child->color==BLACK)
        {
            // do nothing
        } else if (node->color==RED && child->color==RED) {
            // Can't happen
            assert(node->color!=RED || child->color != RED);
        } else if (node->color==BLACK && child->color==BLACK) {
            child->color = BLACK_BLACK;
            double_black(tree, child);
        } else { // if (node->color==BLACK && child->color==RED) 
            child->color = BLACK;
        }
    }

    rcu_free(tree->lock, rbnode_free, node);
}


//*******************************
void *rb_remove(rbtree_t *tree, long key)
{
	rbnode_t *node;
	rbnode_t *prev = NULL;
	rbnode_t *sib = NULL;
	//rbnode_t *swap = NULL;
	void *value = NULL;
    //int temp_color;

    key *= 10;

#ifdef MULTIWRITERS
    read_lock(tree->lock);
#else
    write_lock(tree->lock);
#endif

	node = find_leaf(tree->root, key);
    if (node == NULL || key != node->key) 
    {
        // not found
#ifdef MULTIWRITERS
        read_unlock(tree->lock);
#else
        write_unlock(tree->lock);
#endif
        return NULL;
    }

    // found it, so cut it out of the tree
    //******************* remove the node from the tree ***************

    // the node is a leaf
	assert (node->left == NULL && node->right == NULL);

    prev = node->parent;

    if (prev == NULL)
    {
        tree->root = NULL;
    } else if (prev->parent == NULL) {
        sib  = sibling(node);
        tree->root = sib;
        sib->parent = NULL;
    } else {
        if (is_left(node))
            prev->left = NULL;
        else
            prev->right = NULL;
        
        close_up(tree, prev);
    }

    // save value, free node, return value
    value = node->value;

    rcu_free(tree->lock, rbnode_free, node);

    //printf("rb_remove write_unlock\n");
#ifdef MULTIWRITERS
    read_unlock(tree->lock);
#else
    write_unlock(tree->lock);
#endif
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
        if (rbnode_invalid(node, depth) ||
            ( (node->left==NULL) ^ (node->right==NULL)) )
        {
            printf(" INVALID NODE: %d\n", rbnode_invalid(node, depth));
        }
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

    if ( (node->left==NULL) ^ (node->right==NULL)) return 5;

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
    if (node->left == NULL || node->right==NULL) size = 1;
    size += rbn_size(node->left);
    size += rbn_size(node->right);

    return size;
}
//****************************************
int rb_size(rbtree_t *tree)
{
    return rbn_size(tree->root);
}
