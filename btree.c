#include "btree.h"
#include "btree_str.h"
#include <stdio.h>
#include <stdlib.h>

static inline int bt_key_less(key_type a, key_type b) {
    return a < b ? 1 : 0;
}

static inline int bt_key_le(key_type a, key_type b) {
    return a <= b ? 1 : 0;
}

static inline int bt_key_equal(key_type a, key_type b) {
    return a == b ? 1 : 0;
}

/*
static inline int key_less(key_type a, key_type b) {
    return btree_str_cmp(a, b) < 0 ? 1 : 0;
}

static inline int key_equal(key_type a, key_type b) {
    return btree_str_cmp(a, b) == 0 ? 1 : 0;
}
*/

static inline btree_result_t btree_result(result_flags_t flags, key_type lastkey) {
    btree_result_t r;
    r.flags = flags;
    r.lastkey = lastkey;
    return r;
}

static inline btree_result_t brief_result(result_flags_t flags) {
    btree_result_t r;
    r.flags = flags;
    return r;
}

static inline int btree_result_has(btree_result_t r, result_flags_t f) {
    return (r.flags & f) != 0 ? 1 : 0;
}

static inline btree_result_t btree_result_or(btree_result_t r, btree_result_t other) {
    r.flags = (r.flags | other.flags);

    if (btree_result_has(other, btree_update_lastkey))
        r.lastkey = other.lastkey;

    return r;
}

static inline int bt_inode_full(btree_t *bt, btree_inode_t *node) {
    return (node->slotuse == 2 * bt->degree - 1) ? 1 : 0;
}

static inline int bt_inode_few(btree_t *bt, btree_inode_t *node) {
    return (node->slotuse <= bt->degree - 1) ? 1 : 0;
}

static inline int bt_inode_underflow(btree_t *bt, btree_inode_t *node) {
    return (node->slotuse < bt->degree - 1) ? 1 : 0;
}

static inline int bt_leaf_full(btree_t *bt, btree_fnode_t *node) {
    return (node->slotuse == 2 * bt->degree - 1) ? 1 : 0;
}

static inline int bt_leaf_few(btree_t *bt, btree_fnode_t *node) {
    return (node->slotuse <= bt->degree - 1) ? 1 : 0;
}

static inline int bt_leaf_underflow(btree_t *bt, btree_fnode_t *node) {
    return (node->slotuse < bt->degree - 1) ? 1 : 0;
}

static inline int bt_find_slot_inner(btree_inode_t *node, key_type key) {
    BTREE_ASSERT(node != NULL);
    int ix = 0;
    while (ix < node->slotuse && bt_key_less(node->keyslots[ix], key))
        ++ix;
    return ix;
}


static inline int bt_find_slot_leaf(btree_fnode_t *node, key_type key) {
    BTREE_ASSERT(node != NULL);
    int ix = 0;
    while (ix < node->slotuse && bt_key_less(node->keyslots[ix], key))
        ++ix;
    return ix;
}

static inline int bt_node_slots(void *node, int level) {
    BTREE_ASSERT(node != NULL);
    if (level == 0) return ((btree_fnode_t *) node)->slotuse;
    return ((btree_inode_t *) node)->slotuse;
}

static inline btree_fnode_t *bt_alloc_leaf(int maxleafslots) {
    btree_fnode_t *node = malloc(sizeof(btree_fnode_t));
    if (node == NULL) {
        return NULL;
    }
    node->prevleaf = NULL;
    node->nextleaf = NULL;
    node->level = 0;
    node->slotuse = 0;
    node->keyslots = malloc(maxleafslots * sizeof(key_type));
    node->dataslots = malloc(maxleafslots * sizeof(data_type));
    return node;
}

static inline void bt_free_leaf(btree_fnode_t *node) {
    free(node->keyslots);
    free(node->dataslots);
    free(node);
}

static inline btree_inode_t *bt_alloc_inner(int maxinnerslots, unsigned short level) {
    btree_inode_t *node = malloc(sizeof(btree_inode_t));
    if (node == NULL) {
        return NULL;
    }

    node->slotuse = 0;
    node->keyslots = malloc(maxinnerslots * sizeof(key_type));
    node->children = malloc((maxinnerslots + 1) * sizeof(data_type));
    node->level = level;
    return node;
}

