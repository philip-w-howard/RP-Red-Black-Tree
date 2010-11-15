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
ll_t      *My_Data;

//****************************************
int ll_pre_insert(ll_t *list, unsigned long key, void *value)
{
    ll_node_t *new_node;
    ll_node_t *node = list->list;

    new_node = (ll_node_t *)malloc(sizeof(ll_node_t));
    new_node->next = node->next;
    new_node->key = key;
    new_node->value = value;
    new_node->next = NULL;
    node->next = new_node;
    list->size++;

    return 1;
}
//****************************************
int ll_insert(ll_t *list, unsigned long key, void *value)
{
    ll_node_t *node = list->list;

    while (node->next != NULL)
    {
        node = node->next;
        if (node->key == key) return 0;
    }

    node->next = (ll_node_t *)malloc(sizeof(ll_node_t));
    node = node->next;
    node->key = key;
    node->value = value;
    node->next = NULL;
    list->size++;

    return 1;
}
//*******************************************
void *ll_find(ll_t *list, unsigned long key)
{
    ll_node_t *node = list->list;

    while (node->next != NULL)
    {
        node = node->next;
        if (node->key == key) return node->value;
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
            free(node);
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
    My_Data = (ll_t *)malloc(sizeof(ll_t));
    My_Data->list = (ll_node_t *)malloc(sizeof(ll_node_t));
    My_Data->lock = lock;
    My_Data->size = 0;

    for (ii=0; ii<count; ii++)
    {
        value = get_random(&seed) % params->scale + 1;
        while ( !ll_pre_insert(My_Data, value, (void *)value) )
        {
            value = get_random(&seed) % params->scale + 1;
        }
        Values[ii] = value;
    }

    return My_Data;
}
//*******************************************
int Read(unsigned long *random_seed, param_t *params)
{
    int read_elem;
    void *value;

    read_elem = get_random(random_seed) % params->size;
    read_lock(My_Data->lock);
    value = ll_find(My_Data, Values[read_elem]);
    read_unlock(My_Data->lock);
    if ((unsigned long)value == Values[read_elem])
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
    printf("NOT IMPLEMENTED\n");
    exit(-1);
}
//*******************************************
int Insert(unsigned long *random_seed, param_t *params)
{
    printf("NOT IMPLEMENTED\n");
    exit(-1);
}
//*******************************************
int Write(unsigned long *random_seed, param_t *params)
{
    int errors = 0;
    int write_elem;
    void *value;
    long int_value;

    write_elem = get_random(random_seed) % params->size;
    write_lock(My_Data->lock);
    value = ll_remove(My_Data, Values[write_elem]);
    write_unlock(My_Data->lock);
    if (value == NULL) errors++;

    int_value = get_random(random_seed) % params->scale + 1;
    write_lock(My_Data->lock);
    while ( !ll_insert(My_Data, int_value, (void *)int_value) )
    {
        int_value = get_random(random_seed) % params->scale + 1;
    }
    write_unlock(My_Data->lock);
    Values[write_elem] = int_value;

    return errors;
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
