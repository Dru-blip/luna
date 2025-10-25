#pragma  once
#include <stddef.h>
#include <stdint.h>

#include "ast.h"
#include "value.h"

enum opcode {
    // Load accumulator with small integer
    OPCODE_LDASI,
    // Load accumulator with constant value
    OPCODE_LOAD_CONST,
    // Return value from accumulator
    OPCODE_RET,
};

// this is high level representation of instruction
// later this will be optimized to byte stream for efficient storage and
// execution.
struct instruction {
    enum opcode opcode;
    union {
        uint16_t const_index;
    };
};

struct basic_block {
    size_t id;
    char* label;
    struct instruction* instructions;
};

struct exectuable {
    LUNA_OBJECT_HEADER;
    struct instruction* instructions;
    size_t instructions_size;
    size_t constants_size;
    struct lu_value* constants;
};

struct generator {
    size_t current_block_id;
    struct basic_block* blocks;
    struct ast_node* node;
    struct ast_program program;
    size_t block_counter;
    struct lu_value* constants;
    size_t constant_counter;
};

void generator_init(struct generator* generator, struct ast_program program);
size_t generator_basic_block_new(struct generator* generator);
struct exectuable* generator_generate(struct ast_program program);
struct exectuable* generator_make_executable(struct generator* generator);
