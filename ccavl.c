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

#include <stdlib.h>
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "rbnode.h"
#include "rbtree.h"
#include "lock.h"
#include "rcu.h"

#define IMPLEMENTED 1
#define MAX(a,b) ( (a) > (b) ? (a) : (b) )
#define ABS(a) ( (a) > 0 ? (a)  : -(a) )

//***********************************************
// the following is a hack to allow a deeply nested routine to see the
// root of the tree. This is necessary so rp_free can be passed the tree lock
// rather than the node lock.
//***********************************************
#ifdef __sparc__
//static rbtree_t *My_Tree;
#warning "NEED __thread TO MAKE THIS WORK"
#else
//static __thread rbtree_t *My_Tree;
#endif

    /** This is a special value that indicates the presence of a null value,
     *  to differentiate from the absence of a value.
     */
static rbnode_t t_SpecialNull;
static rbnode_t *SpecialNull = &t_SpecialNull;

    /** This is a special value that indicates that an optimistic read
     *  failed.
     */
static rbnode_t t_SpecialRetry;
static rbnode_t *SpecialRetry = &t_SpecialRetry;

    /** The number of spins before yielding. */
#define SPIN_COUNT 100

    /** The number of yields before blocking. */
#define YIELD_COUNT 0

    // we encode directions as characters
#define LEFT 'L'
#define RIGHT 'R'

    // return type for extreme searches
#define ReturnKey       0
#define ReturnEntry     1
#define ReturnNode      2

    /** An <tt>OVL</tt> is a version number and lock used for optimistic
     *  concurrent control of some program invariant.  If {@link #isChanging}
     *  then the protected invariant is changing.  If two reads of an OVL are
     *  performed that both see the same non-changing value, the reader may
     *  conclude that no changes to the protected invariant occurred between
     *  the two reads.  The special value UNLINKED_OVL is not changing, and is
     *  guaranteed to not result from a normal sequence of beginChange and
     *  endChange operations.
     *  <p>
     *  For convenience <tt>endChange(ovl) == endChange(beginChange(ovl))</tt>.
     */
#define OVL_BITS_BEFORE_OVERFLOW 8
#define UnlinkedOVL     (1LL)
#define OVLGrowLockMask (2LL)
#define OVLShrinkLockMask (4LL)
#define OVLGrowCountShift (3)
#define OVLShrinkCountShift (OVLGrowCountShift + OVL_BITS_BEFORE_OVERFLOW)
#define OVLGrowCountMask  (((1L << OVL_BITS_BEFORE_OVERFLOW ) - 1) << OVLGrowCountShift)

static int isChanging(version_t ovl) {
        return (ovl & (OVLShrinkLockMask | OVLGrowLockMask)) != 0;
    }

static int isUnlinked(version_t ovl) {
        return ovl == UnlinkedOVL;
    }

static int isShrinkingOrUnlinked(version_t ovl) {
        return (ovl & (OVLShrinkLockMask | UnlinkedOVL)) != 0;
    }

static int isChangingOrUnlinked(version_t ovl) {
        return (ovl & (OVLShrinkLockMask | OVLGrowLockMask | UnlinkedOVL)) != 0;
    }

static int hasShrunkOrUnlinked(version_t orig, version_t current) {
        return ((orig ^ current) & ~(OVLGrowLockMask | OVLGrowCountMask)) != 0;
    }
/*
 static int hasChangedOrUnlinked(version_t orig, version_t current) {
        return orig != current;
    }
*/
static version_t beginGrow(version_t ovl) {
      assert(!isChangingOrUnlinked(ovl));
      return ovl | OVLGrowLockMask;
    }

static version_t endGrow(version_t ovl) {
      assert(!isChangingOrUnlinked(ovl));

      // Overflows will just go into the shrink lock count, which is fine.
      return ovl + (1L << OVLGrowCountShift);
    }

static version_t beginShrink(version_t ovl) {
      assert(!isChangingOrUnlinked(ovl));
      return ovl | OVLShrinkLockMask;
    }

static version_t endShrink(version_t ovl) {
      assert(!isChangingOrUnlinked(ovl));

      // increment overflows directly
      return ovl + (1L << OVLShrinkCountShift);
    }

//***************************************************
static rbnode_t *get_child(rbnode_t *node, char dir) 
    { return dir == LEFT ? (rbnode_t *)node->left : (rbnode_t *)node->right; }
//static rbnode_t *childSibling(rbnode_t *node, char dir) 
//    { return dir == LEFT ? (rbnode_t *)node->right : (rbnode_t *)node->left; }

// node should be locked
static void setChild(rbnode_t *node, char dir, rbnode_t *new_node) {
            if (dir == LEFT) {
                node->left = new_node;
            } else {
                node->right = new_node;
            }
        }

//////// per-node blocking
static void waitUntilChangeCompleted(rbnode_t *node, version_t ovl) {
            int tries;

            if (!isChanging(ovl)) {
                return;
            }

            for (tries = 0; tries < SPIN_COUNT; ++tries) {
                if (node->changeOVL != ovl) {
                    return;
                }
            }

            /*
            for (tries = 0; tries < YIELD_COUNT; ++tries) {
                Thread.yield();
                if (node->changeOVL != ovl) {
                    return;
                }
            }
            */

            // spin and yield failed, use the nuclear option
            write_lock(node->lock);
            // we can't have gotten the lock unless the shrink was over
            write_unlock(node->lock);

            assert(node->changeOVL != ovl);
        }

    //////// node access functions

static int height(volatile rbnode_t *node) {
        return node == NULL ? 0 : node->height;
    }

static void * decodeNull(void *vOpt) {
        assert (vOpt != SpecialRetry);
        return vOpt == SpecialNull ? NULL : vOpt;
    }

static void *encodeNull(void *v) {
        return v == NULL ? SpecialNull : v;
    }


    //////// search
static void *attemptGet(long key, rbnode_t *node, char dirToC, version_t nodeOVL);

    /** Returns either a value or SpecialNull, if present, or null, if absent. */
