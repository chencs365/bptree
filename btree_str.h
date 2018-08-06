#ifndef STX_BTREE_SLICE_H
#define STX_BTREE_SLICE_H

#include <stddef.h>

typedef struct btree_str_s {
    char *data;
    size_t size;
} btree_str_t;

btree_str_t btree_str(char *ptr, size_t size);

int btree_str_cmp(btree_str_t a, btree_str_t b);

btree_str_t btree_str_copy(btree_str_t arg);
#endif
