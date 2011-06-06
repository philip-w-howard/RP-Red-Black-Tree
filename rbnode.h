#ifndef RBNODE_H
#define RBNODE_H

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
//#ifdef STM
	long color;
//#else
//    int color;
//#endif

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