static inline void bt_free_inode(btree_inode_t *node) {
    free(node->keyslots);
    free(node->children);
    free(node);
}

static inline void free_key(key_type key) {
    //free(key.data);
}


static void bt_split_leaf(btree_t *bt, btree_fnode_t *leaf, key_type *_newkey, void **_newleaf) {
    int i;
    unsigned int mid = (leaf->slotuse >> 1);
    btree_fnode_t *newleaf = bt_alloc_leaf(2 * bt->degree - 1);

    newleaf->nextleaf = leaf->nextleaf;
    if (newleaf->nextleaf == NULL) {
        bt->ftail = newleaf;
    } else {
        newleaf->nextleaf->prevleaf = newleaf;
    }

    for (i = mid; i < leaf->slotuse; ++i) {
        newleaf->keyslots[i - mid] = leaf->keyslots[i];
        newleaf->dataslots[i - mid] = leaf->dataslots[i];
    }
    newleaf->slotuse = leaf->slotuse - mid;
    leaf->slotuse = mid;

    leaf->nextleaf = newleaf;
    newleaf->prevleaf = leaf;

    *_newkey = leaf->keyslots[leaf->slotuse - 1];
    *_newleaf = newleaf;
}

static void
bt_split_inner(btree_t *bt, btree_inode_t *inner, key_type *_newkey, void **_newinner, unsigned int addslot) {
    int i;
    unsigned int mid = (inner->slotuse >> 1);
    if (addslot <= mid && mid > inner->slotuse - (mid + 1))
        mid--;

    btree_inode_t *newinner = bt_alloc_inner(2 * bt->degree - 1, inner->level);

    for (i = mid + 1; i < inner->slotuse; ++i) {
        newinner->keyslots[i - mid - 1] = inner->keyslots[i];
    }
    for (i = mid + 1; i <= inner->slotuse; ++i) {
        newinner->children[i - mid - 1] = inner->children[i];
    }
    newinner->slotuse = inner->slotuse - (mid + 1);
    inner->slotuse = mid;

    *_newkey = inner->keyslots[mid];
    *_newinner = newinner;
}

static int btree_insert_descend(btree_t *bt,
                                void *node, unsigned short level,
                                key_type key, data_type value,
                                key_type *splitkey, void **splitnode) {
    BTREE_ASSERT(bt != NULL);
    BTREE_ASSERT(level >= 0);
    BTREE_ASSERT(node != NULL);

    //key = btree_str_copy(key);
    int i, slot;
    if (level == 0) {
        btree_fnode_t *leaf = node;
        slot = bt_find_slot_leaf(leaf, key);
        if (slot < leaf->slotuse && bt_key_equal(key, leaf->keyslots[slot])) {
            leaf->dataslots[slot] = value;
            return 0;
        }

        if (bt_leaf_full(bt, leaf)) {
            bt_split_leaf(bt, leaf, splitkey, splitnode);
            if (slot >= leaf->slotuse) {
                slot -= leaf->slotuse;
                leaf = *splitnode;
            }
        }

        for (i = leaf->slotuse - 1; i >= slot; --i) {
            leaf->keyslots[i + 1] = leaf->keyslots[i];
            leaf->dataslots[i + 1] = leaf->dataslots[i];
        }

        leaf->keyslots[slot] = key;
        leaf->dataslots[slot] = value;
        leaf->slotuse++;

        if (splitnode && leaf != *splitnode && slot == leaf->slotuse - 1) {
            *splitkey = key;
        }

        return 0;
    } else {
        key_type newkey;
        void *newchild = NULL;
        btree_inode_t *splitinner;
        btree_inode_t *inner = node;

        slot = bt_find_slot_inner(inner, key);
        int r = btree_insert_descend(bt, inner->children[slot], inner->level - 1,
                                     key, value, &newkey, &newchild);

        btree_inode_t *fxxk = bt->root;

        if (newchild) {
            if (bt_inode_full(bt, inner)) {
                bt_split_inner(bt, inner, splitkey, splitnode, slot);
                if (slot == inner->slotuse + 1 && inner->slotuse < ((btree_inode_t *) *splitnode)->slotuse) {
                    splitinner = *splitnode;
                    inner->keyslots[inner->slotuse] = *splitkey;
                    inner->children[inner->slotuse + 1] = splitinner->children[0];
                    inner->slotuse++;
                    splitinner->children[0] = newchild;
                    *splitkey = newkey;

                    return r;
                } else if (slot >= inner->slotuse + 1) {
                    slot -= inner->slotuse + 1;
                    inner = *splitnode;
                }
            }

            for (i = inner->slotuse - 1; i >= slot; --i) {
                inner->keyslots[i + 1] = inner->keyslots[i];
            }
            for (i = inner->slotuse; i >= slot; --i) {
                inner->children[i + 1] = inner->children[i];
            }

            inner->keyslots[slot] = newkey;
            inner->children[slot + 1] = newchild;
            inner->slotuse++;
        }
        return r;
    }
}

