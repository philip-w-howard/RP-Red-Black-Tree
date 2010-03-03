#include <stdlib.h>

void read_lock(void *lock) {}
void read_unlock(void *lock) {}
void write_lock(void *lock) {}
void write_unlock(void *lock) {}
void rcu_synchronize() {}
void rcu_remove(void *ptr) {free(ptr);}
