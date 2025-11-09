#include "objects/string.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "luna.h"
#include "value.h"

LU_NATIVE_FN(String_to_string) {
    //
    LU_RETURN_OBJ(self);
}

LU_NATIVE_FN(String_char_at) {
    int64_t index;
    LU_TRY_UNPACK_INT(vm, args, 0, &index);

    struct lu_string* str = self;

    if (index < 0 || index >= str->length) {
        lu_raise_error(vm->istate, "Index out of bounds");
        LU_RETURN_NONE();
    }

    char c[2];
    if (str->type == STRING_SMALL || str->type == STRING_SMALL_INTERNED) {
        c[0] = str->Sms[index];
    } else if (str->type == STRING_SIMPLE) {
        c[0] = str->block->data[index];
    }

    c[1] = '\0';

    LU_RETURN_OBJ(lu_string_new(vm->istate, c));
}

LU_NATIVE_FN(String_substring) {
    int64_t start, end;
    LU_TRY_UNPACK_ARGS(vm, "ii", argc, args, &start, &end);
    struct string_block* block = calloc(1, sizeof(struct string_block) + end - start + 1);
    block->next = block->prev = nullptr;
    block->prev = vm->istate->string_pool.last_block;
    vm->istate->string_pool.last_block = block;
    block->prev->next = block;

    struct lu_string* str = self;

    if (str->type == STRING_SMALL_INTERNED || str->type == STRING_SMALL) {
        memcpy(block->data, str->Sms + start, end - start);
    } else if (str->type == STRING_SIMPLE) {
        memcpy(block->data, str->block->data + start, end - start);
    }

    LU_RETURN_OBJ(lu_string_from_block(vm->istate, block));
}

LU_NATIVE_FN(String_index_of) {
    struct lu_string* needle_str;
    struct lu_string* haystack_str = self;
    LU_TRY_UNPACK_STR(vm, args, 0, &needle_str);

    size_t needle_len = needle_str->length;
    size_t haystack_len = haystack_str->length;

    if (needle_len == 0) {
        LU_RETURN_INT(0);
    }

    if (haystack_len < needle_len) {
        LU_RETURN_INT(-1);
    }

    char* needle = lu_string_get_cstring(needle_str);
    char* haystack = lu_string_get_cstring(haystack_str);

    for (size_t i = 0; i <= haystack_len - needle_len; i++) {
        if (memcmp(haystack + i, needle, needle_len) == 0) {
            LU_RETURN_INT(i);
        }
    }

    LU_RETURN_INT(-1);
}

struct lu_object* lu_string_prototype_new(struct lu_istate* state) {
    struct lu_object* obj = lu_object_new(state);

    lu_register_native_fn(state, obj, "toString", String_to_string, 0);
    lu_register_native_fn(state, obj, "charAt", String_char_at, 1);
    lu_register_native_fn(state, obj, "substring", String_substring, 2);
    lu_register_native_fn(state, obj, "indexOf", String_index_of, 1);

    return obj;
}
