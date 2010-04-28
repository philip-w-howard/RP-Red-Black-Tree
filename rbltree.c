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
    write_lock(lock);
    tree->root = NULL;
    tree->restructure_copies = 0;
    tree->swap_copies= 0;
    tree->restructures = 0;
    tree->lock = lock;
    write_unlock(lock);
}
//*******************************
static rbnode_t *find_leaf(rbnode_t *node, long key)
{
    rbnode_t *last = node;
    rbnode_t *temp;

	while (node != NULL)
	{
        last = node;

        if (node->left==NULL && node->right==NULL)
            return node;
        else if (key < node->key)
        {
            if (node->left == NULL) return node;
            node = rcu_dereference(node->left);
        }
        else if (key > node->key)
        {
            if (node->right == NULL) return node;
            node = rcu_dereference(node->right);
        }
        else // (key == node->key)
        {
            if ((temp=node->left) != NULL && temp->key == key)
                node = rcu_dereference(temp);
            else if ( (temp=node->right) != NULL && temp->key == key)
                node = rcu_dereference(temp);
            else 
            {
                temp = node->left;
                if (temp != NULL)
                {
                    temp=find_leaf(temp, key);

                    if (temp!=NULL && temp->key==key) return temp;
                }

                temp = node->right;
                if (temp != NULL)
                {
                    temp = find_leaf(node->right, key);
                    if (temp != NULL) return temp;
                }

                return node;
            }
        }
    }

    return last;
}
//*******************************
rbnode_t *old_find_leaf(rbnode_t *node, long key)
{
	while (node != NULL && node->left != NULL)
	{
		if (key == node->key) 
        {
            if (node->left->key == key)
                node = rcu_dereference(node->left);
            else if (node->right != NULL && node->right->key == key)
                node = rcu_dereference(node->right);
            else 
            {
                rbnode_t *temp;
                temp=find_leaf(node->left, key);

                if (temp!=NULL && temp->key==key)
                    node = temp;
                else
                {
                    if (node->right == NULL) return node;

                    temp = find_leaf(node->right, key);
                    if (temp != NULL)
                        return temp;
                    else
                        return node;
                }
            }
        }
        else if (key < node->key) 
            node = rcu_dereference(node->left);
		else 
            node = rcu_dereference(node->right);
	}

    return node;
}
//*******************************
void *rb_find(rbtree_t *tree, long key)
{
    void *value;

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
        
        rcu_free(tree->lock, rbnode_free, node);

        // Make sure all readers have seen bprime before hiding path to B
        rcu_synchronize(tree->lock);

        grandparent->left = node->right;
        if (grandparent->left == NULL)
        {
            // close up intermediate node
            close_up(tree, grandparent);
        }
        else
        {
            grandparent->left->parent = grandparent;
        }

        parent->right = node->left;
        if (node->left == NULL)
        {
            // close up intermediate node
            close_up(tree, parent);
        }
        else
        {
            parent->right->parent = parent;
        }

        *a = bprime->left;
        *b = bprime;
        *c = bprime->right;
        /*
        rcu_assign_pointer(grandparent->left, node->right);
        if (node->right != NULL) node->right->parent = grandparent;

        rcu_assign_pointer(parent->right, node->left);
        if (node->left != NULL) node->left->parent = parent;

		*a = parent;
        *b = bprime;
        *c = grandparent;
        rcu_free(tree->lock, rbnode_free, node);
        */
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

        rcu_free(tree->lock, rbnode_free, node);

        // Make sure all readers have seen bprime before hiding path to B
        rcu_synchronize(tree->lock);

        parent->left = node->right;
        if (parent->left == NULL)
        {
            // close up internal node
            close_up(tree, parent);
        } else {
            parent->left->parent = parent;
        }

        grandparent->right = node->left;
        if (node->left == NULL)
        {
            // close up internal node
            close_up(tree, grandparent);
        } else {
            grandparent->right->parent = grandparent;
        }

        *a = bprime->left;
        *b = bprime;
        *c = bprime->right;

        /*
        FIX THIS: possibly create invalid leaf
        rcu_assign_pointer(grandparent->right, node->left);
        if (node->left != NULL) node->left->parent = grandparent;

        rcu_assign_pointer(parent->left, node->right);
        if (node->right != NULL) node->right->parent = parent;

        *a = grandparent;
        *b = bprime;
        *c = parent;
        rcu_free(tree->lock, rbnode_free, node);
        */
   }
#else
    rbnode_t *greatgrandparent = grandparent->parent;
    int left = 0;
    if (grandparent->parent != NULL) left = is_left(grandparent);
    //printf("restructure %s\n", toString(node));

    assert(!IMPLEMENTED);

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
        if (left) {
            greatgrandparent->left = *b;
        } else {
            greatgrandparent->right = *b;
		}

        (*b)->parent = greatgrandparent;
    } else {
        (*b)->parent = NULL;
        tree->root = *b;
    }
#endif
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

    //printf("rb_insert write_lock\n");
    write_lock(tree->lock);

    new_node = rbnode_create(key, value);

    //check_for(tree->root, new_node);

    node = find_leaf(tree->root, key);
	if (node == NULL)
	{
        new_node->color = BLACK;
		rcu_assign_pointer(tree->root, new_node);
        write_unlock(tree->lock);
        return 1;
	} else {
        rbnode_t *new_sib = rbnode_copy(node);
        
        new_node->color = RED;
        new_sib->color = RED;

        new_node->parent = node;
        new_sib->parent = node;
        if (key < node->key)
        {
            rcu_assign_pointer(node->right, new_sib);
            rcu_assign_pointer(node->left,  new_node);
        }
        else
        {
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

    write_lock(tree->lock);

	node = find_leaf(tree->root, key);
    if (node == NULL || key != node->key) 
    {
        // not found
        write_unlock(tree->lock);
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
