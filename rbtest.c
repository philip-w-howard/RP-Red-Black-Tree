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

#include "lock.h"
#include "rbtree.h"
#include "tests.h"
#include "rbmain.h"

unsigned long *Values;
rbtree_t      *My_Tree;

void check_tree()
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
//*******************************
void *Init_Data(int count, void *lock, param_t *params)
{
    int ii;
    unsigned long seed = init_random_seed(); // random();
    unsigned long value;

    Values = (unsigned long *)malloc(count*sizeof(unsigned long));
    My_Tree = (rbtree_t *)malloc(sizeof(rbtree_t));
    rb_create(My_Tree, lock);

#ifndef VALIDATE
    if (params->mode == MODE_TRAVERSE || params->mode == MODE_TRAVERSENLN)
    {
        rb_insert(My_Tree, -1, (void *)-1);
        rb_insert(My_Tree, params->scale+1, (void *)(long)params->scale+1);
        //count -= 2;
    }
#endif

#ifdef VALIDATE
    for (value=1; value<=count; value++)
    {
        rb_insert(My_Tree, value, (void *)(value));
    }
#else
    for (ii=0; ii<count; ii++)
    {
        value = get_random(&seed) % params->scale + 1;
        while ( !rb_insert(My_Tree, value, (void *)value) )
        {
            value = get_random(&seed) % params->scale + 1;
        }
#ifdef DEBUG_INSERT
        if (ii%1000 == 0)
        printf("Insert %d %ld\n", ii, value);
        //check_tree();
#endif

        Values[ii] = value;
    }
#endif

    return My_Tree;
}
#ifdef VALIDATE
// ******************************************
// O(N) traversal
int Traverse(unsigned long *random_seed, param_t *params)
{
    rbnode_t *new_node, *node;
    long key = -1;
    long index = 1;
    int errors = 0;

#ifdef DEBUG
    long values[1000];
    index = 0;
    //printf("TRAVERSAL ******************************************\n");
#endif
    read_lock(My_Tree->lock);
    new_node = rb_first_n(My_Tree);

    while (new_node != NULL)
    {
        node = new_node;
        key = node->key;

        if (key != index)
        {
            if (index & 0x0001) index++;
            errors++;
        }
        if (key != index)
        {
            printf(" ***************** INVALID TRAVERSAL ******************\n");
            printf(" ***************** INVALID TRAVERSAL ******************\n");
            printf("Expected %ld found %ld\n", index, key);
            printf(" ***************** INVALID TRAVERSAL ******************\n");
            printf(" ***************** INVALID TRAVERSAL ******************\n");
            read_unlock(My_Tree->lock);
            exit(-1);
            return 0;
        }
        index++;
        new_node = rb_next(node);
    }

    read_unlock(My_Tree->lock);

    return errors;
}
#else // !VALIDATE
// ******************************************
// O(N) traversal
int Traverse(unsigned long *random_seed, param_t *params)
{
    rbnode_t *new_node, *node;
    long key = -1;

#ifdef DEBUG
    long values[1000];
    index = 0;
    //printf("TRAVERSAL ******************************************\n");
#endif
#ifdef NO_GRACE_PERIOD
    read_lock(My_Tree->lock);
#else
    rw_lock(My_Tree->lock);
#endif
    new_node = rb_first_n(My_Tree);
    assert(new_node->key == -1);

    while (new_node != NULL)
    {
        node = new_node;
        key = node->key;

#ifdef DEBUG
        values[index++] = key;
#endif
        new_node = rb_next(node);
#ifdef DEBUG
        if (new_node != NULL && node->key >= new_node->key)
        {
            printf("******************************************\n"
                   "TRAVERSEAL ERROR key: %ld new: %ld\n"
                   "******************************************\n", 
                   node->key, new_node->key);
            while (--index >= 0)
            {
                printf("%3d: %ld\n", index, values[index]);
            }
#ifdef NO_GRACE_PERIOD
            read_unlock(My_Tree->lock);
#else
            rw_unlock(My_Tree->lock);
#endif
            write_lock(My_Tree->lock);
            rb_output(My_Tree);
            exit(-1);
            return 0;
        }
#endif
    }

#ifdef NO_GRACE_PERIOD
    read_unlock(My_Tree->lock);
#else
    rw_unlock(My_Tree->lock);
#endif

    assert(key == params->scale + 1);
    return 0;
}
#endif // VALIDATE


