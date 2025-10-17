#pragma once

#include "runtime/objects/strobj.h"
#include "runtime/worklist.h"

struct string_interner;

typedef enum {
    fly_string_node_red,
    fly_string_node_black,
} fly_string_node_color;

// Instead of red-black trees hashmap may provide better performance
// since we dont care about the order.
// rb trees are not simple to implementation
typedef struct fly_string_node {
    struct fly_string_node *left, *right, *parent;
    lu_string_t* str;
    fly_string_node_color color;
} fly_string_node_t;

typedef struct fly_string_node_iter {
    worklist_t queue;
} fly_string_node_iter_t;

static inline void fly_string_node_iter_init(fly_string_node_iter_t* iter,
                                             fly_string_node_t* root) {
    iter->queue.head = iter->queue.tail = nullptr;

    if (root) {
        worklist_enqueue(&iter->queue, root);
    }
}

static inline fly_string_node_t* fly_string_node_iter_next(
    fly_string_node_iter_t* iter) {
    fly_string_node_t* node = worklist_dequeue(&iter->queue);

    if (node && (node->left || node->right)) {
        if (node->left) {
            worklist_enqueue(&iter->queue, node->left);
        }
        if (node->right) {
            worklist_enqueue(&iter->queue, node->right);
        }
    }

    return node;
}

lu_string_t* fly_string_insert(struct string_interner* interner, char* str,
                               size_t str_len);
lu_string_t* fly_string_lookup(fly_string_node_t* root, char* str,
                               size_t str_len);
