#ifndef RBNODE_H
#define RBNODE_H

#define BLACK	    0
#define RED		    1
#define BLACK_BLACK 2

typedef unsigned long long version_t;

typedef struct rbnode_s
{
	long key;
	void *value;
	struct rbnode_s *left;
	struct rbnode_s *right;
    struct rbnode_s *parent;
	unsigned long index;
    // Red Black
	int color;

    // FG locking and AVL
    void *lock;

    // AVL
    volatile int height;
    volatile version_t changeOVL;
} rbnode_t;

rbnode_t *rbnode_create(long key, void *value);
rbnode_t *rbnode_copy(rbnode_t *node);
void rbnode_free(void *ptr);
int rbnode_invalid(rbnode_t *node, int depth);

#endif
