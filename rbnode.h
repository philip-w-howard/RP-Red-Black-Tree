#ifndef RBNODE_H
#define RBNODE_H

#define BLACK	    0
#define RED		    1
#define BLACK_BLACK 2

typedef struct rbnode_s
{
	unsigned long key;
	void *value;
	int color;
	struct rbnode_s *left;
	struct rbnode_s *right;
    struct rbnode_s *parent;
	unsigned long index;
} rbnode_t;

rbnode_t *rbnode_create(unsigned long key, void *value);
rbnode_t *rbnode_copy(rbnode_t *node);
void rbnode_free(void *ptr);

#endif
