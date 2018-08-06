#include "btree_str.h"
#include <stdlib.h>
#include <string.h>

inline btree_str_t btree_str(char *ptr, size_t size) {
    btree_str_t str;
    str.data = ptr;
    str.size = size;
    return str;
}

inline int btree_str_cmp(btree_str_t a, btree_str_t b) {
    size_t min_len = a.size < b.size ? a.size : b.size;
    int r = memcmp(a.data, b.data, min_len);
    if (r == 0) {
        if (a.size < b.size) {
            r = -1;
        } else if (a.size > b.size) {
            r = +1;
        }
    }

    return r;
}

inline btree_str_t btree_str_copy(btree_str_t arg) {
    btree_str_t str;
    str.data = malloc(arg.size);
    memcpy(str.data, arg.data, arg.size);
    str.size = arg.size;
    return str;
}