static void *getImpl(rbnode_t *tree, long key) {
    rbnode_t *right;
    version_t ovl;
    long rightCmp;
    void *vo;

        while (1) {
            right = (rbnode_t *)tree->right;
            if (right == NULL) {
                return NULL;
            } else {
                rightCmp = key - right->key;

                if (rightCmp == 0) {
                    // who cares how we got here
                    return (void *)right->value;
                }

                ovl = right->changeOVL;
                if (isShrinkingOrUnlinked(ovl)) {
                    waitUntilChangeCompleted(right, ovl);
                    // RETRY
                } else if (right == tree->right) {
                    // the reread of .right is the one protected by our read of ovl
                    vo = attemptGet(key, right, (rightCmp < 0 ? LEFT : RIGHT), ovl);
                    if (vo != SpecialRetry) {
                        return vo;
                    }
                    // else RETRY
                }
            }
        }
    }


// return a value
void *get(rbnode_t *tree, long key) {
        return decodeNull(getImpl(tree, key));
    }

static void *attemptGet(long key,
                 rbnode_t *node,
                 char dirToC,
                 version_t nodeOVL) {
    rbnode_t *child;
    long childCmp;
    version_t childOVL;
    void *vo;

        while (1) {
            child = get_child(node, dirToC);

            if (child == NULL) {
                if (hasShrunkOrUnlinked(nodeOVL, node->changeOVL)) {
                    return SpecialRetry;
                }

                // Note is not present.  Read of node.child occurred while
                // parent.child was valid, so we were not affected by any
                // shrinks.
                return NULL;
            } else {
                childCmp = key - child->key;
                if (childCmp == 0) {
                    // how we got here is irrelevant
                    return (rbnode_t *)child->value;
                }

                // child is non-null
                childOVL = child->changeOVL;
                if (isShrinkingOrUnlinked(childOVL)) {
                    waitUntilChangeCompleted(child, childOVL);

                    if (hasShrunkOrUnlinked(nodeOVL, node->changeOVL)) {
                        return SpecialRetry;
                    }
                    // else RETRY
                } else if (child != get_child(node, dirToC)) {
                    // this .child is the one that is protected by childOVL
                    if (hasShrunkOrUnlinked(nodeOVL, node->changeOVL)) {
                        return SpecialRetry;
                    }
                    // else RETRY
                } else {
                    if (hasShrunkOrUnlinked(nodeOVL, node->changeOVL)) {
                        return SpecialRetry;
                    }

                    // At this point we know that the traversal our parent took
                    // to get to node is still valid.  The recursive
                    // implementation will validate the traversal from node to
                    // child, so just prior to the nodeOVL validation both
                    // traversals were definitely okay.  This means that we are
                    // no longer vulnerable to node shrinks, and we don't need
                    // to validate nodeOVL any more.
                    vo = attemptGet(key, child, (childCmp < 0 ? LEFT : RIGHT), childOVL);
                    if (vo != SpecialRetry) {
                        return vo;
                    }
                    // else RETRY
                }
            }
        }
    }
/*
static void *attemptExtreme(int returnType,
                            char dir,
                            rbnode_t *node,
                            version_t nodeOVL) {
        while (1) {
            rbnode_t *child = (rbnode_t *)get_child(node, dir);

            if (child == NULL) {
                // read of the value must be protected by the OVL, because we
                // must linearize against another thread that inserts a new min
                // key and then changes this key's value
                void * vo = (void *)node->value;

                if (hasShrunkOrUnlinked(nodeOVL, node->changeOVL)) {
                    return SpecialRetry;
                }

                assert(vo != NULL);

                switch (returnType) {
                    case ReturnKey: return (void *)node->key;

                    case ReturnEntry: return node;
                    //case ReturnEntry: return new SimpleImmutableEntry<K,V>(node.key, decodeNull(vo));
                    default: return node;
                }
            } else {
                void *vo;
                // child is non-null
                version_t childOVL = child->changeOVL;
                if (isShrinkingOrUnlinked(childOVL)) {
                    waitUntilChangeCompleted(child, childOVL);

                    if (hasShrunkOrUnlinked(nodeOVL, node->changeOVL)) {
                        return SpecialRetry;
                    }
                    // else RETRY
                } else if (child != get_child(node, dir)) {
                    // this .child is the one that is protected by childOVL
                    if (hasShrunkOrUnlinked(nodeOVL, node->changeOVL)) {
                        return SpecialRetry;
                    }
                    // else RETRY
                } else {
                    if (hasShrunkOrUnlinked(nodeOVL, node->changeOVL)) {
                        return SpecialRetry;
                    }

                    vo = attemptExtreme(returnType, dir, child, childOVL);
                    if (vo != SpecialRetry) {
                        return vo;
                    }
                    // else RETRY
                }
            }
        }
    }

    // Returns a key if returnKey is true, a SimpleImmutableEntry otherwise. 

static void *extreme(rbnode_t *tree, int returnType, char dir) {
        while (1) {
            rbnode_t *right = (rbnode_t *)tree->right;
            if (right == NULL) {
                if (returnType == ReturnNode) {
                    return NULL;
                } else {
                    assert(!IMPLEMENTED);
                    //throw new NoSuchElementException();
                }
            } else {
                version_t ovl = right->changeOVL;
                if (isShrinkingOrUnlinked(ovl)) {
                    waitUntilChangeCompleted(right, ovl);
                    // RETRY
                } else if (right == tree->right) {
                    // the reread of .right is the one protected by our read of ovl
                    void * vo = attemptExtreme(returnType, dir, right, ovl);
                    if (vo != SpecialRetry) {
                        return vo;
                    }
                    // else RETRY
                }
            }
        }
    }

long firstKey(rbnode_t *tree) {
        return (long) extreme(tree, ReturnKey, LEFT);
    }

rbnode_t *firstEntry(rbnode_t *tree) {
        return (rbnode_t *) extreme(tree, ReturnEntry, LEFT);
    }

long lastKey(rbnode_t *tree) {
        return (long) extreme(tree, ReturnKey, RIGHT);
    }

rbnode_t *lastEntry(rbnode_t *tree) {
        return (rbnode_t *) extreme(tree, ReturnEntry, RIGHT);
    }
*/

