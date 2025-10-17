#include "strings/fly_string.h"

#include <stdint.h>
#include <string.h>

#include "arena.h"
#include "runtime/heap.h"
#include "runtime/objects/strobj.h"
#include "strings/interner.h"
#include "strings/view.h"

static int64_t lu_strcmp(char* a, char* b, size_t a_len, size_t b_len) {
    size_t min_len = a_len > b_len ? b_len : a_len;
    int64_t cmp_res = memcmp(a, b, min_len);
    if (cmp_res != 0) return cmp_res;
    return a_len < b_len ? -1 : a_len > b_len;
}

static void __rotate_left(string_interner_t* interner,
                          fly_string_node_t* node) {
    fly_string_node_t* pivot = node->right;
    fly_string_node_t* parent = node->parent;

    node->right = pivot->left;
    if (node->right) {
        node->right->parent = node;
    }

    pivot->left = node;
    node->parent = pivot;
    pivot->parent = parent;

    if (!parent) {
        interner->root = pivot;
    } else if (parent->left == node) {
        parent->left = pivot;
    } else {
        parent->right = pivot;
    }
}

static void __rotate_right(string_interner_t* interner,
                           fly_string_node_t* node) {
    fly_string_node_t* pivot = node->left;
    fly_string_node_t* parent = node->parent;

    node->left = pivot->right;
    if (node->left) {
        node->left->parent = node;
    }

    pivot->right = node;
    node->parent = pivot;
    pivot->parent = parent;

    if (!parent) {
        interner->root = pivot;
    } else if (parent->left == node) {
        parent->left = pivot;
    } else {
        parent->right = pivot;
    }
}

static void __fix_insertion(string_interner_t* interner,
                            fly_string_node_t* node) {
    while (node->color == fly_string_node_red &&
           node->parent->color == fly_string_node_red) {
        fly_string_node_t* grandparent = node->parent->parent;
        if (grandparent->left == node->parent) {
            fly_string_node_t* uncle = grandparent->right;
            if (uncle && uncle->color == fly_string_node_red) {
                grandparent->color = fly_string_node_red;
                uncle->color = node->parent->color = fly_string_node_black;
                node = grandparent;
            } else {
                if (node->parent->right == node) {
                    node = node->parent;
                    __rotate_left(interner, node);
                }
                node->parent->color = fly_string_node_black;
                grandparent->color = fly_string_node_red;
                __rotate_right(interner, grandparent);
            }
        } else {
            fly_string_node_t* uncle = grandparent->left;
            if (uncle && uncle->color == fly_string_node_red) {
                grandparent->color = fly_string_node_red;
                uncle->color = node->parent->color = fly_string_node_black;
                node = grandparent;
            } else {
                if (node->parent->left == node) {
                    node = node->parent;
                    __rotate_right(interner, node);
                }
                node->parent->color = fly_string_node_black;
                grandparent->color = fly_string_node_red;
                __rotate_left(interner, grandparent);
            }
        }
    }
    interner->root->color = fly_string_node_black;
}

lu_string_t* fly_string_insert(struct string_interner* interner, char* str,
                               size_t str_len) {
    fly_string_node_t* parent = nullptr;
    fly_string_node_t* temp = interner->root;
    while (temp) {
        parent = temp;
        int64_t cmp =
            lu_strcmp(str, temp->str->data.str, str_len, temp->str->data.len);
        if (cmp == 0) {
            return temp->str;
        }
        temp = cmp < 0 ? temp->left : temp->right;
    }

    string_view_t view = {
        .str = arena_alloc(&interner->string_arena, str_len),
        .len = str_len,
    };
    memcpy(view.str, str, str_len);

    fly_string_node_t* new_node =
        arena_alloc(&interner->node_arena, sizeof(fly_string_node_t));
    new_node->color = fly_string_node_red;
    new_node->left = new_node->right = new_node->parent = nullptr;

    lu_string_t* new_string =
        (lu_string_t*)heap_allocate_object(interner->heap, sizeof(lu_string_t));
    new_string->data = view;
    new_string->length = view.len;
    new_string->type = Str_type;
    new_node->str = new_string;

    if (!parent) {
        new_node->color = fly_string_node_black;
        interner->nstrings = 1;
        interner->root = new_node;
        return new_string;
    }

    int64_t cmp =
        lu_strcmp(str, parent->str->data.str, str_len, parent->str->data.len);
    if (cmp > 0) {
        parent->right = new_node;
    } else {
        parent->left = new_node;
    }

    new_node->parent = parent;
    interner->nstrings++;
    // leaving the tree as unbalanced having some problems with rotations.
    // will fix later :).
    //
    // if (new_node->parent->parent) {
    //     __fix_insertion(interner, new_node);
    // }
    return new_node->str;
}

lu_string_t* fly_string_lookup(fly_string_node_t* root, char* str,
                               size_t str_len) {
    fly_string_node_t* temp = root;
    while (temp) {
        int64_t cmp =
            lu_strcmp(str, temp->str->data.str, str_len, temp->str->data.len);
        if (cmp == 0) {
            return temp->str;
        }
        temp = cmp < 0 ? temp->left : temp->right;
    }

    return nullptr;
}
