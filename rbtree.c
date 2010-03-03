#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "rbnode.h"
#include "rbtree.h"
#include "lock.h"
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
    assert(node != NULL);

    if (node->parent->left == node)
        return 1;
    else
        return 0;
}
//*******************************
void rb_create(rbtree_t *tree, void *lock)
{
    //printf("rb_create write_lock\n");
    write_lock(lock);
    tree->root = NULL;
    tree->restructure_copies = 0;
    tree->swap_copies= 0;
    tree->restructures = 0;
    tree->lock = lock;
    //printf("rb_create write_unlock\n");
    write_unlock(lock);
}
//*******************************
static rbnode_t *find_node(rbtree_t *tree, unsigned long key)
{
	rbnode_t *node = rcu_dereference(tree->root);

	while (node != NULL)
	{
		if (key == node->key) 
            return node;
		else if (key < node->key) 
            node = rcu_dereference(node->left);
		else 
            node = rcu_dereference(node->right);
	}

	return NULL;
}
//*******************************
void *rb_find(rbtree_t *tree, unsigned long key)
{
    void *value;

    //printf("rb_find read_lock\n");
    read_lock(tree->lock);
	rbnode_t *node = find_node(tree, key);

    if (node != NULL) 
        value = node->value;
    else
        value = NULL;

    //printf("rb_find read_unlock\n");
    read_unlock(tree->lock);

    return value;
}
//*******************************
static rbnode_t *sibling(rbnode_t *node)
{
    assert( (node->parent != NULL) );

    if (node->parent->left == node)
        return node->parent->right;
    else
        return node->parent->left;
}
//*******************************
static void restructure(rbtree_t *tree, rbnode_t *grandparent, rbnode_t *parent, rbnode_t *node,
                        rbnode_t **a, rbnode_t **b, rbnode_t **c)
{
#ifdef RCU
    rbnode_t *greatgrandparent = grandparent->parent;
    int left = 0;
    if (grandparent->parent != NULL) left = is_left(grandparent);
    //printf("restructure %s\n", toString(node));

    tree->restructures++;

    if (grandparent->left == parent && parent->left == node)
    {
        // diag left
		rbnode_t *cprime;
		cprime = rbnode_copy(grandparent);

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
		
		rcu_free(tree->lock, grandparent);
		
    } 
    else if (grandparent->left == parent && parent->right == node)
    {
        // zig left
		rbnode_t *bprime = rbnode_copy(node);
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
		rcu_free(tree->lock, node);
	}
    else if (parent->right == node && grandparent->right == parent)
    {
        // diag right
		rbnode_t *aprime = rbnode_copy(grandparent);
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
		rcu_free(tree->lock, grandparent);
    }
    else
    {
        // zig right
		rbnode_t *bprime = rbnode_copy(node);
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
		rcu_free(tree->lock, node);
   }
#else
    rbnode_t *greatgrandparent = grandparent->parent;
    int left = 0;
    if (grandparent->parent != NULL) left = is_left(grandparent);
    //printf("restructure %s\n", toString(node));

	tree->restructure_copies++;
    if (grandparent->left == parent && parent->left == node)
    {
        // diag left
        rcu_assign_pointer(grandparent->left, parent->right);
        if (parent->right != NULL) parent->right->parent = grandparent;
        
        rcu_assign_pointer(parent->right, grandparent);
        grandparent->parent = parent;

        *a = node;
        *b = parent;
        *c = grandparent;
    } 
    else if (grandparent->left == parent && parent->right == node)
    {
        // zig left
        rcu_assign_pointer(parent->right, node->left);
        if (node->left != NULL) node->left->parent = parent;

        rcu_assign_pointer(grandparent->left, node->right);
        if (node->right != NULL) node->right->parent = grandparent;

        rcu_assign_pointer(node->left, parent);
        parent->parent = node;

        rcu_assign_pointer(node->right, grandparent);
        grandparent->parent = node;

        *a = parent;
        *b = node;
        *c = grandparent;
    }
    else if (parent->right == node && grandparent->right == parent)
    {
        // diag right
        rcu_assign_pointer(grandparent->right, parent->left);
        if (parent->left != NULL) parent->left->parent = grandparent;

        rcu_assign_pointer(parent->left, grandparent);
        grandparent->parent = parent;

        *a = grandparent;
        *b = parent;
        *c = node;
    }
    else
    {
        // zig right
        rcu_assign_pointer(grandparent->right, node->left);
        if (node->left != NULL) node->left->parent = grandparent;

        rcu_assign_pointer(parent->left, node->right);
        if (node->right != NULL) node->right->parent = parent;

        rcu_assign_pointer(node->left, grandparent);
        grandparent->parent = node;

        rcu_assign_pointer(node->right, parent);
        parent->parent = node;

        *a = grandparent;
        *b = node;
        *c = parent;
    }

    if (greatgrandparent != NULL)
    {
        if (left) {
            rcu_assign_pointer(greatgrandparent->left, *b);
        } else {
            rcu_assign_pointer(greatgrandparent->right, *b);
		}

        (*b)->parent = greatgrandparent;
    } else {
        (*b)->parent = NULL;
        rcu_assign_pointer(tree->root, (*b));
    }
#endif
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
        if (grandparent->parent != NULL) grandparent->color = RED;
        if (grandparent->parent != NULL && grandparent->parent->color == RED)
        {
            recolor(tree, grandparent);
        }
    }
}
//*******************************
void rb_insert(rbtree_t *tree, unsigned long key, void *value)
{
    rbnode_t *new_node = rbnode_create(key, value);

    //printf("rb_insert write_lock\n");
    write_lock(tree->lock);

	if (tree->root == NULL)
	{
        new_node->color = BLACK;
		rcu_assign_pointer(tree->root, new_node);
	} else {
		rbnode_t *node = tree->root;
		rbnode_t *prev = node;

		while (node != NULL)
		{
			prev = node;
			if (key <= node->key) 
				node = node->left;
			else 
				node = node->right;
		}

        new_node->color = RED;
        new_node->parent = prev;
        if (key <= prev->key) {
			rcu_assign_pointer(prev->left, new_node);
		} else  {
			rcu_assign_pointer(prev->right, new_node);
		}

        if (prev->color == RED) recolor(tree, new_node);
	}

    //printf("rb_insert write_unlock\n");
    write_unlock(tree->lock);
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
static void double_black(rbtree_t *tree, rbnode_t *r);
static void double_black_node(rbtree_t *tree, rbnode_t *x, rbnode_t *y, rbnode_t *r)
{
    rbnode_t *z;
    rbnode_t *a, *b, *c;

    assert(y != NULL);
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
void *rb_remove(rbtree_t *tree, unsigned long key)
{
	rbnode_t *node = tree->root;
	rbnode_t *prev = NULL;
	rbnode_t *next = NULL;
	rbnode_t *swap = NULL;
#ifndef RCU
	rbnode_t *temp = NULL;
#endif
	void *value = NULL;
    int temp_color;

    //printf("rb_remove write_lock\n");
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
    //******************* swap with external node if necessary ****************************
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
            
#ifdef RCU
            // make sure all readers have seen the new location of swap
            // before hiding it's old location
            rcu_synchronize(tree->lock);
#endif
            
			rcu_assign_pointer(node->right, swap->right);
            if (swap->right != NULL) swap->right->parent = node;

            rcu_assign_pointer(swap->right, node);
            node->parent = swap;

            rcu_assign_pointer(node->left, NULL);   // swap->left guaranteed to be NULL
        } else {
#ifdef RCU
            // exchange children of swap and node
			rbnode_t *new_node = rbnode_copy(swap);
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

            // node is now temporarily out of the tree, but it's being
            // deleted anyway, so that's OK

            // need to make sure bprime is seen before path to node's 
            // children is erased
            rcu_synchronize(tree->lock);

            rcu_assign_pointer(node->right, swap->right);
            if (swap->right != NULL) swap->right->parent = node;

            rcu_assign_pointer(node->left, NULL);

            rcu_assign_pointer(swap->parent->left, node);
			node->parent = swap->parent;
			
			rcu_free(tree->lock, swap);
#else
            // exchange children of swap and node
            rcu_assign_pointer(swap->left, node->left);
            node->left->parent = swap;      // safe: checked above
            rcu_assign_pointer(node->left, NULL);    // swap->left guaranteed to be NULL

            temp = swap->right;
            rcu_assign_pointer(swap->right, node->right);
            node->right->parent = swap;     // safe: checked above

            rcu_assign_pointer(node->right, temp);
            if (temp != NULL) temp->parent = node;

			tree->swap_copies++;
            temp = swap->parent;
            if (prev == NULL)
            {
                rcu_assign_pointer(tree->root, swap);
                swap->parent = prev;
            } else {
                if (is_left(node)) {
                    rcu_assign_pointer(prev->left, swap);
                } else {
                    rcu_assign_pointer(prev->right, swap);
				}
                swap->parent = prev;
            }

            if (temp == node)
            {
                rcu_assign_pointer(swap->right, node);
                node->parent = swap;
            } else {
                rcu_assign_pointer(temp->left, node);          // swap is guaranteed on left
                node->parent = temp;
            }
#endif
        }
    }

    //******************************** do the remove ***************************
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
			rcu_assign_pointer(prev->left, next);
        } else {
            rcu_assign_pointer(prev->right, next);
        }
        if (next != NULL) next->parent = prev;
    } else {
		rcu_assign_pointer(tree->root, next);
		if (next != NULL) next->parent = NULL;
	}

    //******************** rebalance *******************
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

    // Has a grace period already expired? Can we safely just free?
    rcu_free(tree->lock, node);

    //printf("rb_remove write_unlock\n");
    write_unlock(tree->lock);
	return value;
}
//**************************************
static void output_list(rbnode_t *node, int depth)
{
    if (node != NULL)
    {
        output_list(node->left, depth+1);
        printf("depth: %d value: %s", depth, toString(node));
        printf(" l: %s", toString(node->left));
        printf(" r: %s\n", toString(node->right));
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