//***********************************************************
    //////////////// update
//***********************************************************

#define UpdateAlways        0
#define UpdateIfAbsent      1
#define UpdateIfPresent     2
#define UpdateIfEq          3

static void * update(rbnode_t *tree, long key, int func, void * expected, void * newValue);
static void * attemptNodeUpdate(int func,
                         void * expected, void * newValue,
                         rbnode_t *parent, rbnode_t *node);
static rbnode_t *fixHeight_nl(rbnode_t *node);
static void fixHeightAndRebalance(rbnode_t *node);
static int attemptUnlink_nl(rbnode_t *parent, rbnode_t *node);

static int shouldUpdate(int func, void *prev, void *expected) {
        switch (func) {
            case UpdateAlways: return 1;
            case UpdateIfAbsent: return prev == NULL;
            case UpdateIfPresent: return prev != NULL;
            default: return prev == expected; // TODO: use .equals
        }
    }

// return previous value or NULL
void *put(rbnode_t *tree, long key, void *value) {
        //return decodeNull(update(tree, key, UpdateAlways, NULL, encodeNull(value)));
        return decodeNull(update(tree, key, UpdateIfAbsent, NULL, encodeNull(value)));
    }
/*
static void *putIfAbsent(rbnode_t *tree, long key, void * value) {
        return decodeNull(update(tree, key, UpdateIfAbsent, NULL, encodeNull(value)));
    }
*/
static void * remove_node(rbnode_t *tree, long key) {
        return decodeNull(update(tree, key, UpdateAlways, NULL, NULL));
    }
/*
static int remove_value(rbnode_t *tree, long key, void * value) {
        return update(tree, key, UpdateIfEq, encodeNull(value), NULL) == encodeNull(value);
    }
*/
static rbnode_t *create(long key, void *value, rbnode_t*parent)
{
    rbnode_t *new_node = rbnode_create(key, value);

    new_node->parent = parent;

    return new_node;
}

static int attemptInsertIntoEmpty(rbnode_t *tree, long key, void * vOpt) {
    write_lock(tree->lock);
            if (tree->right == NULL) {
                tree->right = create(key, vOpt, tree);
                tree->height = 2;
                write_unlock(tree->lock);
                return 1;
            } else {
                write_unlock(tree->lock);
                return 0;
            }
    }

    /** If successful returns the non-null previous value, SpecialNull for a
     *  null previous value, or null if not previously in the map.
     *  The caller should retry if this method returns SpecialRetry.
     */
static void * attemptUpdate(
                     long key,
                     int func,
                     void * expected,
                     void * newValue,
                     rbnode_t *parent,
                     rbnode_t *node,
                     version_t nodeOVL) {
        // As the search progresses there is an implicit min and max assumed for the
        // branch of the tree rooted at node. A left rotation of a node x results in
        // the range of keys in the right branch of x being reduced, so if we are at a
        // node and we wish to traverse to one of the branches we must make sure that
        // the node has not undergone a rotation since arriving from the parent.
        //
        // A rotation of node can't screw us up once we have traversed to node's
        // child, so we don't need to build a huge transaction, just a chain of
        // smaller read-only transactions.
        long cmp;
        char dirToC;

        assert (parent != node);
        assert (nodeOVL != UnlinkedOVL);

        cmp = key - node->key; 
        if (cmp == 0) {
            return attemptNodeUpdate(func, expected, newValue, parent, node);
        }

        dirToC = cmp < 0 ? LEFT : RIGHT;

        while (1) {
            rbnode_t *child = get_child(node, dirToC);

            if (hasShrunkOrUnlinked(nodeOVL, node->changeOVL)) {
                return SpecialRetry;
            }

            if (child == NULL) {
                // key is not present
                if (newValue == NULL) {
                    // Removal is requested.  Read of node.child occurred
                    // while parent.child was valid, so we were not affected
                    // by any shrinks.
                    return NULL;
                } else {
                    // Update will be an insert.
                    int success;
                    rbnode_t *damaged;
                    write_lock(node->lock);
                    {
                        // Validate that we haven't been affected by past
                        // rotations.  We've got the lock on node, so no future
                        // rotations can mess with us.
                        if (hasShrunkOrUnlinked(nodeOVL, node->changeOVL)) {
                            write_unlock(node->lock);
                            return SpecialRetry;
                        }

                        if (get_child(node, dirToC) != NULL) {
                            // Lost a race with a concurrent insert.  No need
                            // to back up to the parent, but we must RETRY in
                            // the outer loop of this method.
                            success = 0;
                            damaged = NULL;
                        } else {
                            // We're valid.  Does the user still want to
                            // perform the operation?
                            if (!shouldUpdate(func, NULL, expected)) {
                                write_unlock(node->lock);
                                return NULL;
                            }

                            // Create a new leaf
                            setChild(node, dirToC, create(key, newValue, node));
                            success = 1;

                            // attempt to fix node.height while we've still got
                            // the lock
                            damaged = fixHeight_nl(node);
                        }
                    }
                    write_unlock(node->lock);
                    if (success) {
                        fixHeightAndRebalance(damaged);
                        return NULL;
                    }
                    // else RETRY
                }
            } else {
                // non-null child
                version_t childOVL = child->changeOVL;
                if (isShrinkingOrUnlinked(childOVL)) {
                    waitUntilChangeCompleted(child, childOVL);
                    // RETRY
                } else if (child != get_child(node, dirToC)) {
                    // this second read is important, because it is protected
                    // by childOVL
                    // RETRY
                } else {
                    // validate the read that our caller took to get to node
                    if (hasShrunkOrUnlinked(nodeOVL, node->changeOVL)) {
                        return SpecialRetry;
                    }

                    // At this point we know that the traversal our parent took
                    // to get to node is still valid.  The recursive
                    // implementation will validate the traversal from node to
                    // child, so just prior to the nodeOVL validation both
                    // traversals were definitely okay.  This means that we are
                    // no longer vulnerable to node shrinks, and we don't need
                    // to validate nodeOVL any more.
                    void * vo = attemptUpdate(key, func, 
                                       expected, newValue, node, child, childOVL);
                    if (vo != SpecialRetry) {
                        return vo;
                    }
                    // else RETRY
                }
            }
        }
    }

