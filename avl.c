#include <pthread.h>
#include <stdlib.h>
#include <assert.h>
#include <limits.h>

#include "avl.h"

#define IMPLEMENTED 1
#define MAX(a,b) ( (a) > (b) ? (a) : (b) )
#define ABS(a) ( (a) > 0 ? (a)  : -(a) )

    /** This is a special value that indicates the presence of a null value,
     *  to differentiate from the absence of a value.
     */
static avl_node_t t_SpecialNull;
static avl_node_t *SpecialNull = &t_SpecialNull;

    /** This is a special value that indicates that an optimistic read
     *  failed.
     */
static avl_node_t t_SpecialRetry;
static avl_node_t *SpecialRetry = &t_SpecialRetry;

    /** The number of spins before yielding. */
#define SPIN_COUNT 100

    /** The number of yields before blocking. */
#define YIELD_COUNT 0

#define OVL_BITS_BEFORE_OVERFLOW 8

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

static int hasChangedOrUnlinked(version_t orig, version_t current) {
        return orig != current;
    }

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

//*******************************************
avl_node_t *create(long key, int height, void *value,
        version_t version, avl_node_t *parent, 
        avl_node_t *left, avl_node_t *right)
{
    avl_node_t *new_node = (avl_node_t *)malloc(sizeof(avl_node_t));
    assert(new_node != NULL);

    new_node->key = key;
    new_node->height = height;
    new_node->value = value;
    new_node->changeOVL = version;
    new_node->parent = parent;
    new_node->left = left;
    new_node->right = right;
    new_node->lock = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
    assert(new_node->lock != NULL);
    pthread_mutex_init(new_node->lock, NULL);

    return new_node;
}

static avl_node_t *get_child(avl_node_t *node, char dir) 
    { return dir == LEFT ? (avl_node_t *)node->left : (avl_node_t *)node->right; }
static avl_node_t *childSibling(avl_node_t *node, char dir) 
    { return dir == LEFT ? (avl_node_t *)node->right : (avl_node_t *)node->left; }

// node should be locked
static void setChild(avl_node_t *node, char dir, avl_node_t *new_node) {
            if (dir == LEFT) {
                node->left = node;
            } else {
                node->right = node;
            }
        }

//////// per-node blocking
static void waitUntilChangeCompleted(avl_node_t *node, version_t ovl) {
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
            pthread_mutex_lock(node->lock);
            // we can't have gotten the lock unless the shrink was over
            pthread_mutex_unlock(node->lock);

            assert(node->changeOVL != ovl);
        }

/*
static int validatedHeight(avl_node_t *node) {
            final int hL = left == null ? 0 : left.validatedHeight();
            final int hR = right == null ? 0 : right.validatedHeight();
            assert(Math.abs(hL - hR) <= 1);
            final int h = 1 + Math.max(hL, hR);
            assert(h == height);
            return height;
        }
    }
*/
    //////// node access functions

static int height(avl_node_t *node) {
        return node == NULL ? 0 : node->height;
    }

static void * decodeNull(void *vOpt) {
        assert (vOpt != SpecialRetry);
        return vOpt == SpecialNull ? NULL : vOpt;
    }

static void *encodeNull(void *v) {
        return v == NULL ? SpecialNull : v;
    }

/*
    //////////////// state

    private final avl_node_t *rootHolder = new avl_node_t *1, null, null, 0L, null, null);
    private final EntrySet entries = new EntrySet();
*/

    //////////////// public interface

int  isEmpty(avl_node_t *tree) {
        // removed-but-not-unlinked nodes cannot be leaves, so if the tree is
        // truly empty then the root holder has no right child
        return tree->right == NULL;
    }

void clear() {
    assert(!IMPLEMENTED);
    }

    //////// search
static void *attemptGet(long key, avl_node_t *node, char dirToC, version_t nodeOVL);

    /** Returns either a value or SpecialNull, if present, or null, if absent. */
