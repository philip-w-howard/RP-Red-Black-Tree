//******************************************************
// 
// Red/Black Tree implementation by Phil Howard
//
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "rbnode.h"
#include "rbtree.h"
//#include "lock.h"
#include "rcu.h"
#include <stm.h>

#define RB_START_RO_TX()   BEGIN_TRANSACTION
#define RB_START_TX()      BEGIN_TRANSACTION
#define RB_COMMIT()        END_TRANSACTION
#define LOAD(a)            ((typeof((a)) )wlpdstm_read_word((Word *)&(a)))
#define STORE(a,b)         wlpdstm_write_word((Word *)&(a), (Word)(b))

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
    if (LOAD(LOAD(node->parent)->left) == node)
        return 1;
    else
        return 0;
}
//*******************************
void rb_create(rbtree_t *tree, void *lock)
{
    RB_START_TX();

    STORE(tree->root, NULL);
    STORE(tree->restructure_copies, 0);
    STORE(tree->restructure_multi_copies, 0);
    STORE(tree->swap_copies, 0);
    STORE(tree->restructures, 0);
    STORE(tree->grace_periods, 0);
    STORE(tree->lock, lock);

    RB_COMMIT();
}
//*******************************
static rbnode_t *find_node(rbtree_t *tree, long key)
{
	rbnode_t *node = LOAD(tree->root);

	while (node != NULL && key != LOAD(node->key))
	{
		if (key < LOAD(node->key)) 
            node = LOAD(node->left);
		else 
            node = LOAD(node->right);
	}

	return node;
}
//*******************************
void *rb_find(rbtree_t *tree, long key)
{
    void *value;

    RB_START_RO_TX();
	rbnode_t *node = find_node(tree, key);

    if (node != NULL) 
        value = LOAD(node->value);
    else
        value = NULL;

    RB_COMMIT();

    return value;
}
//*******************************
static rbnode_t *sibling(rbnode_t *node)
{
    if (LOAD(LOAD(node->parent)->left) == node)
        return LOAD(LOAD(node->parent)->right);
    else
        return LOAD(LOAD(node->parent)->left);
}
//*******************************
static void restructure(rbtree_t *tree, rbnode_t *grandparent, rbnode_t *parent, rbnode_t *node,
                        rbnode_t **a, rbnode_t **b, rbnode_t **c)
{
    rbnode_t *aprime, *bprime, *cprime;
    rbnode_t *greatgrandparent = LOAD(grandparent->parent);
    int left = 0;

    STORE(tree->restructures, tree->restructures+1);

    if (LOAD(grandparent->parent) != NULL) left = is_left(grandparent);
    //printf("restructure %s\n", toString(node));

    if (LOAD(grandparent->left) == parent && LOAD(parent->left) == node)
    {
        // diag left
        aprime = node;
        bprime = parent;
        cprime = grandparent;

        // assign pointer
        STORE(cprime->left, LOAD(bprime->right));
        if (LOAD(bprime->right) != NULL) STORE(LOAD(bprime->right)->parent, cprime);
        
        STORE(bprime->right, cprime);
        STORE(cprime->parent, bprime);

        if (greatgrandparent != NULL)
        {
            if (left) {
                STORE(greatgrandparent->left, bprime);
            } else {
                STORE(greatgrandparent->right, bprime);
            }

            STORE(bprime->parent, greatgrandparent);
        } else {
            STORE(bprime->parent, NULL);
            STORE(tree->root, bprime);
        }
    } 
    else if (LOAD(grandparent->left) == parent && LOAD(parent->right) == node)
    {
        // zig left
        aprime = parent;
        bprime = node;
        cprime = grandparent;

        STORE(aprime->right, bprime->left);
        if (LOAD(bprime->left) != NULL) STORE(LOAD(bprime->left)->parent, aprime);

        STORE(cprime->left, bprime->right);
        if (LOAD(bprime->right) != NULL) STORE(LOAD(bprime->right)->parent, cprime);

        STORE(bprime->left, aprime);
        STORE(aprime->parent, bprime);

        STORE(bprime->right, cprime);
        STORE(cprime->parent, bprime);

        if (greatgrandparent != NULL)
        {
            if (left) {
                STORE(greatgrandparent->left, bprime);
            } else {
                STORE(greatgrandparent->right, bprime);
            }

            STORE(bprime->parent, greatgrandparent);
        } else {
            STORE(bprime->parent, NULL);
            STORE(tree->root, bprime);
        }
    }
    else if (LOAD(parent->right) == node && LOAD(grandparent->right) == parent)
    {
        // diag right
        aprime = grandparent;
        bprime = parent;
        cprime = node;

        STORE(aprime->right, bprime->left);
        if (LOAD(bprime->left) != NULL) STORE(LOAD(bprime->left)->parent, aprime);

        STORE(bprime->left, aprime);
        STORE(aprime->parent, bprime);

        if (greatgrandparent != NULL)
        {
            if (left) {
                STORE(greatgrandparent->left, bprime);
            } else {
                STORE(greatgrandparent->right, bprime);
            }

            STORE(bprime->parent, greatgrandparent);
        } else {
            STORE(bprime->parent, NULL);
            STORE(tree->root, bprime);
        }
    }
    else
    {
        // zig right
        aprime = grandparent;
        bprime = node;
        cprime = parent;

        STORE(aprime->right, bprime->left);
        if (LOAD(bprime->left) != NULL) STORE(LOAD(bprime->left)->parent, aprime);

        STORE(cprime->left, bprime->right);
        if (LOAD(bprime->right) != NULL) STORE(LOAD(bprime->right)->parent, cprime);

        STORE(bprime->left, aprime);
        STORE(aprime->parent, bprime);

        STORE(bprime->right, cprime);
        STORE(cprime->parent, bprime);

        if (greatgrandparent != NULL)
        {
            if (left) {
                STORE(greatgrandparent->left, bprime);
            } else {
                STORE(greatgrandparent->right, bprime);
            }

            STORE(bprime->parent, greatgrandparent);
        } else {
            STORE(bprime->parent, NULL);
            STORE(tree->root, bprime);
        }
    }

    *a = aprime;
    *b = bprime;
    *c = cprime;
}
//*******************************
static void recolor(rbtree_t *tree, rbnode_t *node)
{
    rbnode_t *parent = LOAD(node->parent);
    rbnode_t *grandparent = LOAD(parent->parent);
    rbnode_t *w = sibling(parent);
    rbnode_t *a, *b, *c;

    if (w == NULL || LOAD(w->color) == BLACK)
    {
        // case 1
        restructure(tree, grandparent, parent, node, &a, &b, &c);

        STORE(a->color, RED);
        STORE(b->color, BLACK);
        STORE(c->color, RED);
    } else if ( w!=NULL && LOAD(w->color) == RED) {
        // case 2
        STORE(w->color, BLACK);
        STORE(parent->color, BLACK);

        // condition makes sure root stays black
        if (LOAD(grandparent->parent) != NULL) 
        {
            STORE(grandparent->color, RED);
        }

        if (LOAD(grandparent->parent) != NULL && 
            LOAD(LOAD(grandparent->parent)->color) == RED)
        {
            recolor(tree, grandparent);
        }
    }
}
//*******************************
int rb_insert(rbtree_t *tree, long key, void *value)
{
    rbnode_t *new_node;
    int result = 1;

    RB_START_TX();

    //check_for(tree->root, new_node);

	if (LOAD(tree->root) == NULL)
	{
        new_node = rbnode_create(key, value);

        STORE(new_node->color, BLACK);
		STORE(tree->root, new_node);
	} else {
		rbnode_t *node = LOAD(tree->root);
		rbnode_t *prev = node;

		while (node != NULL)
		{
			prev = node;
            if (key == LOAD(node->key))
            {
                result = 0;
                break;
            }
            else if (key <= LOAD(node->key)) 
				node = LOAD(node->left);
			else 
				node = LOAD(node->right);
		}

        if (result != 0)
        {
            new_node = rbnode_create(key, value);

            STORE(new_node->color, RED);
            STORE(new_node->parent, prev);
            if (key <= LOAD(prev->key)) {
                STORE(prev->left, new_node);
            } else  {
                STORE(prev->right, new_node);
            }

            if (LOAD(prev->color) == RED) recolor(tree, new_node);
        }
	}

    RB_COMMIT();

    return result;
}
//*******************************
static rbnode_t *leftmost(rbnode_t *node)
{
	if (node == NULL) return NULL;

	while (LOAD(node->left) != NULL)
	{
		node = LOAD(node->left);
	}
	return node;
}
//*******************************
static rbnode_t *rightmost(rbnode_t *node)
{
	if (node == NULL) return NULL;

	while (LOAD(node->right) != NULL)
	{
		node = LOAD(node->right);
	}
	return node;
}
//*******************************
static void double_black(rbtree_t *tree, rbnode_t *r);
static void double_black_node(rbtree_t *tree, rbnode_t *x, rbnode_t *y, rbnode_t *r)
{
    rbnode_t *z;
    rbnode_t *a, *b, *c;

    if (LOAD(y->color) == BLACK)
    {
        z = NULL;
        if (LOAD(y->left) != NULL && LOAD(LOAD(y->left)->color) == RED)
            z = LOAD(y->left);
        else if (LOAD(y->right) != NULL && LOAD(LOAD(y->right)->color) == RED)
            z = LOAD(y->right);
        if (z != NULL)
        {
            // case 1
			int x_color = LOAD(x->color);
            restructure(tree, x, y, z, &a, &b, &c);
            STORE(b->color, x_color);
            STORE(a->color, BLACK);
            STORE(c->color, BLACK);
            if (r != NULL) 
            {
                STORE(r->color, BLACK);
            }
            return;
        } else {
            // case 2
            if (r != NULL) 
            {
                STORE(r->color, BLACK);
            }
            STORE(y->color, RED);
            if (LOAD(x->color) == RED)
            {
                STORE(x->color, BLACK);
            }
            else
            {
                STORE(x->color, BLACK_BLACK);
                double_black(tree, x);
            }
            return;
        }
    }
    else // if (y->color == RED)
    {
        // case 3
        if (is_left(y))
            z = LOAD(y->left);
        else 
            z = LOAD(y->right);
        restructure(tree, x,y,z, &a, &b, &c);

		STORE(y->color, BLACK);
        STORE(x->color, RED);

        if (LOAD(x->left) == r)
            y = LOAD(x->right);
        else
            y = LOAD(x->left);

        double_black_node(tree, x, y, r);
    }
}
//*******************************
static void double_black(rbtree_t *tree, rbnode_t *r)
{
    rbnode_t *x, *y;

    x = LOAD(r->parent);
	if (x == NULL)
	{
		STORE(r->color, BLACK);
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
    long temp_color;

    RB_START_TX();

	node = find_node(tree, key);

    // found
	if (node != NULL) 
    {

        prev = LOAD(node->parent);

        // found it, so cut it out of the tree
        //******************* swap with external node if necessary ***************
        if (LOAD(node->left) != NULL && LOAD(node->right) != NULL)
        {
            // need to do a swap with leftmost on right branch
            swap = leftmost(LOAD(node->right));
            temp_color = LOAD(swap->color);
            STORE(swap->color, LOAD(node->color));
            STORE(node->color, temp_color);

            if (swap == LOAD(node->right))
            {
                STORE(swap->left, LOAD(node->left));
                STORE(LOAD(node->left)->parent, swap);      // safe: checked above

                if (prev == NULL) {
                    STORE(tree->root, swap);
                } else {
                    if (LOAD(prev->left) == node) {
                        STORE(prev->left, swap);
                    } else {
                        STORE(prev->right, swap);
                    }
                }
                STORE(swap->parent, prev);

                prev = swap;
                next = LOAD(swap->right);
            } else {
                prev = LOAD(swap->parent);
                next = LOAD(swap->right);

                // fix-up swap's left child (replacing a NULL child)
                STORE(swap->left, LOAD(node->left));
                STORE(LOAD(node->left)->parent, swap);      // safe: checked above
                //node->left = NULL;              // swap->left guaranteed to be NULL

                // take swap temporarily out of the tree
                STORE(LOAD(swap->parent)->left, LOAD(swap->right));
                if (LOAD(swap->right) != NULL) STORE(LOAD(swap->right)->parent, LOAD(swap->parent));

                // fix-up swap's right child
                STORE(swap->right, LOAD(node->right));
                STORE(LOAD(node->right)->parent, swap);     // safe: checked above

                // put swap in new location
                if (LOAD(node->parent) == NULL)
                {
                    STORE(tree->root, swap);
                    STORE(swap->parent, NULL);
                } else {
                    if (is_left(node)) {
                        STORE(LOAD(node->parent)->left, swap);
                    } else {
                        STORE(LOAD(node->parent)->right, swap);
                    }
                    STORE(swap->parent, LOAD(node->parent));
                }
            }
        } else {
            // the node is guaranteed to have a terminal child
            prev = LOAD(node->parent);
            if (LOAD(node->left) == NULL)
            {
                // no left branch; bump right branch up
                next = LOAD(node->right);
            } else {
                next = LOAD(node->left);
            }

            if (prev != NULL)
            {
                if (is_left(node))
                {
                    STORE(prev->left, next);
                } else {
                    STORE(prev->right, next);
                }
                if (next != NULL) STORE(next->parent, prev);
            } else {
                STORE(tree->root, next);
                if (next != NULL) STORE(next->parent, NULL);
            }
        }

        //******************** rebalance *******************
        // need node==deleted node, prev,next == parent,child of node in its swapped position
        if (LOAD(node->color) == RED || (next!=NULL && LOAD(next->color) == RED))
        {
            // case 1
            if (next != NULL) 
            {
                STORE(next->color, BLACK);
            }
        } 
        else if (LOAD(node->color) == BLACK)
        {
            if (LOAD(prev->left) == next)
            {
                double_black_node(tree, prev, LOAD(prev->right), next);
            }
            else
            {
                double_black_node(tree, prev, LOAD(prev->left), next);
            }
        }
    }

    // save value, free node, return value
    if (node != NULL)
    {
        value = LOAD(node->value);
        rcu_free(tree->lock, rbnode_free, node);
    }
    else 
    {
        value = NULL;
    }

    RB_COMMIT();
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

    RB_START_RO_TX();

    node = leftmost(tree->root);
    if (node != NULL)
    {
        *key = node->key;
        value = node->value;
    }

    RB_COMMIT();

    return value;
}
//***************************************
void *rb_last(rbtree_t *tree, long *key)
{
    rbnode_t *node;
    void *value = NULL;

    RB_START_RO_TX();

    node = rightmost(tree->root);
    if (node != NULL)
    {
        *key = node->key;
        value = node->value;
    }

    RB_COMMIT();

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

    RB_START_RO_TX();
    
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

    RB_COMMIT();

    return value;
}
//***************************************
void *rb_old_next(rbtree_t *tree, long prev_key, long *key)
{
    rbnode_t *node;
    void *value;

    RB_START_RO_TX();
    
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

    RB_COMMIT();

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