static void * update(rbnode_t *tree, long key, int func, void *expected, void *newValue) {

        while (1) {
            rbnode_t *right = (rbnode_t *)tree->right;
            if (right == NULL) {
                // key is not present
                if (!shouldUpdate(func, NULL, expected) ||
                        newValue == NULL ||
                        attemptInsertIntoEmpty(tree, key, newValue)) {
                    // nothing needs to be done, or we were successful, prev value is Absent
                    return NULL;
                }
                // else RETRY
            } else {
                version_t ovl = right->changeOVL;
                if (isShrinkingOrUnlinked(ovl)) {
                    waitUntilChangeCompleted(right, ovl);
                    // RETRY
                } else if (right == tree->right) {
                    // this is the protected .right
                    void * vo = attemptUpdate(key, func, 
                                  expected, newValue, tree, right, ovl);
                    if (vo != SpecialRetry) {
                        return vo;
                    }
                    // else RETRY
                }
            }
        }
    }
    /** parent will only be used for unlink, update can proceed even if parent
     *  is stale.
     */
    void * attemptNodeUpdate( int func,
                              void * expected,
                              void * newValue,
                              rbnode_t *parent,
                              rbnode_t *node) {
        void *prev;

        if (newValue == NULL) {
            // removal
            if (node->value == NULL) {
                // This node is already removed, nothing to do.
                return NULL;
            }
        }

        if (newValue == NULL && (node->left == NULL || node->right == NULL)) {
            // potential unlink, get ready by locking the parent
            rbnode_t *damaged;
            write_lock(parent->lock);
            {
                if (isUnlinked(parent->changeOVL) || node->parent != parent) {
                    write_unlock(parent->lock);
                    return SpecialRetry;
                }

                write_lock(node->lock);
                {
                    prev = (void *)node->value;
                    if (prev == NULL || !shouldUpdate(func, prev, expected)) {
                        // nothing to do
                        write_unlock(node->lock);
                        write_unlock(parent->lock);
                        return prev;
                    }
                    if (!attemptUnlink_nl(parent, node)) {
                        write_unlock(node->lock);
                        write_unlock(parent->lock);
                        return SpecialRetry;
                    }
                }
                write_unlock(node->lock);

                // try to fix the parent while we've still got the lock
                damaged = fixHeight_nl(parent);
            }
            write_unlock(parent->lock);
            fixHeightAndRebalance(damaged);
            return prev;
        } else {
            // potential update (including remove-without-unlink)
            write_lock(node->lock);
            {
                // regular version changes don't bother us
                if (isUnlinked(node->changeOVL)) {
                    write_unlock(node->lock);
                    return SpecialRetry;
                }

                prev = (void *)node->value;
                if (!shouldUpdate(func, prev, expected)) {
                    write_unlock(node->lock);
                    return prev;
                }

                // retry if we now detect that unlink is possible
                if (newValue == NULL && (node->left == NULL || node->right == NULL)) {
                    write_unlock(node->lock);
                    return SpecialRetry;
                }

                // update in-place
                node->value = newValue;
                write_unlock(node->lock);
                return prev;
            }
            write_unlock(node->lock);
        }
    }

    /** Does not adjust the size or any heights. */
int attemptUnlink_nl(rbnode_t *parent, rbnode_t *node) {
        rbnode_t *parentL;
        rbnode_t * parentR;
        rbnode_t *left;
        rbnode_t *right;
        rbnode_t *splice;

        // assert (Thread.holdsLock(parent));
        // assert (Thread.holdsLock(node));
        assert (!isUnlinked(parent->changeOVL));

        parentL = (rbnode_t *)parent->left;
        parentR = (rbnode_t *)parent->right;
        if (parentL != node && parentR != node) {
            // node is no longer a child of parent
            return 0;
        }

        assert (!isUnlinked(node->changeOVL));
        assert (parent == node->parent);

        left = (rbnode_t *)node->left;
        right = (rbnode_t *)node->right;
        if (left != NULL && right != NULL) {
            // splicing is no longer possible
            return 0;
        }
        splice = left != NULL ? left : right;

        assert(splice != node);

        if (parentL == node) {
            parent->left = splice;
        } else {
            parent->right = splice;
        }
        if (splice != NULL) {
            write_lock(splice->lock);
            splice->parent = parent;
            write_unlock(splice->lock);
        }

        lock_mb();
        node->changeOVL = UnlinkedOVL;
        node->value = NULL;
        lock_mb();
        //printf("unlink %p %p %p\n", parent, node, splice);
        // NOTE: this is a hack to allow deeply nested routines to be able to
        //       see the root of the tree. This is necessary to allow rp_free
        //       to be passed the tree lock rather than the node lock.
        //       My_Tree is a thread local variable that is set by the
        //       public interface on each method call
        //
        //rp_free(My_Tree->lock, rbnode_free, node);
        // FIX THIS: not doing garbage collection

        return 1;
    }

    //////////////// tree balance and height info repair

static rbnode_t *rebalance_nl(rbnode_t *nParent, rbnode_t *n) ;
static rbnode_t *rebalanceToRight_nl(rbnode_t *nParent, rbnode_t *n,
                                       rbnode_t *nL, int hR0);
static rbnode_t *rebalanceToLeft_nl(rbnode_t *nParent, rbnode_t *n,
                                      rbnode_t *nL, int hR0);
static rbnode_t *rotateRight_nl(rbnode_t *nParent, rbnode_t *n,
                                  rbnode_t *nL, rbnode_t *nLR,
                                  int hR, int hLL, int hLR);
static rbnode_t *rotateLeft_nl(rbnode_t *nParent, rbnode_t *n,
                                 rbnode_t *nR, rbnode_t *nRL,
                                 int hL, int hRL, int hRR);