int btree_insert(btree_t *bt, key_type key, data_type value) {
    int r;
    void *newchild = NULL;
    key_type newkey;

    pthread_rwlock_wrlock(&(bt->rwlock));
    if (bt->root == NULL) {
        bt->root = bt_alloc_leaf(2 * bt->degree - 1);
        bt->fhead = bt->root;
        bt->ftail = bt->root;
        bt->height = 1;
    }


    r = btree_insert_descend(bt, bt->root, bt->height - 1, key, value,
                             &newkey, &newchild);
    if (newchild) {
        btree_inode_t *newroot;
        newroot = bt_alloc_inner(2 * bt->degree - 1, bt->height);
        newroot->keyslots[0] = newkey;
        newroot->children[0] = bt->root;
        newroot->children[1] = newchild;
        newroot->slotuse = 1;
        bt->root = newroot;
        bt->height++;
    }
    pthread_rwlock_unlock(&(bt->rwlock));

    return r;
}

static btree_result_t bt_merge_leaves(btree_t *bt, btree_fnode_t *left,
                                   btree_fnode_t *right, btree_inode_t *parent) {
    int i;
    for (i = 0; i < right->slotuse; ++i) {
        left->keyslots[left->slotuse + i] = right->keyslots[i];
        left->dataslots[left->slotuse + i] = right->dataslots[i];
    }

    left->slotuse += right->slotuse;
    left->nextleaf = right->nextleaf;

    if (left->nextleaf)
        left->nextleaf->prevleaf = left;
    else
        bt->ftail = left;

    right->slotuse = 0;
    return brief_result(btree_fixmerge);
}

static btree_result_t bt_merge_inner(btree_inode_t *left, btree_inode_t *right,
                                  btree_inode_t *parent, unsigned int parentslot) {
    int i;
    left->keyslots[left->slotuse] = parent->keyslots[parentslot];
    left->slotuse++;

    for (i = 0; i < right->slotuse; ++i) {
        left->keyslots[left->slotuse + i] = right->keyslots[i];
    }
    for (i = 0; i <= right->slotuse; ++i) {
        left->children[left->slotuse + i] = right->children[i];
    }

    left->slotuse += right->slotuse;
    right->slotuse = 0;

    return brief_result(btree_fixmerge);
}

static btree_result_t bt_shift_left_leaf(btree_fnode_t *left, btree_fnode_t *right,
                                      btree_inode_t *parent, unsigned int parentslot) {
    int i;
    unsigned int shiftnum = (right->slotuse - left->slotuse) >> 1;
    BTREE_ASSERT(shiftnum > 0);

    for (i = 0; i < shiftnum; ++i) {
        left->keyslots[left->slotuse + i] = right->keyslots[i];
        left->dataslots[left->slotuse + i] = right->dataslots[i];
    }
    left->slotuse += shiftnum;

    for (i = shiftnum; i < right->slotuse; ++i) {
        right->keyslots[i - shiftnum] = right->keyslots[i];
        right->dataslots[i - shiftnum] = right->dataslots[i];
    }
    right->slotuse -= shiftnum;

    BTREE_ASSERT(right->slotuse > 0);
    if (parentslot < parent->slotuse) {
        parent->keyslots[parentslot] = left->keyslots[left->slotuse - 1];
        return brief_result(btree_ok);
    } else {
        return btree_result(btree_update_lastkey, left->keyslots[left->slotuse - 1]);
    }
}

