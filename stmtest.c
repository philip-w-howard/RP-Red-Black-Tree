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
#include "my_stm.h"

unsigned long *Values;

//*******************************************
void list()
{
    int ii;
    unsigned long count = Values[0];
    char buff1[200], buff2[200];

    strcpy(buff1, "LIST:");

    for (ii=0; ii<count; ii++)
    {
        sprintf(buff2, " %ld", Values[ii]);
        strcat(buff1, buff2);
    }
    
    printf("%s\n", buff1);
}
//*******************************************
void *Init_Data(int count, void *lock, param_t *params)
{
    int ii;
    int size = params->scale;
    unsigned long random_seed = 12397567;

    Values = (unsigned long *)malloc(size*sizeof(unsigned long));

    for (ii=0; ii<size; ii++)
    {
        Values[ii] = 0;
    }

    for (ii=0; ii<count; ii++)
    {
        Insert(&random_seed, params);
    }

    return Values;
}
//*******************************************
int Read(unsigned long *random_seed, param_t *params)
{
    int read_elem;
    void *value;

    read_elem = get_random(random_seed) % params->size;

    BEGIN_TRANSACTION;
    value = LOAD(Values[read_elem]);
    END_TRANSACTION;

    return 0;
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
    int value;
    int ii;

    BEGIN_TRANSACTION;

    value = LOAD(Values[0]);

    for (ii=0; ii<value; ii++)
    {
        STORE(Values[ii], LOAD(Values[ii])-1);
    }

    END_TRANSACTION;

    return 0;
}
//*******************************************
int Insert(unsigned long *random_seed, param_t *params)
{
    int value;
    int ii;

    BEGIN_TRANSACTION;
    value = LOAD(Values[0]);

    for (ii=0; ii<=value; ii++)
    {
        STORE(Values[ii], LOAD(Values[ii])+1);
    }

    END_TRANSACTION;

    return 0;
}
//*******************************************
int Write(unsigned long *random_seed, param_t *params)
{
    Insert(random_seed, params);

    Delete(random_seed, params);

    return 0;
}
//*******************************************
int Size(void *data_structure)
{
    return Values[0];
}
//*******************************************
void Output_Stats(void *data_structure)
{
//    ll_t *tree = (ll_t *)data_structure;
}