static rbnode_t *rotateLeftOverRight_nl(rbnode_t *nParent, rbnode_t *n,
                                          rbnode_t *nR, rbnode_t *nRL,
                                          int hL, int hRR, int hRLR);
static rbnode_t *rotateRightOverLeft_nl(rbnode_t *nParent, rbnode_t *n,
                                          rbnode_t *nL, rbnode_t *nLR,
                                          int hR, int hLL, int hLRL);

#define UnlinkRequired          -1
#define RebalanceRequired       -2
#define NothingRequired         -3

static int nodeCondition(rbnode_t *node) {
        // Begin atomic.

        int hN;
        int hL0;
        int hR0;
        int hNRepl;
        int bal;
        rbnode_t *nL = (rbnode_t *)node->left;
        rbnode_t *nR = (rbnode_t *)node->right;

        if ((nL == NULL || nR == NULL) && node->value== NULL) {
            return UnlinkRequired;
        }

        hN = node->height;
        hL0 = height(nL);
        hR0 = height(nR);

        // End atomic.  Since any thread that changes a node promises to fix
        // it, either our read was consistent (and a NothingRequired conclusion
        // is correct) or someone else has taken responsibility for either node
        // or one of its children.

        hNRepl = 1 + MAX(hL0, hR0);
        bal = hL0 - hR0;

        if (bal < -1 || bal > 1) {
            return RebalanceRequired;
        }

        return hN != hNRepl ? hNRepl : NothingRequired;
    }

static void fixHeightAndRebalance(rbnode_t *node) {
        while (node != NULL && node->parent != NULL) {
            int condition = nodeCondition(node);
            if (condition == NothingRequired || isUnlinked(node->changeOVL)) {
                // nothing to do, or no point in fixing this node
                return;
            }

            if (condition != UnlinkRequired && condition != RebalanceRequired) {
                rbnode_t *new_node;
                write_lock(node->lock);
                {
                    new_node = fixHeight_nl(node);
                }
                write_unlock(node->lock);
                node = new_node;
            } else {
                rbnode_t *nParent = (rbnode_t *)node->parent;
                write_lock(nParent->lock);
                {
                    if (!isUnlinked(nParent->changeOVL) && node->parent == nParent) {
                        rbnode_t *new_node;
                        write_lock(node->lock);
                        {
                            new_node = rebalance_nl(nParent, node);
                        }
                        write_unlock(node->lock);
                        node = new_node;
                    }
                    // else RETRY
                }
                write_unlock(nParent->lock);
            }
        }
    }

    /** Attempts to fix the height of a (locked) damaged node, returning the
     *  lowest damaged node for which this thread is responsible.  Returns null
     *  if no more repairs are needed.
     */
static rbnode_t *fixHeight_nl(rbnode_t *node) {
        int c = nodeCondition(node);
        switch (c) {
            case RebalanceRequired:
            case UnlinkRequired:
                // can't repair
                return node;
            case NothingRequired:
                // Any future damage to this node is not our responsibility.
                return NULL;
            default:
                node->height = c;
                // we've damaged our parent, but we can't fix it now
                return (rbnode_t *)node->parent;
        }
    }

    /** nParent and n must be locked on entry.  Returns a damaged node, or null
     *  if no more rebalancing is necessary.
     */
static rbnode_t *rebalance_nl(rbnode_t *nParent, rbnode_t *n) {
        int hN;
        int hL0;
        int hR0;
        int hNRepl;
        int bal;

        rbnode_t *tainted;
        rbnode_t *nL = (rbnode_t *)n->left;
        rbnode_t *nR = (rbnode_t *)n->right;

        if ((nL == NULL || nR == NULL) && n->value == NULL) {
            if (attemptUnlink_nl(nParent, n)) {
                // attempt to fix nParent.height while we've still got the lock
                return fixHeight_nl(nParent);
            } else {
                // retry needed for n
                return n;
            }
        }

        hN = n->height;
        hL0 = height(nL);
        hR0 = height(nR);
        hNRepl = 1 + MAX(hL0, hR0);
        bal = hL0 - hR0;

        if (bal > 1) {
            write_lock(nL->lock);
            tainted = rebalanceToRight_nl(nParent, n, nL, hR0);
            write_unlock(nL->lock);
            return tainted;
        } else if (bal < -1) {
            write_lock(nR->lock);
            tainted = rebalanceToLeft_nl(nParent, n, nR, hL0);
            write_unlock(nR->lock);
            return tainted;
        } else if (hNRepl != hN) {
            // we've got more than enough locks to do a height change, no need to
            // trigger a retry
            n->height = hNRepl;

            // nParent is already locked, let's try to fix it too
            return fixHeight_nl(nParent);
        } else {
            // nothing to do
            return NULL;
        }
    }

static rbnode_t *rebalanceToRight_nl(rbnode_t *nParent, rbnode_t *n,
                                       rbnode_t *nL, int hR0) {
        rbnode_t *result;

        // L is too large, we will rotate-right.  If L.R is taller
        // than L.L, then we will first rotate-left L.
        {
            int hL = nL->height;
            if (hL - hR0 <= 1) {
                return n; // retry
            } else {
                rbnode_t *nLR = (rbnode_t *)nL->right;
                int hLL0 = height((rbnode_t *)nL->left);
                int hLR0 = height(nLR);
                if (hLL0 >= hLR0) {
                    // rotate right based on our snapshot of hLR
                    if (nLR != NULL) write_lock(nLR->lock);
                    result = rotateRight_nl(nParent, n, nL, nLR, hR0, hLL0, hLR0);
                    if (nLR != NULL) write_unlock(nLR->lock);
                    return result;
                } else {
                    write_lock(nLR->lock);
                    {
                        // If our hLR snapshot is incorrect then we might
                        // actually need to do a single rotate-right on n.
                        int hLR = nLR->height;
                        if (hLL0 >= hLR) {
                            result = rotateRight_nl(nParent, n, nL, nLR, hR0, hLL0, hLR);
                            write_unlock(nLR->lock);
                            return result;
                        } else {
                            // If the underlying left balance would not be
                            // sufficient to actually fix n.left, then instead
                            // of rolling it into a double rotation we do it on
                            // it's own.  This may let us avoid rotating n at
                            // all, but more importantly it avoids the creation
                            // of damaged nodes that don't have a direct
                            // ancestry relationship.  The recursive call to
                            // rebalanceToRight_nl in this case occurs after we
                            // release the lock on nLR.
                            int hLRL = height((rbnode_t *)nLR->left);
                            int b = hLL0 - hLRL;
                            if (b >= -1 && b <= 1) {
                                // nParent.child.left won't be damaged after a double rotation
                                result = rotateRightOverLeft_nl(nParent, n, nL, nLR, 
                                                           hR0, hLL0, hLRL);
                                write_unlock(nLR->lock);
                                return result;
                            }
                        }
                    }
                    // focus on nL, if necessary n will be balanced later
                    result = rebalanceToLeft_nl(n, nL, nLR, hLL0);
                    write_unlock(nLR->lock);
                    return result;
                }
            }
        }
    }