static void bt_shift_left_inner(btree_inode_t *left, btree_inode_t *right,
                             btree_inode_t *parent, unsigned int parentslot) {
    int i;
    unsigned int shiftnum = (right->slotuse - left->slotuse) >> 1;

    left->keyslots[left->slotuse] = parent->keyslots[parentslot];
    left->slotuse++;

    for (i = 0; i < shiftnum - 1; ++i) {
        left->keyslots[left->slotuse + i] = right->keyslots[i];
    }
    for (i = 0; i < shiftnum; ++i) {
        left->children[left->slotuse + i] = right->children[i];
    }

    left->slotuse += shiftnum - 1;
    parent->keyslots[parentslot] = right->keyslots[shiftnum - 1];

    for (i = shiftnum; i < right->slotuse; ++i) {
        right->keyslots[i - shiftnum] = right->keyslots[i];
    }
    for (i = shiftnum; i <= right->slotuse; ++i) {
        right->children[i - shiftnum] = right->children[i];
    }

    right->slotuse -= shiftnum;
    BTREE_ASSERT(right->slotuse > 0);
}

static void bt_shift_right_leaf(btree_fnode_t *left, btree_fnode_t *right,
                             btree_inode_t *parent, unsigned int parentslot) {
    int i;
    unsigned int shiftnum = (left->slotuse - right->slotuse) >> 1;

    for (i = right->slotuse - 1; i >= 0; --i) {
        right->keyslots[i + shiftnum] = right->keyslots[i];
        right->dataslots[i + shiftnum] = right->dataslots[i];
    }
    right->slotuse += shiftnum;

    for (i = 0; i < shiftnum; ++i) {
        right->keyslots[i] = left->keyslots[left->slotuse - shiftnum + i];
        right->dataslots[i] = left->dataslots[left->slotuse - shiftnum + i];
    }

    left->slotuse -= shiftnum;
    parent->keyslots[parentslot] = left->keyslots[left->slotuse - 1];
    BTREE_ASSERT(left->slotuse > 0);
}

static void bt_shift_right_inner(btree_inode_t *left, btree_inode_t *right,
                              btree_inode_t *parent, unsigned int parentslot) {
    int i;
    unsigned int shiftnum = (left->slotuse - right->slotuse) >> 1;

    for (i = right->slotuse - 1; i >= 0; --i) {
        right->keyslots[i + shiftnum] = right->keyslots[i];
    }
    for (i = right->slotuse; i >= 0; --i) {
        right->children[i + shiftnum] = right->children[i];
    }

    right->slotuse += shiftnum;
    right->keyslots[shiftnum - 1] = parent->keyslots[parentslot];

    for (i = 0; i < shiftnum - 1; ++i) {
        right->keyslots[i] = left->keyslots[left->slotuse - shiftnum + i + 1];
    }
    for (i = 0; i <= shiftnum - 1; ++i) {
        right->children[i] = left->children[left->slotuse - shiftnum + i + 1];
    }

    parent->keyslots[parentslot] = left->keyslots[left->slotuse - shiftnum];
    left->slotuse -= shiftnum;
    BTREE_ASSERT(left->slotuse > 0);
}