#ifdef VALIDATE
// ******************************************
// O(N log(N)) traversal
int TraverseNLN(unsigned long *random_seed, param_t *params)
{
    long new_key=0;
    void *value;
    long key = -1;
    long index = 1;
    int errors = 0;

    //printf("TRAVERSAL ******************************************\n");
    value = rb_first(My_Tree, &new_key);

    while (value != NULL)
    {
        key = new_key;
        if (key != index)
        {
            if (index & 0x0001) index++;
            errors++;
        }
        if (key != index)
        {
            printf("****************** INVALID TRAVERSAL ***********************\n");
            printf("found %ld expected %ld\n", key, index);
            printf("****************** INVALID TRAVERSAL ***********************\n");
            exit(-1);
            return 0;
        }

        value = rb_next_nln(My_Tree, key, &new_key);
        index++;
    }
    return errors;
}
#else // !VALIDATE
// ******************************************
// O(N log(N)) traversal
int TraverseNLN(unsigned long *random_seed, param_t *params)
{
    long new_key=0;
    void *value;
    long key = -1;

    //printf("TRAVERSAL ******************************************\n");
    value = rb_first(My_Tree, &new_key);
    assert(new_key == -1);

    while (value != NULL)
    {
        key = new_key;
        value = rb_next_nln(My_Tree, key, &new_key);
        if (value != NULL && key >= new_key)
        {
            write_lock(My_Tree->lock);
            printf("******************************************\n"
                   "TRAVERSEAL ERROR key: %ld new: %ld\n"
                   "******************************************\n", 
                   key, new_key);
            rb_output(My_Tree);
            write_unlock(My_Tree->lock);
            return 0;
        }
    }
    assert(key == params->scale + 1);
    return 0;
}
#endif // VALIDATE
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
#ifdef RP_UPDATE
int Update(unsigned long *random_seed, param_t *params)
{
    int errors = 0;
    void *value;
    long int_value;

    int_value = get_random(random_seed) % params->scale + 1;
    value = rb_update(My_Tree, int_value);
    if (value == NULL) errors++;

    return errors;
}
#endif
int Insert(unsigned long *random_seed, param_t *params)
{
    int errors = 0;
    long int_value;

    int_value = get_random(random_seed) % params->scale + 1;
    if (!rb_insert(My_Tree, int_value, (void *)int_value) ) errors++;

    return errors;
}
#ifdef VALIDATE
int Write(unsigned long *random_seed, param_t *params)
{
    long int_value;
    void *value;

    int_value = (get_random(random_seed) % params->size) + 1;

    // make sure we have an odd value
    int_value |= 0x0001;

    value = rb_remove(My_Tree, int_value);
    if (value == NULL)
    {
        printf("Failure to remove %ld\n", int_value);
        exit(-2);
    }

    if (!rb_insert(My_Tree, int_value, (void *)int_value) )
    {
        printf("Failure to insert %ld\n", int_value);
        exit(-3);
    }

    return 0;
}
#else // !VALIDATE
int Write(unsigned long *random_seed, param_t *params)
{
    int errors = 0;
    int write_elem;
    long int_value;
    void *value;

    write_elem = get_random(random_seed) % params->size;
#ifdef DEBUG
    check_tree();
    printf("Remove %ld\n", Values[write_elem]);
#endif
    value = rb_remove(My_Tree, Values[write_elem]);
    if (value == NULL) errors++;

    int_value = get_random(random_seed) % params->scale + 1;
#ifdef DEBUG
    check_tree();
    printf("Insert %ld\n", int_value);
#endif
    while ( !rb_insert(My_Tree, int_value, (void *)int_value) )
    {
        int_value = get_random(random_seed) % params->scale + 1;
#ifdef DEBUG
        printf("Insert %ld\n", int_value);
#endif
    }
    Values[write_elem] = int_value;

    return errors;
}
#endif
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