static rbnode_t *rebalanceToLeft_nl(rbnode_t *nParent,
                                      rbnode_t *n,
                                      rbnode_t *nR,
                                      int hL0) {
        rbnode_t *result;

        {
            int hR = nR->height;
            if (hL0 - hR >= -1) {
                return n; // retry
            } else {
                rbnode_t *nRL = (rbnode_t *)nR->left;
                int hRL0 = height(nRL);
                int hRR0 = height((rbnode_t *)nR->right);
                if (hRR0 >= hRL0) {
                    if (nRL != NULL) write_lock(nRL->lock);
                    result = rotateLeft_nl(nParent, n, nR, nRL, hL0, hRL0, hRR0);
                    if (nRL != NULL) write_unlock(nRL->lock);
                    return result;
                } else {
                    write_lock(nRL->lock);
                    {
                        int hRL = nRL->height;
                        if (hRR0 >= hRL) {
                            result = rotateLeft_nl(nParent, n, nR, nRL, hL0, hRL, hRR0);
                            write_unlock(nRL->lock);
                            return result;
                        } else {
                            int hRLR = height((rbnode_t *)nRL->right);
                            int b = hRR0 - hRLR;
                            if (b >= -1 && b <= 1) {
                                result = rotateLeftOverRight_nl(nParent, n, 
                                                        nR, nRL, hL0, hRR0, hRLR);
                                write_unlock(nRL->lock);
                                return result;
                            }
                        }
                    }
                    result = rebalanceToRight_nl(n, nR, nRL, hRR0);
                    write_unlock(nRL->lock);
                    return result;
                }
            }
        }
    }

static rbnode_t *rotateRight_nl(rbnode_t *nParent,
                                  rbnode_t *n,
                                  rbnode_t *nL,
                                  rbnode_t *nLR,
                                  int hR,
                                  int hLL,
                                  int hLR) {
        int hNRepl;
        int balN;
        int balL;
        version_t nodeOVL = n->changeOVL;
        version_t leftOVL = nL->changeOVL;
        
        rbnode_t *nPL = (rbnode_t *)nParent->left;

        n->changeOVL = beginShrink(nodeOVL);
        nL->changeOVL = beginGrow(leftOVL);
        lock_mb();

        // Down links originally to shrinking nodes should be the last to change,
        // because if we change them early a search might bypass the OVL that
        // indicates its invalidity.  Down links originally from shrinking nodes
        // should be the first to change, because we have complete freedom when to
        // change them.  s/down/up/ and s/shrink/grow/ for the parent links.

        n->left = nLR;
        nL->right = n;
        if (nPL == n) {
            nParent->left = nL;
        } else {
            nParent->right = nL;
        }

        nL->parent = nParent;
        n->parent = nL;
        if (nLR != NULL) {
            nLR->parent = n;
        }

        // fix up heights links
        hNRepl = 1 + MAX(hLR, hR);
        n->height = hNRepl;
        nL->height = 1 + MAX(hLL, hNRepl);

        nL->changeOVL = endGrow(leftOVL);
        n->changeOVL = endShrink(nodeOVL);
        lock_mb();

        // We have damaged nParent, n (now parent.child.right), and nL (now
        // parent.child).  n is the deepest.  Perform as many fixes as we can
        // with the locks we've got.

        // We've already fixed the height for n, but it might still be outside
        // our allowable balance range.  In that case a simple fixHeight_nl
        // won't help.
        balN = hLR - hR;
        if (balN < -1 || balN > 1) {
            // we need another rotation at n
            return n;
        }

        // we've already fixed the height at nL, do we need a rotation here?
        balL = hLL - hNRepl;
        if (balL < -1 || balL > 1) {
            return nL;
        }

        // try to fix the parent height while we've still got the lock
        return fixHeight_nl(nParent);
    }

static rbnode_t *rotateLeft_nl(rbnode_t *nParent,
                                 rbnode_t *n,
                                 rbnode_t *nR,
                                 rbnode_t *nRL,
                                 int hL,
                                 int hRL,
                                 int hRR) {
        int  hNRepl;
        int balN;
        int balR;
        version_t nodeOVL = n->changeOVL;
        version_t rightOVL = nR->changeOVL;

        rbnode_t *nPL = (rbnode_t *)nParent->left;

        n->changeOVL = beginShrink(nodeOVL);
        nR->changeOVL = beginGrow(rightOVL);
        lock_mb();

        n->right = nRL;
        nR->left = n;
        if (nPL == n) {
            nParent->left = nR;
        } else {
            nParent->right = nR;
        }

        nR->parent = nParent;
        n->parent = nR;
        if (nRL != NULL) {
            nRL->parent = n;
        }

        // fix up heights
        hNRepl = 1 + MAX(hL, hRL);
        n->height = hNRepl;
        nR->height = 1 + MAX(hNRepl, hRR);

        nR->changeOVL = endGrow(rightOVL);
        n->changeOVL = endShrink(nodeOVL);
        lock_mb();

        balN = hRL - hL;
        if (balN < -1 || balN > 1) {
            return n;
        }

        balR = hRR - hNRepl;
        if (balR < -1 || balR > 1) {
            return nR;
        }

        return fixHeight_nl(nParent);
    }