btree_result_t btree_erase_descend(btree_t *bt,
                                   key_type key,
                                   void *curr, int level,
                                   btree_inode_t *parent, unsigned int parentslot) {
    if (level == 0) {
        int i;
        btree_fnode_t *leaf = curr;
        btree_fnode_t *leftleaf = NULL;
        btree_fnode_t *rightleaf = NULL;
        if (parent && parentslot < parent->slotuse) {
            rightleaf = parent->children[parentslot+1];
        }
        if (parent && parentslot > 0) {
            leftleaf = parent->children[parentslot-1];
        }

        int slot = bt_find_slot_leaf(leaf, key);
        if (slot >= leaf->slotuse || !bt_key_equal(key, leaf->keyslots[slot])) {
            return brief_result(btree_not_found);
        }
        /* delete leaf here */
        free_key(leaf->keyslots[slot]);
        for (i = slot + 1; i < leaf->slotuse; ++i) {
            leaf->keyslots[i - 1] = leaf->keyslots[i];
            leaf->dataslots[i - 1] = leaf->dataslots[i];
        }
        leaf->slotuse--;

        btree_result_t myres = brief_result(btree_ok);

        if (parent && slot == leaf->slotuse) {
            if (parentslot < parent->slotuse) {
                if (leaf->slotuse > 0) {
                    parent->keyslots[parentslot] = leaf->keyslots[leaf->slotuse - 1];
                }
            } else {
                if (leaf->slotuse > 0) {
                    myres = btree_result_or(myres, btree_result(btree_update_lastkey,
                                                                leaf->keyslots[leaf->slotuse - 1]));
                } else {
                    myres = btree_result_or(myres, btree_result(btree_update_lastkey,
                                                                leftleaf->keyslots[leftleaf->slotuse - 1]));
                }
            }
        }

        /* if not root adjust leaf */
        if (parent && bt_leaf_underflow(bt, leaf)) {
            if (parentslot == 0) {
                if (bt_leaf_few(bt, rightleaf))
                    myres = btree_result_or(myres, bt_merge_leaves(bt, leaf, rightleaf, parent));
                else
                    myres = btree_result_or(myres, bt_shift_left_leaf(leaf, rightleaf, parent, parentslot));
            } else if (parentslot == parent->slotuse)
                if (bt_leaf_few(bt, leftleaf))
                    myres = btree_result_or(myres, bt_merge_leaves(bt, leftleaf, leaf, parent));
                else
                    bt_shift_right_leaf(leftleaf, leaf, parent, parentslot-1);
            else {
                if (bt_leaf_few(bt, leftleaf) &&  bt_leaf_few(bt, rightleaf))
                    myres = btree_result_or(myres, bt_merge_leaves(bt, leaf, rightleaf, parent));
                else if (leftleaf->slotuse <= rightleaf->slotuse)
                    myres = btree_result_or(myres, bt_shift_left_leaf(leaf, rightleaf, parent, parentslot));
                else 
                    bt_shift_right_leaf(leftleaf, leaf, parent, parentslot - 1);
            }
            if (parentslot < parent->slotuse) BTREE_ASSERT(leaf->slotuse > 0);
        }

        return myres;
    } else {
        int i;
        btree_inode_t *inner = curr;
        btree_inode_t *leftinner = NULL;
        btree_inode_t *rightinner = NULL;
        BTREE_ASSERT(inner != NULL);

        if (parent && parentslot < parent->slotuse) {
            rightinner = parent->children[parentslot+1];
        }
        if (parent && parentslot > 0) {
            leftinner = parent->children[parentslot-1];
        }
        if (parent && parent->slotuse == 0) {
            printf("bug\n");
        }

        int slot = bt_find_slot_inner(inner, key);
        btree_result_t result = btree_erase_descend(bt, key,
                                                    inner->children[slot], level - 1,
                                                    inner, slot);

        btree_result_t myres = brief_result(btree_ok);
        if (btree_result_has(result, btree_not_found)) {
            return result;
        }

        if (btree_result_has(result, btree_update_lastkey)) {
            if (parent && parentslot < parent->slotuse) {
                parent->keyslots[parentslot] = result.lastkey;
            } else {
                myres = btree_result_or(myres, btree_result(btree_update_lastkey, result.lastkey));
            }
        }
        btree_fnode_t *child1 = inner->children[slot];
        if (btree_result_has(result, btree_fixmerge)) {
            if (bt_node_slots(inner->children[slot], inner->level - 1) != 0)
                slot++;
            level > 1 ? bt_free_inode(inner->children[slot]) : bt_free_leaf(inner->children[slot]);
            for (i = slot; i < inner->slotuse; ++i) {
                inner->keyslots[i - 1] = inner->keyslots[i];
                inner->children[i] = inner->children[i + 1];
            }
            inner->slotuse--;  

            if (inner->level == 1) {
                slot--;
                btree_fnode_t *child = inner->children[slot];
                inner->keyslots[slot] = child->keyslots[child->slotuse - 1];
            }
        }

        /* adjust inner node */
        if (bt_inode_underflow(bt, inner) && !(inner == bt->root && inner->slotuse >= 1)) {
            if (inner == bt->root) {
                bt->root = inner->children[0];
                bt->height -= 1;
                inner->slotuse = 0;
                bt_free_inode(inner);

                return brief_result(btree_ok);
            }

            int ret = bt_inode_underflow(bt, inner);

            if (parentslot == 0) {
                if (bt_inode_few(bt, rightinner))
                    myres = btree_result_or(myres, bt_merge_inner(inner, rightinner, parent, parentslot));
                else
                    bt_shift_left_inner(inner, rightinner, parent, parentslot);
            } else if (parentslot == parent->slotuse) {
                if (bt_inode_few(bt, leftinner))
                    myres = btree_result_or(myres, bt_merge_inner(leftinner, inner, parent, parentslot-1));
                else
                    bt_shift_right_inner(leftinner, inner, parent, parentslot-1);
            } else {
                if (bt_inode_few(bt, leftinner) && bt_inode_few(bt, rightinner))
                    myres = btree_result_or(myres, bt_merge_inner(inner, rightinner, parent, parentslot));
                else if (leftinner->slotuse <= rightinner->slotuse)
                    bt_shift_left_inner(inner, rightinner, parent, parentslot);
                else 
                    bt_shift_right_inner(leftinner, inner, parent, parentslot - 1);
            }

            if (parentslot < parent->slotuse) BTREE_ASSERT(inner->slotuse > 0);
        }
        return myres;

    }
}

