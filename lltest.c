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
#include "tests.h"
#include "rbmain.h"
#include "rcu.h"

#define LL_INSERT ll_sort_insert
#define LL_FIND ll_sort_find

typedef struct ll_node_s
{
    unsigned long key;
    void *value;
    struct ll_node_s *next;
} ll_node_t;

typedef struct ll_s
{
    ll_node_t *list;
    void *lock;
    unsigned long size;
} ll_t;

unsigned long *Values;
ll_t      *My_List;

//****************************************
int ll_pre_insert(ll_t *list, unsigned long key, void *value)
{
    ll_node_t *new_node;
    ll_node_t *node = list->list;

    new_node = (ll_node_t *)malloc(sizeof(ll_node_t));
    new_node->next = node->next;
    new_node->key = key;
    new_node->value = value;
    rp_assign_pointer(node->next, new_node);
    list->size++;

    return 1;
}
//****************************************
int ll_post_insert(ll_t *list, unsigned long key, void *value)
{
    ll_node_t *node = list->list;
    ll_node_t *new_node;
    ll_node_t *prev = node;

    while (node != NULL)
    {
        prev = node;
        if (node->key == key) return 0;
        node = node->next;
    }
    if (node->key == key) return 0;

    new_node = (ll_node_t *)malloc(sizeof(ll_node_t));
    new_node->key = key;
    new_node->value = value;
    new_node->next = NULL;
    list->size++;
    rp_assign_pointer(prev->next, new_node);
    rp_assign_pointer(node->next->next, new_node);

    return 1;
}
//****************************************
int ll_sort_insert(ll_t *list, unsigned long key, void *value)
{
    ll_node_t *node = list->list;
    ll_node_t *new_node;
    ll_node_t *prev = node;

    while (node != NULL && node->key <= key)
    {
        prev = node;
        if (node->key == key) return 0;
        node = node->next;
    }

    new_node = (ll_node_t *)malloc(sizeof(ll_node_t));
    new_node->key = key;
    new_node->value = value;
    new_node->next = prev->next;
    list->size++;
    rp_assign_pointer(prev->next, new_node);

    return 1;
}
//*******************************************
void *ll_full_find(ll_t *list, unsigned long key)
{
    ll_node_t *node = rp_dereference(list->list);

    while (node != NULL)
    {
        if (node->key == key) return node->value;
        node = rp_dereference(node->next);
    }

    return NULL;
}
//*******************************************
void *ll_sort_find(ll_t *list, unsigned long key)
{
    ll_node_t *node = rp_dereference(list->list);

    while (node != NULL && node->key <= key)
    {
        if (node->key == key) return node->value;
        node = rp_dereference(node->next);
    }

    return NULL;
}
//*******************************************
void *ll_remove(ll_t *list, unsigned long key)
{
    ll_node_t *node = list->list;
    ll_node_t *prev_node;
    void *value;

    while (node->next != NULL)
    {
        prev_node = node;
        node = node->next;
        if (node->key == key) 
        {
            prev_node->next = node->next;
            value = node->value;
            rp_free(list->lock, free, node);
            list->size--;
            return value;
        }
    }

    return NULL;
}
//*******************************************
void *Init_Data(int count, void *lock, param_t *params)
{
    int ii;
    unsigned long seed = init_random_seed(); // random();
    unsigned long value;

    Values = (unsigned long *)malloc(count*sizeof(unsigned long));
    My_List = (ll_t *)malloc(sizeof(ll_t));
    My_List->list = (ll_node_t *)malloc(sizeof(ll_node_t));
    My_List->lock = lock;
    My_List->size = 0;

    for (ii=0; ii<count; ii++)
    {
        value = get_random(&seed) % params->scale + 1;
        while ( !LL_INSERT(My_List, value, (void *)value) )
        {
            value = get_random(&seed) % params->scale + 1;
        }
        Values[ii] = value;
    }

    return My_List;
}
//*******************************************
static void do_fail(int read_elem, unsigned long key)
{
    printf("Failed to find %d %ld %ld\n", read_elem, key, Values[read_elem]);
    Traverse(NULL, NULL);
}
//*******************************************
int Read(unsigned long *random_seed, param_t *params)
{
    int read_elem;
    void *value;
    unsigned long key;

    read_elem = get_random(random_seed) % params->size;
    key = Values[read_elem];
    read_lock(My_List->lock);
    value = LL_FIND(My_List, key);
    read_unlock(My_List->lock);
    if ((unsigned long)value == key)
        return 0;
    else
        return 1;
}
//*******************************************
int RRead(unsigned long *random_seed, param_t *params)
{
    printf("NOT IMPLEMENTED\n");
    exit(-1);
}
//*******************************************
int Delete(unsigned long *random_seed, param_t *params)
{
    int errors = 0;
    void *value;
    long int_value;
    int_value = get_random(random_seed) % params->scale + 1;
    write_lock(My_List->lock);
    value = ll_remove(My_List, int_value);
    write_unlock(My_List->lock);
    if (value == NULL) errors++;

    return errors;
}
//*******************************************
int Insert(unsigned long *random_seed, param_t *params)
{
    int errors = 0;
    int result;
    long int_value;
    int_value = get_random(random_seed) % params->scale + 1;
    write_lock(My_List->lock);
    result = LL_INSERT(My_List, int_value, (void *)int_value);
    write_unlock(My_List->lock);
    if (result != 1) errors++;

    return errors;
}
//*******************************************
static void test_data(param_t *params)
{
    int ii;
    static int failures = 0;

    for (ii=0; ii<params->size; ii++)
    {
        if (ll_full_find(My_List, Values[ii]) == NULL)
        {
            failures++;
            printf("test_data failure: %d\n", failures);
            do_fail(ii, Values[ii]);
            break;
        }
    }
}
//*******************************************
int Write(unsigned long *random_seed, param_t *params)
{
    int errors = 0;
    int write_elem;
    void *value;
    long int_value;

    write_elem = get_random(random_seed) % params->size;
    write_lock(My_List->lock);
    value = ll_remove(My_List, Values[write_elem]);
    write_unlock(My_List->lock);
    if (value == NULL) errors++;

    int_value = get_random(random_seed) % params->scale + 1;
    write_lock(My_List->lock);
    while ( !LL_INSERT(My_List, int_value, (void *)int_value) )
    {
        int_value = get_random(random_seed) % params->scale + 1;
    }
    write_unlock(My_List->lock);
    Values[write_elem] = int_value;
    //test_data(params);

    return errors;
}
//*******************************************
int Traverse(unsigned long *random_seed, param_t *params)
{
    int index = 0;
    ll_node_t *node = rp_dereference(My_List->list);

    while (node->next != NULL)
    {
        node = rp_dereference(node->next);
        printf("Traverse: %5d %ld\n", index, node->key);
        index++;
    }

    //ll_full_find(My_List, 99700000000);
    return 0;
}
//*******************************************
int TraverseN(unsigned long *random_seed, param_t *params)
{
    ll_full_find(My_List, 99700000000);
    return 0;
}
//*******************************************
int Size(void *data_structure)
{
    return ((ll_t *)data_structure)->size;
}
//*******************************************
void Output_Stats(void *data_structure)
{
//    ll_t *tree = (ll_t *)data_structure;
}