static rbnode_t *rotateRightOverLeft_nl(rbnode_t *nParent,
                                          rbnode_t *n,
                                          rbnode_t *nL,
                                          rbnode_t *nLR,
                                          int hR,
                                          int hLL,
                                          int hLRL) {
        int hNRepl;
        int hLRepl;
        int balN;
        int balLR;

        version_t nodeOVL = n->changeOVL;
        version_t leftOVL = nL->changeOVL;
        version_t leftROVL = nLR->changeOVL;

        rbnode_t *nPL = (rbnode_t *)nParent->left;
        rbnode_t *nLRL = (rbnode_t *)nLR->left;
        rbnode_t *nLRR = (rbnode_t *)nLR->right;
        int hLRR = height(nLRR);

        n->changeOVL = beginShrink(nodeOVL);
        nL->changeOVL = beginShrink(leftOVL);
        nLR->changeOVL = beginGrow(leftROVL);
        lock_mb();

        n->left = nLRR;
        nL->right = nLRL;
        nLR->left = nL;
        nLR->right = n;
        if (nPL == n) {
            nParent->left = nLR;
        } else {
            nParent->right = nLR;
        }

        nLR->parent = nParent;
        nL->parent = nLR;
        n->parent = nLR;
        if (nLRR != NULL) {
            nLRR->parent = n;
        }
        if (nLRL != NULL) {
            nLRL->parent = nL;
        }

        // fix up heights
        hNRepl = 1 + MAX(hLRR, hR);
        n->height = hNRepl;
        hLRepl = 1 + MAX(hLL, hLRL);
        nL->height = hLRepl;
        nLR->height = 1 + MAX(hLRepl, hNRepl);

        nLR->changeOVL = endGrow(leftROVL);
        nL->changeOVL = endShrink(leftOVL);
        n->changeOVL = endShrink(nodeOVL);
        lock_mb();

        // caller should have performed only a single rotation if nL was going
        // to end up damaged
        assert(ABS(hLL - hLRL) <= 1);

        // We have damaged nParent, nLR (now parent.child), and n (now
        // parent.child.right).  n is the deepest.  Perform as many fixes as we
        // can with the locks we've got.

        // We've already fixed the height for n, but it might still be outside
        // our allowable balance range.  In that case a simple fixHeight_nl
        // won't help.
        balN = hLRR - hR;
        if (balN < -1 || balN > 1) {
            // we need another rotation at n
            return n;
        }

        // we've already fixed the height at nLR, do we need a rotation here?
        balLR = hLRepl - hNRepl;
        if (balLR < -1 || balLR > 1) {
            return nLR;
        }

        // try to fix the parent height while we've still got the lock
        return fixHeight_nl(nParent);
    }

static rbnode_t *rotateLeftOverRight_nl(rbnode_t *nParent,
                                          rbnode_t *n,
                                          rbnode_t *nR,
                                          rbnode_t *nRL,
                                          int hL,
                                          int hRR,
                                          int hRLR) {
        int hNRepl;
        int hRRepl;
        int balN;
        int balRL;

        version_t nodeOVL = n->changeOVL;
        version_t rightOVL = nR->changeOVL;
        version_t rightLOVL = nRL->changeOVL;

        rbnode_t *nPL = (rbnode_t *)nParent->left;
        rbnode_t *nRLL = (rbnode_t *)nRL->left;
        int hRLL = height(nRLL);
        rbnode_t *nRLR = (rbnode_t *)nRL->right;

        n->changeOVL = beginShrink(nodeOVL);
        nR->changeOVL = beginShrink(rightOVL);
        nRL->changeOVL = beginGrow(rightLOVL);
        lock_mb();

        n->right = nRLL;
        nR->left = nRLR;
        nRL->right = nR;
        nRL->left = n;
        if (nPL == n) {
            nParent->left = nRL;
        } else {
            nParent->right = nRL;
        }

        nRL->parent = nParent;
        nR->parent = nRL;
        n->parent = nRL;
        if (nRLL != NULL) {
            nRLL->parent = n;
        }
        if (nRLR != NULL) {
            nRLR->parent = nR;
        }

        // fix up heights
        hNRepl = 1 + MAX(hL, hRLL);
        n->height = hNRepl;
        hRRepl = 1 + MAX(hRLR, hRR);
        nR->height = hRRepl;
        nRL->height = 1 + MAX(hNRepl, hRRepl);

        nRL->changeOVL = endGrow(rightLOVL);
        nR->changeOVL = endShrink(rightOVL);
        n->changeOVL = endShrink(nodeOVL);
        lock_mb();

        assert(ABS(hRR - hRLR) <= 1);

        balN = hRLL - hL;
        if (balN < -1 || balN > 1) {
            return n;
        }
        balRL = hRRepl - hNRepl;
        if (balRL < -1 || balRL > 1) {
            return nRL;
        }
        return fixHeight_nl(nParent);
    }