int btree_erase(btree_t *bt, key_type key) {
    if (!bt->root) return -1;

    pthread_rwlock_wrlock(&(bt->rwlock));
    btree_result_t result = btree_erase_descend(bt, key, bt->root, bt->height - 1, NULL, 0);

    pthread_rwlock_unlock(&(bt->rwlock));
    return btree_result_has(result, btree_not_found);
}

btree_t *btree_create(unsigned short degree) {
    btree_t *bt = malloc(sizeof(btree_t));
    BTREE_ASSERT(bt != NULL);
    bt->degree = degree;

    bt->root = bt_alloc_leaf(2 * bt->degree - 1);
    BTREE_ASSERT(bt->root != NULL);
    bt->fhead = bt->root;
    bt->ftail = bt->root;
    bt->height = 1;
    pthread_rwlock_init(&(bt->rwlock), NULL);

    return bt;
}

data_type btree_search(btree_t *bt, key_type key) {
    int slot;

    pthread_rwlock_rdlock(&(bt->rwlock));
    void *n = bt->root;
    btree_inode_t *inner;
    btree_fnode_t *leaf;
    unsigned short level = bt->height - 1;
    if (!n) {
        pthread_rwlock_unlock(&(bt->rwlock));
        return NULL;
    }

    while (level > 0) {
        inner = n;
        slot = bt_find_slot_inner(inner, key);
        n = inner->children[slot];
        level--;
    }

    leaf = n;
    slot = bt_find_slot_leaf(leaf, key);
    data_type value = (slot < leaf->slotuse) ? leaf->dataslots[slot] : bt->ftail->dataslots[bt->ftail->slotuse-1];
    pthread_rwlock_unlock(&(bt->rwlock));
    return value;
}

void dump_node(void *n, int level) {
    btree_fnode_t *leaf;
    btree_inode_t *inner;
    void *subnode;
    int slot;
    if (level == 0) {
        leaf = n;
        printf("--------------------\n");
        printf("|      level=%d     |\n", leaf->level);
        printf("--------------------\n|");
        for (slot = 0; slot < leaf->slotuse; ++slot) {
            printf("  %d  |", leaf->keyslots[slot]);
        }
        printf("\n--------------------\n");
    } else {
        inner = n;
        printf("--------------------\n");
        printf("|      level=%d     |\n", inner->level);
        printf("--------------------\n|");
        for (slot = 0; slot < inner->slotuse; ++slot) {
            printf("  %d  |", inner->keyslots[slot]);
        }
        printf("\n--------------------\n");
        for (slot = 0; slot <= inner->slotuse; ++slot) {
            subnode = inner->children[slot];
            dump_node(subnode, level - 1);
        }
    }
}

int btree_split_cb(btree_t *bt, key_type oldkey, key_type newkey1, data_type value1, key_type newkey2, data_type value2) {
    BTREE_ASSERT(bt != NULL);

    if (btree_erase(bt, oldkey) != 0) {
        return -1;
    }
    if (btree_insert(bt, newkey1, value1) != 0) {
        return -1;
    }
    if (btree_insert(bt, newkey2, value2) != 0) {
        return -1;
    }
    return 0;
}

btree_iter_t *btree_iter(btree_t *bt) {
    if (bt == NULL) {
        return NULL;
    }
    btree_iter_t *it = malloc(sizeof(btree_iter_t));
    if (it == NULL) {
        return NULL;
    }

    it->node = bt->fhead;
    it->key = it->node->keyslots[0];
    it->value = it->node->dataslots[0];

    return it;
}

