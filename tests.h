#ifndef __TESTS_H
#define __TESTS_H
#include "rbmain.h"

int Read(unsigned long *random_seed, param_t *params);
int Write(unsigned long *random_seed, param_t *params);
void *Init_Data(int size, void *lock, param_t *params);
int Size(void *data_structure);
void Output_Stats(void *data_structure);


#endif

