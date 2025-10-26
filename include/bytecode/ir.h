#pragma once
#include <stddef.h>
#include <stdint.h>

#include "ast.h"
#include "value.h"

enum opcode {
    // still not implemented
    OPCODE_LOAD_SMI,
    // Load constant value
    OPCODE_LOAD_CONST,
    OPCODE_LOAD_NONE,
    OPCODE_LOAD_TRUE,
    OPCODE_LOAD_FALSE,

    OPCODE_LOAD_GLOBAL_BY_INDEX,
    OPCODE_LOAD_GLOBAL_BY_NAME,
    OPCODE_STORE_GLOBAL_BY_INDEX,
    OPCODE_STORE_GLOBAL_BY_NAME,
    OPCODE_STORE_LOCAL,
    OPCODE_LOAD_LOCAL,

    OPCODE_MOV,

    OPCODE_ADD,
    OPCODE_SUB,
    OPCODE_MUL,

    OPCODE_DIV,
    OPCODE_MOD,

    OPCODE_TEST_GREATER_THAN,
    OPCODE_TEST_GREATER_THAN_EQUAL,

    OPCODE_TEST_LESS_THAN,
    OPCODE_TEST_LESS_THAN_EQUAL,

    OPCODE_TEST_EQUAL,
    OPCODE_TEST_NOT_EQUAL,

    OPCODE_SHIFT_LEFT,
    OPCODE_SHIFT_RIGHT,

    OPCODE_LOGIC_AND,
    OPCODE_LOGIC_OR,

    OPCODE_UNARY_MINUS,
    OPCODE_UNARY_PLUS,
    OPCODE_UNARY_NOT,

    OPCODE_JUMP,
    OPCODE_JMP_IF,
    OPCODE_RET,
};

// this is high level representation of instruction
// later this will be represented in byte stream for better storage.
struct instruction {
    enum opcode opcode;
    union {
        // for load constants
        struct {
            uint16_t const_index;
            uint32_t register_index;
        };
        // for binary operations
        struct {
            uint32_t r1;
            uint32_t r2;
            uint32_t dst;
        };
        // for jump if
        struct {
            uint32_t cond;
            uint32_t true_block_id;
            uint32_t false_block_id;
        };
        // for unconditional jump
        struct {
            uint32_t target_block_id;
        };
        struct {
            // for mov instruction
            uint32_t m_dst;
            uint32_t m_src;
        };
    };
};

struct basic_block {
    size_t id;
    char* label;
    struct instruction* instructions;
    struct span* instructions_spans;
    size_t start_offset;  // used when linearizing blocks
};

struct exectuable {
    struct instruction* instructions;
    size_t instructions_size;
    size_t constants_size;
    struct lu_value* constants;
    uint32_t max_register_count;
    struct span* instructions_span;
    const char* file_path;
    size_t global_variable_count;
};

enum scope {
    SCOPE_GLOBAL,
    SCOPE_LOCAL,
    SCOPE_PARAM,
};

struct variable {
    enum scope scope;
    char* name;
    size_t name_length;
    size_t scope_depth;
    uint32_t allocated_reg;
};

struct generator {
    struct lu_istate* state;
    size_t current_block_id;
    struct basic_block* blocks;
    struct ast_node* node;
    struct ast_program program;
    size_t block_counter;
    struct lu_value* constants;
    size_t constant_counter;
    uint32_t register_counter;
    struct variable* local_variables;
    struct variable* global_variables;
    size_t local_variable_count;
    size_t global_variable_count;
    uint32_t scope_depth;
};

void generator_init(struct generator* generator, struct ast_program program);
size_t generator_basic_block_new(struct generator* generator);
struct exectuable* generator_generate(struct lu_istate* state,
                                      struct ast_program program);
struct exectuable* generator_make_executable(struct generator* generator);

void print_executable(struct exectuable* executable);