btree_iter_t *btree_iter_next(btree_iter_t *it) {
    if (it == NULL || it->node == NULL) {
        return NULL;
    }

    static int lo = 1;
    if (lo < it->node->slotuse) {
        it->key = it->node->keyslots[lo];
        it->value = it->node->dataslots[lo++];
        return it;
    }

    lo = 0;
    it->node = it->node->nextleaf;
    if (it->node == NULL) {
        free(it);
        return NULL;
    }

    it->key = it->node->keyslots[lo];
    it->value = it->node->dataslots[lo++];
    return it;
}

void btree_destory(btree_t *bt) {
}

int btree_adjust_cb(btree_t *bt, key_type oldkey, key_type newkey1, data_type value1) {
    BTREE_ASSERT(bt != NULL);

    if (btree_erase(bt, oldkey) != 0) {
        return -1;
    }

    if (btree_insert(bt, newkey1, value1) != 0) {
        return -1;
    }
    return 0;

}

void bt_verify_leaf(btree_t *bt, btree_fnode_t *leaf, key_type *minkey, key_type *maxkey) {
    BTREE_ASSERT(leaf != NULL);
    if (leaf != bt->root) {
        BTREE_ASSERT(leaf->slotuse != 0);
        BTREE_ASSERT(leaf->slotuse >= bt->degree-1);
    }

    BTREE_ASSERT(leaf->slotuse <= 2*bt->degree-1);
    int i;
    *minkey = leaf->keyslots[0];
    *maxkey = leaf->keyslots[leaf->slotuse-1];
    for (i = 1; i < leaf->slotuse; ++i) {
        BTREE_ASSERT(bt_key_less(leaf->keyslots[i-1], leaf->keyslots[i]) != 0);
    }
}

void bt_verify_inner(btree_t *bt, btree_inode_t *inner, key_type *minkey, key_type *maxkey) {
    BTREE_ASSERT(inner != NULL);
    if (inner != bt->root) {
        BTREE_ASSERT(inner->slotuse != 0);
        BTREE_ASSERT(inner->slotuse >= bt->degree-1);
    }
    BTREE_ASSERT(inner->slotuse <= 2*bt->degree-1);
    int i;
    key_type _minkey, _maxkey;
    *minkey = inner->keyslots[0];
    *maxkey = inner->keyslots[inner->slotuse-1];
    for (i = 1; i < inner->slotuse; ++i) {
        BTREE_ASSERT(bt_key_less(inner->keyslots[i-1], inner->keyslots[i]) != 0);
    }
    for (i = 0; i < inner->slotuse; ++i) {
        if (inner->level > 1) {
            bt_verify_inner(bt, inner->children[i], &_minkey, &_maxkey);
            BTREE_ASSERT(bt_key_less(_maxkey, inner->keyslots[i]) == 1);
            if (i > 0) {
                BTREE_ASSERT(bt_key_less(inner->keyslots[i-1], _minkey) == 1);
            }
        } else {
            bt_verify_leaf(bt, inner->children[i], &_minkey, &_maxkey);
            BTREE_ASSERT(bt_key_equal(_maxkey, inner->keyslots[i]) == 1);
            if (i > 0) {
                BTREE_ASSERT(bt_key_less(inner->keyslots[i-1], _minkey) == 1);
            }
        }
    }

    if (inner->level > 1) {
        bt_verify_inner(bt, inner->children[i], &_minkey, &_maxkey);
        if (i > 0) {
            BTREE_ASSERT(bt_key_less(inner->keyslots[i-1], _minkey) == 1);
        }
    } else {
        bt_verify_leaf(bt, inner->children[i], &_minkey, &_maxkey);
        if (i > 0) {
            BTREE_ASSERT(bt_key_less(inner->keyslots[i-1], _minkey) == 1);
        }
    }
}

void btree_verify(btree_t *bt) {
    key_type minkey, maxkey;
    BTREE_ASSERT(bt != NULL);
    BTREE_ASSERT(bt->root != NULL);
    if (bt->height > 1) {
        bt_verify_inner(bt, bt->root, &minkey, &maxkey);
    } else {
        bt_verify_leaf(bt, bt->root, &minkey, &maxkey);
    }
}