static void *getImpl(avl_node_t *tree, long key) {
    avl_node_t *right;
    version_t ovl;
    int rightCmp;
    void *vo;

        while (1) {
            right = (avl_node_t *)tree->right;
            if (right == NULL) {
                return NULL;
            } else {
                rightCmp = key - right->key;

                if (rightCmp == 0) {
                    // who cares how we got here
                    return (avl_node_t *)right->value;
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


/*
static int containsKey(avl_node_t *tree, long key) {
        return getImpl(tree, key) != NULL;
    }
*/

// return a value
void *get(avl_node_t *tree, long key) {
        return decodeNull(getImpl(tree, key));
    }

static void *attemptGet(long key,
                 avl_node_t *node,
                 char dirToC,
                 version_t nodeOVL) {
    avl_node_t *child;
    int childCmp;
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
                    return (avl_node_t *)child->value;
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

static void *attemptExtreme(int returnType,
                            char dir,
                            avl_node_t *node,
                            version_t nodeOVL) {
        while (1) {
            avl_node_t *child = (avl_node_t *)get_child(node, dir);

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

    /** Returns a key if returnKey is true, a SimpleImmutableEntry otherwise. */
static void *extreme(avl_node_t *tree, int returnType, char dir) {
        while (1) {
            avl_node_t *right = (avl_node_t *)tree->right;
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

long firstKey(avl_node_t *tree) {
        return (long) extreme(tree, ReturnKey, LEFT);
    }

avl_node_t *firstEntry(avl_node_t *tree) {
        return (avl_node_t *) extreme(tree, ReturnEntry, LEFT);
    }

long lastKey(avl_node_t *tree) {
        return (long) extreme(tree, ReturnKey, RIGHT);
    }

avl_node_t *lastEntry(avl_node_t *tree) {
        return (avl_node_t *) extreme(tree, ReturnEntry, RIGHT);
    }

//***********************************************************
    //////////////// update
//***********************************************************

#define UpdateAlways        0
#define UpdateIfAbsent      1
#define UpdateIfPresent     2
#define UpdateIfEq          3

static void * update(avl_node_t *tree, long key, int func, void * expected, void * newValue);
static void * attemptNodeUpdate(avl_node_t *tree, int func,
                         void * expected, void * newValue,
                         avl_node_t *parent, avl_node_t *node);
static avl_node_t *fixHeight_nl(avl_node_t *node);
static void fixHeightAndRebalance(avl_node_t *node);
static int attemptUnlink_nl(avl_node_t *parent, avl_node_t *node);

static int shouldUpdate(int func, void *prev, void *expected) {
        switch (func) {
            case UpdateAlways: return 1;
            case UpdateIfAbsent: return prev == NULL;
            case UpdateIfPresent: return prev != NULL;
            default: return prev == expected; // TODO: use .equals
        }
    }

// return previous value or NULL
void *put(avl_node_t *tree, long key, void *value) {
        return decodeNull(update(tree, key, UpdateAlways, NULL, encodeNull(value)));
    }

static void *putIfAbsent(avl_node_t *tree, long key, void * value) {
        return decodeNull(update(tree, key, UpdateIfAbsent, NULL, encodeNull(value)));
    }

/*
static void *replace(avl_node_t *tree, long key, void * value) {
        return decodeNull(update(tree, key, UpdateIfPresent, NULL, encodeNull(value)));
    }
*/

/*
static int replace_value(avl_node_t *tree, long key, void * oldValue, void * newValue) {
        return update(tree, key, UpdateIfEq, encodeNull(oldValue), encodeNull(newValue)) == encodeNull(oldValue);
    }
*/

static void * remove(avl_node_t *tree, long key) {
        return decodeNull(update(tree, key, UpdateAlways, NULL, NULL));
    }

static int remove_value(avl_node_t *tree, long key, void * value) {
        return update(tree, key, UpdateIfEq, encodeNull(value), NULL) == encodeNull(value);
    }


static int attemptInsertIntoEmpty(avl_node_t *tree, long key, void * vOpt) {
    pthread_mutex_lock(tree->lock);
            if (tree->right == NULL) {
                tree->right = create(key, 1, vOpt, 0, tree, NULL, NULL);
                tree->height = 2;
                pthread_mutex_unlock(tree->lock);
                return 1;
            } else {
                pthread_mutex_unlock(tree->lock);
                return 0;
            }
    }

    /** If successful returns the non-null previous value, SpecialNull for a
     *  null previous value, or null if not previously in the map.
     *  The caller should retry if this method returns SpecialRetry.
     */
static void * attemptUpdate(avl_node_t *tree, 
                     long key,
                     int func,
                     void * expected,
                     void * newValue,
                     avl_node_t *parent,
                     avl_node_t *node,
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
        int cmp;
        char dirToC;

        assert (nodeOVL != UnlinkedOVL);

        cmp = key - node->key; 
        if (cmp == 0) {
            return attemptNodeUpdate(tree, func, expected, newValue, parent, node);
        }

        dirToC = cmp < 0 ? LEFT : RIGHT;

        while (1) {
            avl_node_t *child = get_child(node, dirToC);

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
                    avl_node_t *damaged;
                    pthread_mutex_lock(node->lock);
                    {
                        // Validate that we haven't been affected by past
                        // rotations.  We've got the lock on node, so no future
                        // rotations can mess with us.
                        if (hasShrunkOrUnlinked(nodeOVL, node->changeOVL)) {
                            pthread_mutex_unlock(node->lock);
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
                                pthread_mutex_unlock(node->lock);
                                return NULL;
                            }

                            // Create a new leaf
                            setChild(node, dirToC, 
                                    create(key, 1, newValue, 0, node, NULL, NULL));
                            success = 1;

                            // attempt to fix node.height while we've still got
                            // the lock
                            damaged = fixHeight_nl(node);
                        }
                    }
                    pthread_mutex_unlock(node->lock);
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
                    void * vo = attemptUpdate(tree, key, func, 
                                       expected, newValue, node, child, childOVL);
                    if (vo != SpecialRetry) {
                        return vo;
                    }
                    // else RETRY
                }
            }
        }
    }

static void * update(avl_node_t *tree, long key, int func, void *expected, void *newValue) {

        while (1) {
            avl_node_t *right = (avl_node_t *)tree->right;
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
                    void * vo = attemptUpdate(tree, key, func, 
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
    void * attemptNodeUpdate(avl_node_t *tree, 
                                     int func,
                                     void * expected,
                                     void * newValue,
                                     avl_node_t *parent,
                                     avl_node_t *node) {
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
            avl_node_t *damaged;
            pthread_mutex_lock(parent->lock);
            {
                if (isUnlinked(parent->changeOVL) || node->parent != parent) {
                    pthread_mutex_unlock(parent->lock);
                    return SpecialRetry;
                }

                pthread_mutex_lock(node->lock);
                {
                    prev = (void *)node->value;
                    if (prev == NULL || !shouldUpdate(func, prev, expected)) {
                        // nothing to do
                        pthread_mutex_unlock(node->lock);
                        pthread_mutex_unlock(parent->lock);
                        return prev;
                    }
                    if (!attemptUnlink_nl(parent, node)) {
                        pthread_mutex_unlock(node->lock);
                        pthread_mutex_unlock(parent->lock);
                        return SpecialRetry;
                    }
                }
                pthread_mutex_unlock(node->lock);

                // try to fix the parent while we've still got the lock
                damaged = fixHeight_nl(parent);
            }
            pthread_mutex_unlock(parent->lock);
            fixHeightAndRebalance(damaged);
            return prev;
        } else {
            // potential update (including remove-without-unlink)
            pthread_mutex_lock(node->lock);
            {
                // regular version changes don't bother us
                if (isUnlinked(node->changeOVL)) {
                    pthread_mutex_unlock(node->lock);
                    return SpecialRetry;
                }

                prev = (void *)node->value;
                if (!shouldUpdate(func, prev, expected)) {
                    pthread_mutex_unlock(node->lock);
                    return prev;
                }

                // retry if we now detect that unlink is possible
                if (newValue == NULL && (node->left == NULL || node->right == NULL)) {
                    pthread_mutex_unlock(node->lock);
                    return SpecialRetry;
                }

                // update in-place
                node->value = newValue;
                pthread_mutex_unlock(node->lock);
                return prev;
            }
            pthread_mutex_unlock(node->lock);
        }
    }

    /** Does not adjust the size or any heights. */
int attemptUnlink_nl(avl_node_t *parent, avl_node_t *node) {
        avl_node_t *parentL;
        avl_node_t * parentR;
        avl_node_t *left;
        avl_node_t *right;
        avl_node_t *splice;

        // assert (Thread.holdsLock(parent));
        // assert (Thread.holdsLock(node));
        assert (!isUnlinked(parent->changeOVL));

        parentL = (avl_node_t *)parent->left;
        parentR = (avl_node_t *)parent->right;
        if (parentL != node && parentR != node) {
            // node is no longer a child of parent
            return 0;
        }

        assert (!isUnlinked(node->changeOVL));
        assert (parent == node->parent);

        left = (avl_node_t *)node->left;
        right = (avl_node_t *)node->right;
        if (left != NULL && right != NULL) {
            // splicing is no longer possible
            return 0;
        }
        splice = left != NULL ? left : right;

        if (parentL == node) {
            parent->left = splice;
        } else {
            parent->right = splice;
        }
        if (splice != NULL) {
            splice->parent = parent;
        }

        node->changeOVL = UnlinkedOVL;
        node->value = NULL;

        return 1;
    }

    //////////////// NavigableMap stuff
/*

    public Map.Entry<K,V> pollFirstEntry() {
        return pollExtremeEntry(LEFT);
    }

    public Map.Entry<K,V> pollLastEntry() {
        return pollExtremeEntry(RIGHT);
    }

    private Map.Entry<K,V> pollExtremeEntry(final char dir) {
        while (1) {
            final avl_node_t *right = rootHolder.right;
            if (right == null) {
                // tree is empty, nothing to remove
                return null;
            } else {
                final long ovl = right.changeOVL;
                if (isShrinkingOrUnlinked(ovl)) {
                    right.waitUntilChangeCompleted(ovl);
                    // RETRY
                } else if (right == rootHolder.right) {
                    // this is the protected .right
                    final Map.Entry<K,V> result = attemptRemoveExtreme(dir, rootHolder, right, ovl);
                    if (result != null) {
                        return result;
                    }
                    // else RETRY
                }
            }
        }
    }
*/
    /** Optimistic failure is returned as null. */
/*
    private Map.Entry<K,V> attemptRemoveExtreme(final char dir,
                                                final Node<K, V> parent,
                                                final Node<K, V> node,
                                                final long nodeOVL) {
        assert (nodeOVL != UnlinkedOVL);

        while (1) {
            final avl_node_t *child = node.child(dir);

            if (hasShrunkOrUnlinked(nodeOVL, node.changeOVL)) {
                return null;
            }

            if (child == null) {
                // potential unlink, get ready by locking the parent
                final Object vo;
                final avl_node_t *damaged;
                synchronized (parent) {
                    if (isUnlinked(parent.changeOVL) || node.parent != parent) {
                        return null;
                    }

                    synchronized (node) {
                        vo = node.vOpt;
                        if (node.child(dir) != null || !attemptUnlink_nl(parent, node)) {
                            return null;
                        }
                        // success!
                    }
                    // try to fix parent.height while we've still got the lock
                    damaged = fixHeight_nl(parent);
                }
                fixHeightAndRebalance(damaged);
                return new SimpleImmutableEntry<K,V>(node.key, decodeNull(vo));
            } else {
                // keep going down
                final long childOVL = child.changeOVL;
                if (isShrinkingOrUnlinked(childOVL)) {
                    child.waitUntilChangeCompleted(childOVL);
                    // RETRY
                } else if (child != node.child(dir)) {
                    // this second read is important, because it is protected
                    // by childOVL
                    // RETRY
                } else {
                    // validate the read that our caller took to get to node
                    if (hasShrunkOrUnlinked(nodeOVL, node.changeOVL)) {
                        return null;
                    }

                    final Map.Entry<K,V> result = attemptRemoveExtreme(dir, node, child, childOVL);
                    if (result != null) {
                        return result;
                    }
                    // else RETRY
                }
            }
        }
    }
*/


    //////////////// tree balance and height info repair

static avl_node_t *rebalance_nl(avl_node_t *nParent, avl_node_t *n) ;
static avl_node_t *rebalanceToRight_nl(avl_node_t *nParent, avl_node_t *n,
                                       avl_node_t *nL, int hR0);
static avl_node_t *rebalanceToLeft_nl(avl_node_t *nParent, avl_node_t *n,
                                      avl_node_t *nL, int hR0);
static avl_node_t *rotateRight_nl(avl_node_t *nParent, avl_node_t *n, avl_node_t *nL,
                                     int hR, int hLL, avl_node_t *nLR, int hLR);
static avl_node_t *rotateLeft_nl(avl_node_t *nParent, avl_node_t *n, int hL,
                                    avl_node_t *nR, avl_node_t *nRL,
                                    int hRL, int hRR);
static avl_node_t *rotateLeftOverRight_nl(avl_node_t *nParent, avl_node_t *n,
                                    int hL, avl_node_t *nR, avl_node_t *nRL,
                                    int hRR, int hRLR);
static avl_node_t *rotateRightOverLeft_nl(avl_node_t *nParent, avl_node_t *n,
                                    avl_node_t *nL, int hR, int hLL,
                                    avl_node_t *nLR, int hLRL);

#define UnlinkRequired          -1
#define RebalanceRequired       -2
#define NothingRequired         -3

static int nodeCondition(avl_node_t *node) {
        // Begin atomic.

        int hN;
        int hL0;
        int hR0;
        int hNRepl;
        int bal;
        avl_node_t *nL = (avl_node_t *)node->left;
        avl_node_t *nR = (avl_node_t *)node->right;

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

static void fixHeightAndRebalance(avl_node_t *node) {
        while (node != NULL && node->parent != NULL) {
            int condition = nodeCondition(node);
            if (condition == NothingRequired || isUnlinked(node->changeOVL)) {
                // nothing to do, or no point in fixing this node
                return;
            }

            if (condition != UnlinkRequired && condition != RebalanceRequired) {
                pthread_mutex_lock(node->lock);
                {
                    node = fixHeight_nl(node);
                }
                pthread_mutex_unlock(node->lock);
            } else {
                avl_node_t *nParent = (avl_node_t *)node->parent;
                pthread_mutex_lock(nParent->lock);
                {
                    if (!isUnlinked(nParent->changeOVL) && node->parent == nParent) {
                        pthread_mutex_lock(node->lock);
                        {
                            node = rebalance_nl(nParent, node);
                        }
                        pthread_mutex_unlock(node->lock);
                    }
                    // else RETRY
                }
                pthread_mutex_unlock(nParent->lock);
            }
        }
    }

    /** Attempts to fix the height of a (locked) damaged node, returning the
     *  lowest damaged node for which this thread is responsible.  Returns null
     *  if no more repairs are needed.
     */
static avl_node_t *fixHeight_nl(avl_node_t *node) {
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
                return (avl_node_t *)node->parent;
        }
    }

    /** nParent and n must be locked on entry.  Returns a damaged node, or null
     *  if no more rebalancing is necessary.
     */
static avl_node_t *rebalance_nl(avl_node_t *nParent, avl_node_t *n) {
        int hN;
        int hL0;
        int hR0;
        int hNRepl;
        int bal;

        avl_node_t *nL = (avl_node_t *)n->left;
        avl_node_t *nR = (avl_node_t *)n->right;

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
            return rebalanceToRight_nl(nParent, n, nL, hR0);
        } else if (bal < -1) {
            return rebalanceToLeft_nl(nParent, n, nR, hL0);
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

static avl_node_t *rebalanceToRight_nl(avl_node_t *nParent, avl_node_t *n,
                                       avl_node_t *nL, int hR0) {
        avl_node_t *result;

        // L is too large, we will rotate-right.  If L.R is taller
        // than L.L, then we will first rotate-left L.
        pthread_mutex_lock(nL->lock);
        {
            int hL = nL->height;
            if (hL - hR0 <= 1) {
                pthread_mutex_unlock(nL->lock);
                return n; // retry
            } else {
                avl_node_t *nLR = (avl_node_t *)nL->right;
                int hLL0 = height((avl_node_t *)nL->left);
                int hLR0 = height(nLR);
                if (hLL0 >= hLR0) {
                    // rotate right based on our snapshot of hLR
                    result = rotateRight_nl(nParent, n, nL, hR0, hLL0, nLR, hLR0);
                    pthread_mutex_unlock(nL->lock);
                    return result;
                } else {
                    pthread_mutex_lock(nLR->lock);
                    {
                        // If our hLR snapshot is incorrect then we might
                        // actually need to do a single rotate-right on n.
                        int hLR = nLR->height;
                        if (hLL0 >= hLR) {
                            result = rotateRight_nl(nParent, n, nL, hR0, hLL0, nLR, hLR);
                            pthread_mutex_unlock(nLR->lock);
                            pthread_mutex_unlock(nL->lock);
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
                            int hLRL = height((avl_node_t *)nLR->left);
                            int b = hLL0 - hLRL;
                            if (b >= -1 && b <= 1) {
                                // nParent.child.left won't be damaged after a double rotation
                                result = rotateRightOverLeft_nl(nParent, n, nL, 
                                                           hR0, hLL0, nLR, hLRL);
                                pthread_mutex_unlock(nLR->lock);
                                pthread_mutex_unlock(nL->lock);
                                return result;
                            }
                        }
                    }
                    pthread_mutex_unlock(nLR->lock);
                    // focus on nL, if necessary n will be balanced later
                    result = rebalanceToLeft_nl(n, nL, nLR, hLL0);
                    pthread_mutex_unlock(nL->lock);
                    return result;
                }
            }
        }
        pthread_mutex_unlock(nL->lock);
    }

static avl_node_t *rebalanceToLeft_nl(avl_node_t *nParent,
                                      avl_node_t *n,
                                      avl_node_t *nR,
                                      int hL0) {
        avl_node_t *result;

        pthread_mutex_lock(nR->lock);
        {
            int hR = nR->height;
            if (hL0 - hR >= -1) {
                pthread_mutex_unlock(nR->lock);
                return n; // retry
            } else {
                avl_node_t *nRL = (avl_node_t *)nR->left;
                int hRL0 = height(nRL);
                int hRR0 = height((avl_node_t *)nR->right);
                if (hRR0 >= hRL0) {
                    result = rotateLeft_nl(nParent, n, hL0, nR, nRL, hRL0, hRR0);
                    pthread_mutex_unlock(nR->lock);
                    return result;
                } else {
                    pthread_mutex_lock(nRL->lock);
                    {
                        int hRL = nRL->height;
                        if (hRR0 >= hRL) {
                            result = rotateLeft_nl(nParent, n, hL0, nR, nRL, hRL, hRR0);
                            pthread_mutex_unlock(nRL->lock);
                            pthread_mutex_unlock(nR->lock);
                            return result;
                        } else {
                            int hRLR = height((avl_node_t *)nRL->right);
                            int b = hRR0 - hRLR;
                            if (b >= -1 && b <= 1) {
                                result = rotateLeftOverRight_nl(nParent, n, hL0, 
                                                        nR, nRL, hRR0, hRLR);
                                pthread_mutex_unlock(nRL->lock);
                                pthread_mutex_unlock(nR->lock);
                                return result;
                            }
                        }
                    }
                    pthread_mutex_unlock(nRL->lock);
                    result = rebalanceToRight_nl(n, nR, nRL, hRR0);
                    pthread_mutex_unlock(nR->lock);
                    return result;
                }
            }
        }
        pthread_mutex_unlock(nR->lock);
    }

static avl_node_t *rotateRight_nl(avl_node_t *nParent,
                                  avl_node_t *n,
                                  avl_node_t *nL,
                                  int hR,
                                  int hLL,
                                  avl_node_t *nLR,
                                  int hLR) {
        int hNRepl;
        int balN;
        int balL;
        version_t nodeOVL = n->changeOVL;
        version_t leftOVL = nL->changeOVL;
        
        avl_node_t *nPL = (avl_node_t *)nParent->left;

        n->changeOVL = beginShrink(nodeOVL);
        nL->changeOVL = beginGrow(leftOVL);

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

static avl_node_t *rotateLeft_nl(avl_node_t *nParent,
                                 avl_node_t *n,
                                 int hL,
                                 avl_node_t *nR,
                                 avl_node_t *nRL,
                                 int hRL,
                                 int hRR) {
        int  hNRepl;
        int balN;
        int balR;
        version_t nodeOVL = n->changeOVL;
        version_t rightOVL = nR->changeOVL;

        avl_node_t *nPL = (avl_node_t *)nParent->left;

        n->changeOVL = beginShrink(nodeOVL);
        nR->changeOVL = beginGrow(rightOVL);

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

static avl_node_t *rotateRightOverLeft_nl(avl_node_t *nParent,
                                          avl_node_t *n,
                                          avl_node_t *nL,
                                          int hR,
                                          int hLL,
                                          avl_node_t *nLR,
                                          int hLRL) {
        int hNRepl;
        int hLRepl;
        int balN;
        int balLR;

        version_t nodeOVL = n->changeOVL;
        version_t leftOVL = nL->changeOVL;
        version_t leftROVL = nLR->changeOVL;

        avl_node_t *nPL = (avl_node_t *)nParent->left;
        avl_node_t *nLRL = (avl_node_t *)nLR->left;
        avl_node_t *nLRR = (avl_node_t *)nLR->right;
        int hLRR = height(nLRR);

        n->changeOVL = beginShrink(nodeOVL);
        nL->changeOVL = beginShrink(leftOVL);
        nLR->changeOVL = beginGrow(leftROVL);

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

static avl_node_t *rotateLeftOverRight_nl(avl_node_t *nParent,
                                          avl_node_t *n,
                                          int hL,
                                          avl_node_t *nR,
                                          avl_node_t *nRL,
                                          int hRR,
                                          int hRLR) {
        int hNRepl;
        int hRRepl;
        int balN;
        int balRL;

        version_t nodeOVL = n->changeOVL;
        version_t rightOVL = nR->changeOVL;
        version_t rightLOVL = nRL->changeOVL;

        avl_node_t *nPL = (avl_node_t *)nParent->left;
        avl_node_t *nRLL = (avl_node_t *)nRL->left;
        int hRLL = height(nRLL);
        avl_node_t *nRLR = (avl_node_t *)nRL->right;

        n->changeOVL = beginShrink(nodeOVL);
        nR->changeOVL = beginShrink(rightOVL);
        nRL->changeOVL = beginGrow(rightLOVL);

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
void avl_create(avl_node_t *tree, void *lock)
{
    tree->key = LONG_MIN;
    tree->height = 0;
    tree->value = NULL;
    tree->changeOVL = 0;
    tree->parent = NULL;
    tree->left = NULL;
    tree->right = NULL;
    tree->lock = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
    assert(tree->lock != NULL);
    pthread_mutex_init(tree->lock, NULL);
}
//***********************************************
void *avl_find(avl_node_t *tree, long key)
{
}
//***********************************************
int avl_insert(avl_node_t *tree, long key, void *value)
{
    void *old_value;
    
    old_value = put(tree, key, value);

    if (old_value == NULL)
        return 0;
    else
        return 1;
}
//***********************************************
void *avl_remove(avl_node_t *tree, long key)
{
    void *value;
    
    value = remove(tree, key);

    return value;
}
//***********************************************
avl_node_t *avl_first_n(avl_node_t *tree)
{
    return NULL;
}
//***********************************************
void *avl_first(avl_node_t *tree, long *key)
{
    return NULL;
}
//***********************************************
avl_node_t *avl_next_n(avl_node_t *node)
{
    return NULL;
}
//***********************************************
void *avl_next(avl_node_t *tree, long prev_key, long *key)
{
    return NULL;
}
//***********************************************
void avl_output_list(avl_node_t *tree)
{
}
//***********************************************
void avl_output(avl_node_t *tree)
{
}
//***********************************************
int avl_valid(avl_node_t *tree)
{
    return 1;
}

/*
// get a value
void *get(avl_node_t *tree, long key);

// return previous value
void *put(avl_node_t *tree, long key, void * value);

// return previous value
void *remove(avl_node_t *tree, long key);
*/
//***********************************************
// public interface
//***********************************************

#ifdef REMOVE

    //////////////// iteration (node successor)

    @SuppressWarnings("unchecked")
    private avl_node_t *firstNode() {
        return (avl_node_t *)extreme(ReturnNode, LEFT);
    }

    /** Returns the successor to a node, or null if no successor exists. */
    @SuppressWarnings("unchecked")
    private avl_node_t *succ(final avl_node_t *node) {
        while (1) {
            final Object z = attemptSucc(node);
            if (z != SpecialRetry) {
                return (avl_node_t *z;
            }
        }
    }

    private Object attemptSucc(final avl_node_t *node) {
        if (isUnlinked(node.changeOVL)) {
            return succOfUnlinked(node);
        }

        final avl_node_t *right = node.right;
        if (right != null) {
            // If right undergoes a right rotation then its first won't be our
            // successor.  We need to recheck node.right after guaranteeing that
            // right can't shrink.  We actually don't care about shrinks or grows of
            // node itself once we've gotten hold of a right node.
            final long rightOVL = right.changeOVL;
            if (isShrinkingOrUnlinked(rightOVL)) {
                right.waitUntilChangeCompleted(rightOVL);
                return SpecialRetry;
            }

            if (node.right != right) {
                return SpecialRetry;
            }

            return attemptExtreme(ReturnNode, LEFT, right, rightOVL);
        } else {
            final long nodeOVL = node.changeOVL;
            if (isChangingOrUnlinked(nodeOVL)) {
                node.waitUntilChangeCompleted(nodeOVL);
                return SpecialRetry;
            }

            // This check of node.right is the one that is protected by the nodeOVL
            // check in succUp().
            if (node.right != null) {
                return SpecialRetry;
            }

            return succUp(node, nodeOVL);
        }
    }

    private Object succUp(final avl_node_t *node, final long nodeOVL) {
        if (node == rootHolder) {
            return null;
        }

        while (1) {
            final avl_node_t *parent = node.parent;
            final long parentOVL = parent.changeOVL;
            if (isChangingOrUnlinked(parentOVL)) {
                parent.waitUntilChangeCompleted(parentOVL);

                if (hasChangedOrUnlinked(nodeOVL, node.changeOVL)) {
                    return SpecialRetry;
                }
                // else just RETRY at this level
            } else if (node == parent.left) {
                // This check validates the caller's test in which node.right was not
                // an adequate successor.  In attemptSucc that test is .right==null, in
                // our recursive parent that test is parent.right==node.
                if (hasChangedOrUnlinked(nodeOVL, node.changeOVL)) {
                    return SpecialRetry;
                }

                // Parent is the successor.  We don't care whether or not the parent
                // has grown, because we know that we haven't and there aren't any
                // nodes in between the parent and ourself.
                return parent;
            } else if (node != parent.right) {
                if (hasChangedOrUnlinked(nodeOVL, node.changeOVL)) {
                    return SpecialRetry;
                }
                // else RETRY at this level
            } else {
                // This is the last check of node.changeOVL (unless the parent
                // fails).  After this point we are immune to growth of node.
                if (hasChangedOrUnlinked(nodeOVL, node.changeOVL)) {
                    return SpecialRetry;
                }

                final Object z = succUp(parent, parentOVL);
                if (z != SpecialRetry) {
                    return z;
                }
                // else RETRY at this level
            }
        }
    }

    /** Returns the successor to an unlinked node. */
    private Object succOfUnlinked(final avl_node_t *node) {
        return succNode(node.key);
    }

    private Object succNode(final K key) {
        final Comparable<? super K> keyCmp = comparable(key);

        while (1) {
            final avl_node_t *right = rootHolder.right;
            if (right == null) {
                return null;
            }

            final long ovl = right.changeOVL;
            if (isShrinkingOrUnlinked(ovl)) {
                right.waitUntilChangeCompleted(ovl);
                // RETRY
            } else if (right == rootHolder.right) {
                // note that the protected read of root.right is actually the one in
                // the if(), not the read that initialized right
                final Object z = succNode(keyCmp, right, ovl);
                if (z != SpecialRetry) {
                    return z;
                }
                // else RETRY
            }
        }
    }

    private Object succNode(final Comparable<? super K> keyCmp, final avl_node_t *node, final long nodeOVL) {
        while (1) {
            final int cmp = keyCmp.compareTo(node.key);

            if (cmp >= 0) {
                // node.key <= keyCmp, so succ is on right branch
                final avl_node_t *right = node.right;
                if (right == null) {
                    if (hasShrunkOrUnlinked(nodeOVL, node.changeOVL)) {
                        return SpecialRetry;
                    }
                    return null;
                } else {
                    final long rightOVL = right.changeOVL;
                    if (isShrinkingOrUnlinked(rightOVL)) {
                        right.waitUntilChangeCompleted(rightOVL);

                        if (hasShrunkOrUnlinked(nodeOVL, node.changeOVL)) {
                            return SpecialRetry;
                        }
                      // else RETRY
                    } else if (right != node.right) {
                        // this second read is important, because it is protected by rightOVL

                        if (hasShrunkOrUnlinked(nodeOVL, node.changeOVL)) {
                            return SpecialRetry;
                        }
                        // else RETRY
                    } else {
                        if (hasShrunkOrUnlinked(nodeOVL, node.changeOVL)) {
                            return SpecialRetry;
                        }

                        final Object z = succNode(keyCmp, right, rightOVL);
                        if (z != SpecialRetry) {
                            return z;
                        }
                        // else RETRY
                    }
                }
            } else {
                // succ is either on the left branch or is node
                final avl_node_t *left = node.left;
                if (left == null) {
                    if (hasShrunkOrUnlinked(nodeOVL, node.changeOVL)) {
                        return SpecialRetry;
                    }

                    return node;
                } else {
                    final long leftOVL = left.changeOVL;
                    if (isShrinkingOrUnlinked(leftOVL)) {
                        left.waitUntilChangeCompleted(leftOVL);

                        if (hasShrunkOrUnlinked(nodeOVL, node.changeOVL)) {
                            return SpecialRetry;
                        }
                        // else RETRY at this level
                    } else if (left != node.left) {
                        if (hasShrunkOrUnlinked(nodeOVL, node.changeOVL)) {
                            return SpecialRetry;
                        }
                        // else RETRY at this level
                    } else {
                        if (hasShrunkOrUnlinked(nodeOVL, node.changeOVL)) {
                            return SpecialRetry;
                        }
                        final Object z = succNode(keyCmp, left, leftOVL);
                        if (z != SpecialRetry) {
                            return z == null ? node : z;
                        }
                       // else RETRY
                    }
                }
            }
        }
    }

    //////////////// views

    @Override
    public Set<Map.Entry<K,V>> entrySet() {
        return entries;
    }

    private class EntrySet extends AbstractSet<Entry<K,V>> {

        @Override
        public int size() {
            return OptTreeMap.this.size();
        }

        @Override
        public boolean isEmpty() {
            return OptTreeMap.this.isEmpty();
        }

        @Override
        public void clear() {
            OptTreeMap.this.clear();
        }

        @Override
        public boolean contains(final Object o) {
            if (!(o instanceof Entry<?,?>)) {
                return 0;
            }
            final Object k = ((Entry<?,?>)o).getKey();
            final Object v = ((Entry<?,?>)o).getValue();
            final Object actualVo = OptTreeMap.this.getImpl(k);
            if (actualVo == null) {
                // no associated value
                return 0;
            }
            final V actual = decodeNull(actualVo);
            return v == null ? actual == null : v.equals(actual);
        }

        @Override
        public boolean add(final Entry<K,V> e) {
            final Object v = encodeNull(e.getValue());
            return update(e.getKey(), UpdateAlways, null, v) != v;
        }

        @Override
        public boolean remove(final Object o) {
            if (!(o instanceof Entry<?,?>)) {
                return 0;
            }
            final Object k = ((Entry<?,?>)o).getKey();
            final Object v = ((Entry<?,?>)o).getValue();
            return OptTreeMap.this.remove(k, v);
        }

        @Override
        public Iterator<Entry<K,V>> iterator() {
            return new EntryIter();
        }
    }

    private class EntryIter implements Iterator<Entry<K,V>> {
        private avl_node_t *cp;
        private SimpleImmutableEntry<K,V> availEntry;
        private avl_node_t *availNode;
        private avl_node_t *mostRecentNode;

        EntryIter() {
            cp = firstNode();
            advance();
        }

        private void advance() {
            while (cp != null) {
                final K k = cp.key;
                final Object vo = cp.vOpt;
                availNode = cp;
                cp = succ(cp);
                if (vo != null) {
                    availEntry = new SimpleImmutableEntry<K,V>(k, decodeNull(vo));
                    return;
                }
            }
            availEntry = null;
        }

        @Override
        public boolean hasNext() {
            return availEntry != null;
        }

        @Override
        public Map.Entry<K,V> next() {
            mostRecentNode = availNode;
            final Map.Entry<K,V> z = availEntry;
            advance();
            return z;
        }

        @Override
        public void remove() {
            OptTreeMap.this.remove(mostRecentNode.key);
        }
    }
}
#endif

