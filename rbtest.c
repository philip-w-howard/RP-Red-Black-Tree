#include "lock.h"
#include "rbtree.h"
#include "tests.h"
#include "rbmain.h"

unsigned long *Values;
rbtree_t      *My_Tree;

void *Init_Data(int count, void *lock, param_t *params)
{
    int ii;
    unsigned long seed = init_random_seed(); // random();
    unsigned long value;

    Values = (unsigned long *)malloc(count*sizeof(unsigned long));
    My_Tree = (rbtree_t *)malloc(sizeof(rbtree_t));
    rb_create(My_Tree, lock);

    for (ii=0; ii<count; ii++)
    {
        value = get_random(&seed) % params->scale + 1;
        while ( !rb_insert(My_Tree, value, (void *)value) )
        {
            value = get_random(&seed) % params->scale + 1;
        }
        Values[ii] = value;
    }

    return My_Tree;
}
int traversn(unsigned long *random_seed, param_t *params)
{
    long new_key=0;
    void *value;
    long key = -1;

    value = rb_first(My_Tree, &new_key);
    assert(new_key == -1);

    while (value != NULL)
    {
        key = new_key;
        value = rb_next(My_Tree, key, &new_key);
        if (value != NULL && key >= new_key)
        {
            write_lock(My_Tree->lock);
            printf("******************************************\n"
                   "%s\nkey: %ld new: %ld\n"
                   "******************************************\n", 
                   (char *)value, key, new_key);
            rb_output(My_Tree);
            write_unlock(My_Tree->lock);
            return 0;
        }
    }
    assert(key == params->scale + 1);
    return 1;
}
int traverse(unsigned long *random_seed, param_t *params)
{
    rbnode_t *new_node, *node;
    long key = -1;

    rw_lock(My_Tree->lock);
    new_node = rb_first_n(My_Tree);
    assert(new_node->key == -1);

    while (new_node != NULL)
    {
        node = new_node;
        key = node->key;

        new_node = rb_next_n(node);
        if (new_node != NULL && node->key >= new_node->key)
        {
            printf("******************************************\n"
                   "%s\nkey: %ld new: %ld\n"
                   "******************************************\n", 
                   (char *)node->value, node->key, new_node->key);
            rb_output(My_Tree);
            rw_unlock(My_Tree->lock);
            return 0;
        }
    }

    rw_unlock(My_Tree->lock);

    assert(key == params->scale + 1);
    return 1;
}
int Read(unsigned long *random_seed, param_t *params)
{
    int read_elem;
    void *value;

    read_elem = get_random(random_seed) % params->size;
    value = rb_find(My_Tree, Values[read_elem]);
    if ((unsigned long)value == Values[read_elem])
        return 0;
    else
        return 1;
}
int RRead(unsigned long *random_seed, param_t *params)
{
    long int_value;
    void *value;

    int_value = get_random(random_seed) % params->scale + 1;
    value = rb_find(My_Tree, int_value);
    if ((unsigned long)value == int_value)
        return 0;
    else
        return 1;
}
int Delete(unsigned long *random_seed, param_t *params)
{
    int errors = 0;
    void *value;
    long int_value;

    int_value = get_random(random_seed) % params->scale + 1;
    value = rb_remove(My_Tree, int_value);
    if (value == NULL) errors++;

    return errors;
}
static void check_tree()
{
    if (!rb_valid(My_Tree)) 
    {
        printf("******* INVALID TREE **********\n");
        printf("******* INVALID TREE **********\n");
        printf("******* INVALID TREE **********\n");
        printf("******* INVALID TREE **********\n");
        rb_output_list(My_Tree);
        exit(1);
    } else {
        rb_output(My_Tree);
    }
}
int Insert(unsigned long *random_seed, param_t *params)
{
    int errors = 0;
    long int_value;

    int_value = get_random(random_seed) % params->scale + 1;
    if (!rb_insert(My_Tree, int_value, (void *)int_value) ) errors++;

    return errors;
}
int Write(unsigned long *random_seed, param_t *params)
{
    int errors = 0;
    int write_elem;
    void *value;
    long int_value;

    write_elem = get_random(random_seed) % params->size;
    value = rb_remove(My_Tree, Values[write_elem]);
    if (value == NULL) errors++;

    int_value = get_random(random_seed) % params->scale + 1;
    while ( !rb_insert(My_Tree, int_value, (void *)int_value) )
    {
        int_value = get_random(random_seed) % params->scale + 1;
    }
    Values[write_elem] = int_value;

    return errors;
}
int Size(void *data_structure)
{
    return rb_size((rbtree_t *)data_structure);
}
void Output_Stats(void *data_structure)
{
    rbtree_t *tree = (rbtree_t *)data_structure;

    printf("copy %lld mcopy %lld swap %lld rest %lld gp %lld\n",
            tree->restructure_copies, tree->restructure_multi_copies, 
            tree->swap_copies, tree->restructures, 
            tree->grace_periods); 
    if (!rb_valid(tree)) 
    {
        printf("******* INVALID TREE **********\n");
        printf("******* INVALID TREE **********\n");
        printf("******* INVALID TREE **********\n");
        printf("******* INVALID TREE **********\n");
        rb_output_list(tree);
    }
}
