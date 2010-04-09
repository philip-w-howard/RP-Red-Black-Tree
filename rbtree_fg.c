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

#include "rbnode_fg.h"
#include "rbtree.h"
#include "lock.h"
#include "rcu.h"

static int IMPLEMENTED = 0;

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
// Requires node and parent to be locked
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
    tree->swap_copies= 0;
    tree->restructures = 0;
    tree->lock = lock;
    write_unlock(lock);
}
//*******************************
static rbnode_t *find_node(rbtree_t *tree, long key)
{
	rbnode_t *node;
	rbnode_t *new_node;
        
    //printf("find_node read_lock\n");
    read_lock(tree->lock);
    node = rcu_dereference(tree->root);
    read_lock(node->lock);
    //printf("find_node read_unlock\n");
    read_unlock(tree->lock);

	while (node != NULL && key != node->key)
	{
		if (key < node->key) 
            new_node = rcu_dereference(node->left);
		else 
            new_node = rcu_dereference(node->right);

        if (new_node != NULL) read_lock(new_node->lock);
        read_unlock(node->lock);
        node = new_node;
	}

	return node;
}
//*******************************
void *rb_find(rbtree_t *tree, long key)
{
    void *value;

    //printf("rb_find read_lock\n");
    //read_lock(tree->lock);
	rbnode_t *node = find_node(tree, key);

    if (node != NULL) 
    {
        value = node->value;
        read_unlock(node->lock);
    }
    else
    {
        value = NULL;
    }

    //printf("rb_find read_unlock\n");
    //read_unlock(tree->lock);

    return value;
}
//*******************************
// requires node and parent be locked
static rbnode_t *sibling(rbnode_t *node)
{
    if (node->parent->left == node)
        return node->parent->right;
    else
        return node->parent->left;
}
//*******************************
#ifdef RCU
static void restructure(rbtree_t *tree, rbnode_t *grandparent, rbnode_t *parent, rbnode_t *node,
                        rbnode_t **a, rbnode_t **b, rbnode_t **c)
{
    rbnode_t *greatgrandparent = grandparent->parent;
    int left = 0;

    assert(IMPLEMENTED);
    if (grandparent->parent != NULL) left = is_left(grandparent);

    tree->restructures++;

    if (grandparent->left == parent && parent->left == node)
    {
        // diag left
		rbnode_t *cprime;
		cprime = rbnode_copy(grandparent);
        //check_for(tree->root, cprime);

		tree->restructure_copies++;

        rcu_assign_pointer(cprime->left, parent->right);
        if (parent->right != NULL) parent->right->parent = cprime;
        
        rcu_assign_pointer(parent->right, cprime);
        cprime->parent = parent;

		if (greatgrandparent != NULL)
		{
			if (left) 
				rcu_assign_pointer(greatgrandparent->left, parent);
			else
				rcu_assign_pointer(greatgrandparent->right, parent);

			parent->parent = greatgrandparent;
		} else {
			parent->parent = NULL;
			rcu_assign_pointer(tree->root, parent);
		}

        *a = node;
        *b = parent;
        *c = cprime;
		
        rcu_free(tree->lock, rbnode_free, grandparent);
    } 
    else if (grandparent->left == parent && parent->right == node)
    {
        // zig left
		rbnode_t *bprime = rbnode_copy(node);
        //check_for(tree->root, bprime);
		tree->restructure_copies++;
		
		rcu_assign_pointer(bprime->left, parent);
		parent->parent = bprime;

		rcu_assign_pointer(bprime->right, grandparent);
		grandparent->parent = bprime;

 		if (greatgrandparent != NULL)
		{
			if (left) 
				rcu_assign_pointer(greatgrandparent->left, bprime);
			else
				rcu_assign_pointer(greatgrandparent->right, bprime);

			bprime->parent = greatgrandparent;
		} else {
			bprime->parent = NULL;
			rcu_assign_pointer(tree->root, bprime);
		}
        
        // Make sure all readers have seen bprime before hiding path to B
        rcu_synchronize(tree->lock);

        rcu_assign_pointer(grandparent->left, node->right);
        if (node->right != NULL) node->right->parent = grandparent;

        rcu_assign_pointer(parent->right, node->left);
        if (node->left != NULL) node->left->parent = parent;

		*a = parent;
        *b = bprime;
        *c = grandparent;
        rcu_free(tree->lock, rbnode_free, node);
	}
    else if (parent->right == node && grandparent->right == parent)
    {
        // diag right
		rbnode_t *aprime = rbnode_copy(grandparent);
        //check_for(tree->root, aprime);
		tree->restructure_copies++;

        rcu_assign_pointer(aprime->right, parent->left);
        if (parent->left != NULL) parent->left->parent = aprime;

        rcu_assign_pointer(parent->left, aprime);
        aprime->parent = parent;

		if (greatgrandparent != NULL)
		{
			if (left) 
				rcu_assign_pointer(greatgrandparent->left, parent);
			else
				rcu_assign_pointer(greatgrandparent->right, parent);

			parent->parent = greatgrandparent;
		} else {
			parent->parent = NULL;
			rcu_assign_pointer(tree->root, parent);
		}

        *a = aprime;
        *b = parent;
        *c = node;
        rcu_free(tree->lock, rbnode_free, grandparent);
    }
    else
    {
        // zig right
		rbnode_t *bprime = rbnode_copy(node);
        //check_for(tree->root, bprime);
		tree->restructure_copies++;

        rcu_assign_pointer(bprime->left, grandparent);
        grandparent->parent = bprime;

        rcu_assign_pointer(bprime->right, parent);
        parent->parent = bprime;

		if (greatgrandparent != NULL)
		{
			if (left) 
				rcu_assign_pointer(greatgrandparent->left, bprime);
			else
				rcu_assign_pointer(greatgrandparent->right, bprime);

			bprime->parent = greatgrandparent;
		} else {
			bprime->parent = NULL;
			rcu_assign_pointer(tree->root, bprime);
		}

        // Make sure all readers have seen bprime before hiding path to B
        rcu_synchronize(tree->lock);

        rcu_assign_pointer(grandparent->right, node->left);
        if (node->left != NULL) node->left->parent = grandparent;

        rcu_assign_pointer(parent->left, node->right);
        if (node->right != NULL) node->right->parent = parent;

        *a = grandparent;
        *b = bprime;
        *c = parent;
        rcu_free(tree->lock, rbnode_free, node);
   }
}
#else // non-RP restructure
//*******************************
// Requires grandparent, parent, and node to be write_locked
// Locks are still held on return
static void restructure(rbtree_t *tree, 
                    rbnode_t *grandparent, rbnode_t *parent, rbnode_t *node,
                    rbnode_t **a, rbnode_t **b, rbnode_t **c)
{
    rbnode_t *greatgrandparent = grandparent->parent;
    int left = 0;
    if (greatgrandparent != NULL) 
    {
        // 'left' isn't needed until the end, but we need to compute it
        // before the structure of the tree changes
        printf("rest read lock %ld\n", greatgrandparent->key);
        read_lock(greatgrandparent->lock);
        left = is_left(grandparent);
    }

	tree->restructure_copies++;
    if (grandparent->left == parent && parent->left == node)
    {
        // diag left
        grandparent->left = parent->right;
        if (parent->right != NULL) parent->right->parent = grandparent;
        
        parent->right = grandparent;
        grandparent->parent = parent;

        *a = node;
        *b = parent;
        *c = grandparent;
    } 
    else if (grandparent->left == parent && parent->right == node)
    {
        // zig left
        parent->right = node->left;
        if (node->left != NULL) node->left->parent = parent;

        grandparent->left = node->right;
        if (node->right != NULL) node->right->parent = grandparent;

        node->left = parent;
        parent->parent = node;

        node->right = grandparent;
        grandparent->parent = node;

        *a = parent;
        *b = node;
        *c = grandparent;
    }
    else if (parent->right == node && grandparent->right == parent)
    {
        // diag right
        grandparent->right = parent->left;
        if (parent->left != NULL) parent->left->parent = grandparent;

        parent->left = grandparent;
        grandparent->parent = parent;

        *a = grandparent;
        *b = parent;
        *c = node;
    }
    else
    {
        // zig right
        grandparent->right = node->left;
        if (node->left != NULL) node->left->parent = grandparent;

        parent->left = node->right;
        if (node->right != NULL) node->right->parent = parent;

        node->left = grandparent;
        grandparent->parent = node;

        node->right = parent;
        parent->parent = node;

        *a = grandparent;
        *b = node;
        *c = parent;
    }

    if (greatgrandparent != NULL)
    {
        // DEADLOCK possible
        printf("rest upgrade lock %ld\n", greatgrandparent->key);
        upgrade_lock(greatgrandparent->lock);
        if (left) {
            greatgrandparent->left = *b;
        } else {
            greatgrandparent->right = *b;
		}

        (*b)->parent = greatgrandparent;
        printf("rest write UNlock %ld\n", greatgrandparent->key);
        write_unlock(greatgrandparent->lock);
    } else {
        (*b)->parent = NULL;
        // DEADLOCK possible
        printf("rest write lock tree\n");
        write_lock(tree->lock);
        tree->root = *b;
        printf("rest write UNlock tree\n");
        write_unlock(tree->lock);
    }
}
#endif
//*******************************
// assumes node is write locked
static void recolor(rbtree_t *tree, rbnode_t *node)
{
    rbnode_t *parent;
    rbnode_t *grandparent;
    rbnode_t *w;
    rbnode_t *a, *b, *c;

    parent = node->parent;
    printf("read lock %ld\n", parent->key);
    read_lock(parent->lock);
    grandparent = parent->parent;

    // DEADLOCK possible
    printf("write_lock %ld\n", grandparent->key);
    write_lock(grandparent->lock);

    // deadlock free because grandparent is write_locked
    printf("upgrade lock %ld\n", parent->key);
    upgrade_lock(parent->lock);

    // parent and grandparent are locked
    w = sibling(parent);

    if (w != NULL) 
    {
        printf("read lock %ld\n", w->key);
        read_lock(w->lock);
    }

    if (w == NULL || w->color == BLACK)
    {
        // case 1
        printf("restructure\n");
        restructure(tree, grandparent, parent, node, &a, &b, &c);

        a->color = RED;
        b->color = BLACK;
        c->color = RED;

        printf("write UNlock %ld\n", node->key);
        write_unlock(node->lock);
        printf("write UNlock %ld\n", parent->key);
        write_unlock(parent->lock);
        printf("write UNlock %ld\n", grandparent->key);
        write_unlock(grandparent->lock);
        if (w != NULL) 
        {
            printf("read UNlock %ld\n", w->key);
            read_unlock(w->lock);
        }
    } else if ( w!=NULL && w->color == RED) {
        // case 2
        printf("upgrade lock %ld\n", w->key);
        upgrade_lock(w->lock);
        w->color = BLACK;
        parent->color = BLACK;
        if (grandparent->parent != NULL) grandparent->color = RED;
        printf("write UNlock %ld\n", w->key);
        write_unlock(w->lock);
        printf("write UNlock %ld\n", node->key);
        write_unlock(node->lock);
        printf("write UNlock %ld\n", parent->key);
        write_unlock(parent->lock);

        if (grandparent->parent != NULL)
        {
            printf("read lock %ld\n", grandparent->parent->key);
            read_lock(grandparent->parent->lock);
            if (grandparent->parent->color == RED)
            {
                printf("read UNlock %ld\n", grandparent->parent->key);
                read_unlock(grandparent->parent->lock);
                printf("recolor %ld\n", grandparent->key);
                recolor(tree, grandparent);
            } else {
                printf("read UNlock %ld\n", grandparent->parent->key);
                read_unlock(grandparent->parent->lock);
                printf("write UNlock %ld\n", grandparent->key);
                write_unlock(grandparent->lock);
            }
        } else {
            printf("write UNlock %ld\n", grandparent->key);
            write_unlock(grandparent->lock);
        }
    }
}
//*******************************
int rb_insert(rbtree_t *tree, long key, void *value)
{
    rbnode_t *new_node;

    new_node = rbnode_create(key, value);

	if (tree->root == NULL)
	{
        printf("write lock tree\n");
        write_lock(tree->lock);
        new_node->color = BLACK;
		rcu_assign_pointer(tree->root, new_node);
        printf("write UNlock tree\n");
        write_unlock(tree->lock);
        return 1;
	} else {
		rbnode_t *node;
		rbnode_t *prev = NULL;

        printf("read lock tree\n");
        read_lock(tree->lock);
		node = tree->root;
        printf("read lock %ld\n", node->key);
        read_lock(node->lock);
        printf("read UNlock tree\n");
        read_unlock(tree->lock);

		while (node != NULL)
		{
			prev = node;
            if (key == node->key)
            {
                rbnode_free(new_node);
                printf("not found read unlock %ld\n", node->key);
                read_unlock(node->lock);
                return 0;
            }
            else if (key <= node->key) 
				node = node->left;
			else 
				node = node->right;

            if (node != NULL) 
            {
                printf("read lock %ld\n", node->key);
                read_lock(node->lock);
                printf("read UNlock %ld\n", prev->key);
                read_unlock(prev->lock);
            }
		}

        // loop is exited with prev being read_locked
        
        printf("upgrade lock %ld\n", prev->key);
        upgrade_lock(prev->lock);
        printf("write lock %ld\n", new_node->key);
        write_lock(new_node->lock);

        new_node->color = RED;
        new_node->parent = prev;
        if (key <= prev->key) {
			rcu_assign_pointer(prev->left, new_node);
		} else  {
			rcu_assign_pointer(prev->right, new_node);
		}

        printf("write UNlock %ld\n", prev->key);
        write_unlock(prev->lock);
        if (prev->color == RED) 
        {
            printf("recolor %ld\n", new_node->key);
            recolor(tree, new_node);
        } else {
            printf("write UNLOCK %ld\n", new_node->key);
            write_unlock(new_node->lock);
        }
	}

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
// requires x,y,r to be write locked
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
            write_lock(z->lock);
            restructure(tree, x, y, z, &a, &b, &c);
            b->color = x_color;
            a->color = BLACK;
            c->color = BLACK;
            if (r != NULL) r->color = BLACK;
            write_unlock(x->lock);
            write_unlock(y->lock);
            write_unlock(z->lock);
            if (r != NULL) write_unlock(r->lock);
            return;
        } else {
            // case 2
            if (r != NULL) 
            {
                write_unlock(r->lock);
                r->color = BLACK;
            }
            y->color = RED;
            write_unlock(y->lock);
            if (x->color == RED)
            {
                x->color = BLACK;
                write_unlock(x->lock);
            }
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
        // x is parent of y and is already locked
        if (is_left(y))
            z = y->left;
        else 
            z = y->right;

        // should be deadlock free because we already have y locked
        if (z != NULL) write_lock(z->lock);
        restructure(tree, x,y,z, &a, &b, &c);

#ifdef RCU
		// in RCU version, x always gets replaced. Figure out if it's c or a
        write_unlock(x->lock);
		if (c == z)
			x = a;
		else
			x = c;

        // DEADLOCK: x is a new node, but a reader may have already found it
        write_lock(x->lock);
#endif
        write_unlock(z->lock);

		y->color = BLACK;
        write_unlock(y->lock);

        x->color = RED;

        if (x->left == r)
            y = x->right;
        else
            y = x->left;

        write_lock(y->lock);
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
void *rb_remove(rbtree_t *tree, long key)
{
	rbnode_t *node;
	rbnode_t *prev = NULL;
	rbnode_t *next = NULL;
	rbnode_t *swap = NULL;
	void *value = NULL;
    int temp_color;

    //printf("rb_remove write_lock\n");
	node = find_node(tree, key);

    // not found
	if (node == NULL) 
    {
        //printf("rb_remove not found write_unlock\n");
        return NULL;
    }

    assert(IMPLEMENTED);

    prev = node->parent;

    // found it, so cut it out of the tree
    //******************* swap with external node if necessary ***************
	if (node->left != NULL && node->right != NULL)
    {
        // need to do a swap with leftmost on right branch
        swap = leftmost(node->right);
        temp_color = swap->color;
        swap->color = node->color;
        node->color = temp_color;

        if (swap == node->right)
        {
            rcu_assign_pointer(swap->left, node->left);
            node->left->parent = swap;      // safe: checked above

            if (prev == NULL) {
                rcu_assign_pointer(tree->root, swap);
			} else {
                if (prev->left == node) {
                    rcu_assign_pointer(prev->left, swap);
                } else {
                    rcu_assign_pointer(prev->right, swap);
				}
			}
            swap->parent = prev;

            prev = swap;
            next = swap->right;
        } else {
#ifdef RCU
            assert(IMPLEMENTED);

            // exchange children of swap and node
			rbnode_t *new_node = rbnode_copy(swap);
            //check_for(tree->root, new_node);
			tree->swap_copies++;

            rcu_assign_pointer(new_node->left, node->left);
            node->left->parent = new_node;      // safe: checked above

            rcu_assign_pointer(new_node->right, node->right);
            node->right->parent = new_node;     // safe: checked above

            if (prev == NULL)
            {
                rcu_assign_pointer(tree->root, new_node);
                new_node->parent = prev;
            } else {
                if (is_left(node))
                    rcu_assign_pointer(prev->left, new_node);
                else 
                    rcu_assign_pointer(prev->right, new_node);
                new_node->parent = prev;
            }

            // need to make sure bprime is seen before path to b is erased 
            rcu_synchronize(tree->lock);

            prev = swap->parent;
            next = swap->right;

            rcu_assign_pointer(prev->left, swap->right);
            if (swap->right != NULL) swap->right->parent = prev;

			rcu_free(tree->lock, rbnode_free, swap);
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
                assert(IMPLEMENTED);
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
            // prev should already be write locked
		    if (is_left(node))
            {
			    rcu_assign_pointer(prev->left, next);
            } else {
                rcu_assign_pointer(prev->right, next);
            }
            if (next != NULL) next->parent = prev;
        } else {
		    rcu_assign_pointer(tree->root, next);
		    if (next != NULL) next->parent = NULL;
	    }
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

    rcu_free(tree->lock, rbnode_free, node);

    //printf("rb_remove write_unlock\n");
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

    //printf("rb_first ************************* read_lock\n");
    read_lock(tree->lock);

    node = leftmost(tree->root);
    if (node != NULL)
    {
        *key = node->key;
        value = node->value;
    }

    //printf("rb_first ************************* read_unlock\n");
    read_unlock(tree->lock);

    return value;
}
//***************************************
void *rb_last(rbtree_t *tree, long *key)
{
    rbnode_t *node;
    void *value = NULL;

    //printf("rb_last ************************* read_lock\n");
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

    //printf("rb_next ************************* read_lock\n");
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

    //printf("rb_next ************************* read_unlock\n");
    read_unlock(tree->lock);

    return value;
}
//***************************************
void *rb_old_next(rbtree_t *tree, long prev_key, long *key)
{
    rbnode_t *node;
    void *value;

    assert(0);
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