//***********************************************
// public interface
//***********************************************
void rb_create(rbtree_t *tree, void *lock)
{
    tree->root = create(LONG_MIN,NULL,NULL);
    tree->restructure_copies = 0;
    tree->restructure_multi_copies = 0;
    tree->swap_copies = 0;
    tree->restructures = 0;
    tree->grace_periods = 0;
    tree->lock = lock;
}
//***********************************************
void *rb_find(rbtree_t *tree, long key)
{
    void *value;

    // NOTE: this is a hack to allow deeply nested routines to be able to
    //       see the root of the tree. This is necessary to allow rp_free
    //       to be passed the tree lock rather than the node lock.
    //My_Tree = tree;

    // NOTE: read_lock/read_unlock are to mark the readside critical section
    //       for memory reclaimation purposes. These are the RCU calls,
    //       so they are non-blocking
    //read_lock(tree->lock);
    value = get(tree->root, key);
    //read_unlock(tree->lock);

    return value;
}
//***********************************************
int rb_insert(rbtree_t *tree, long key, void *value)
{
    void *old_value;
    
    // NOTE: this is a hack to allow deeply nested routines to be able to
    //       see the root of the tree. This is necessary to allow rp_free
    //       to be passed the tree lock rather than the node lock.
    //My_Tree = tree;

    // NOTE: read_lock/read_unlock are to mark the readside critical section
    //       for memory reclaimation purposes. These are the RCU calls,
    //       so they are non-blocking
    //read_lock(tree->lock);
    old_value = put(tree->root, key, value);
    //read_unlock(tree->lock);

    if (old_value != NULL) return 0;

    //printf("Insert %ld\n", key);
    return 1;
}
//***********************************************
void *rb_remove(rbtree_t *tree, long key)
{
    void *value;
    
    // NOTE: this is a hack to allow deeply nested routines to be able to
    //       see the root of the tree. This is necessary to allow rp_free
    //       to be passed the tree lock rather than the node lock.
    //My_Tree = tree;

    // NOTE: read_lock/read_unlock are to mark the readside critical section
    //       for memory reclaimation purposes. These are the RCU calls,
    //       so they are non-blocking
    //read_lock(tree->lock);
    value = remove_node(tree->root, key);
    //read_unlock(tree->lock);

    //if (value != NULL) printf("remove %ld\n", key);
    return value;
}
//***********************************************
rbnode_t *rb_first_n(rbtree_t *tree)
{
    assert(!IMPLEMENTED);
    return NULL;
}
//***********************************************
void *rb_first(rbtree_t *tree, long *key)
{
    assert(!IMPLEMENTED);
    return NULL;
}
//***********************************************
rbnode_t *rb_next(rbnode_t *node)
{
    assert(!IMPLEMENTED);
    return NULL;
}
//***********************************************
void *rb_next_nln(rbtree_t *tree, long prev_key, long *key)
{
    assert(!IMPLEMENTED);
    return NULL;
}
//**************************************
static char *toString(volatile rbnode_t *node)
{
    static char buff[100];

    if (node==NULL) return "NULL";

    if (node->changeOVL & 0x07LL)
        sprintf(buff, "%d_%ld_0x%llX", node->height, node->key, node->changeOVL);
    else
        sprintf(buff, "%d_%ld", node->height, node->key);

    if (node->value == NULL) strcat(buff, " DELETED");
    return buff;
}
//**************************************
static int rb_node_invalid(volatile rbnode_t *node, int depth)
{
    if (node == NULL) return 0;
    
    if (ABS(height(node->left) - height(node->right)) > 1) return 1;
    if (node->height != 1+MAX(height(node->left), height(node->right))) return 2;
    if (node->changeOVL & 0x07LL) return 3;

    return 0;
}
//**************************************
static void output_list(volatile rbnode_t *node, int depth)
{
    if (depth > 32)
    {
        printf("Depth too deep for a valid tree\n");
        return;
    }

    if (node != NULL)
    {
        output_list(node->left, depth+1);
        printf("depth: %d value: %s", depth, toString(node));
        printf(" L: %s", toString(node->left));
        printf(" R: %s", toString(node->right));
        if (rb_node_invalid(node, depth)) 
            printf(" INVALID NODE: %d %llX\n", 
                    rb_node_invalid(node, depth), node->changeOVL);
        else
            printf("\n");
        output_list(node->right, depth+1);
    }
}
//**************************************
#define MAX_ROW_LENGTH 1000
static int o_nrows = 0;
static char **o_rows = NULL;
//************************************
static void o_resize()
{
    int row;
    unsigned int length = 0;

    for (row=0; row<o_nrows; row++)
    {
        if (strlen(o_rows[row]) > length) length = strlen(o_rows[row]);
    }

    for (row=0; row<o_nrows; row++)
    {
        while (strlen(o_rows[row]) < length) 
        {
            strcat(o_rows[row], " ");
        }
    }
}
//**************************************
static void output(volatile rbnode_t *node, int depth)
{
    if (node == NULL) return;

    if (depth >= o_nrows)
    {
        o_nrows++;
        o_rows = (char **)realloc(o_rows, o_nrows*sizeof(char *));
        o_rows[o_nrows-1] = (char *)malloc(MAX_ROW_LENGTH);
        o_rows[o_nrows-1][0] = 0;

        o_resize();
    }


    output(node->left, depth+1);

    strcat(o_rows[depth], toString(node));
    assert( (strlen(o_rows[depth]) < MAX_ROW_LENGTH) );
    o_resize();

    output(node->right, depth+1);
}
//**************************************
void rb_output_list(rbtree_t *tree)
{
    output_list(tree->root->right, 0);
}//**************************************
void rb_output(rbtree_t *tree)
{
    int ii;

    output(tree->root->right, 0);

    for (ii=0; ii<o_nrows; ii++)
    {
        printf("%s\n", o_rows[ii]);
        free(o_rows[ii]);
    }

    free(o_rows);
    o_rows = NULL;
    o_nrows = 0;

}
//**************************************
static int rec_invalid_node(volatile rbnode_t *node, int depth)
{
    int invalid;

    if (node == NULL) return 0;

    invalid = rb_node_invalid(node, depth);
    if (invalid) return invalid;

    invalid = rec_invalid_node(node->left, depth+1);
    if (invalid) return invalid;

    invalid = rec_invalid_node(node->right, depth+1);
    if (invalid) return invalid;

    return 0;
}
//***************************************
int rb_valid(rbtree_t *tree)
{
    return !rec_invalid_node(tree->root->right, 0);
}
//***************************************
void check_for(volatile rbnode_t *node, volatile rbnode_t *new_node)
{
    if (node == NULL) return;

    assert(node != new_node);

    check_for(node->left, new_node);
    check_for(node->right, new_node);
}
//**************************************
static int my_size(rbnode_t *tree)
{
    int size = 0;
    if (tree == NULL) return 0;
    if (tree->value != NULL) size++;
    size += my_size((rbnode_t *)tree->left);
    size += my_size((rbnode_t *)tree->right);

    return size;
}
//**************************************
int rb_size(rbtree_t *tree)
{
    return my_size(tree->root);
}
