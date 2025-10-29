#pragma once
#include <stddef.h>
#include <stdint.h>

#include "ast.h"
#include "value.h"

enum opcode {
    // still not implemented
    OPCODE_LOAD_SMI,
    // Load constant value
    OPCODE_LOAD_CONST,  // uses instruction.load_const
    OPCODE_LOAD_NONE,   // uses instruction.destination_reg
    OPCODE_LOAD_TRUE,   // uses instruction.destination_reg
    OPCODE_LOAD_FALSE,  // uses instruction.destination_reg

    OPCODE_LOAD_GLOBAL_BY_INDEX,   // uses instruction.mov
    OPCODE_LOAD_GLOBAL_BY_NAME,    // uses instruction.pair
    OPCODE_STORE_GLOBAL_BY_INDEX,  // uses instruction.pair
    OPCODE_STORE_GLOBAL_BY_NAME,   // uses instruction.pair
    OPCODE_STORE_LOCAL,            // [unused] replaced with  OPCODE_MOV
    OPCODE_LOAD_LOCAL,             // [unused] replaced with OPCODE_MOV

    OPCODE_MOV,  // uses instruction.mov ({.dest_reg,.src_reg})

    OPCODE_ADD,  // uses instruction.binary_op
                 // ({.result_reg,.left_reg,.right_reg})
    OPCODE_SUB,  // instruction.binary_op
    OPCODE_MUL,  // instruction.binary_op

    OPCODE_DIV,  // instruction.binary_op
    OPCODE_MOD,  // instruction.binary_op

    OPCODE_TEST_GREATER_THAN,        // instruction.binary_op
    OPCODE_TEST_GREATER_THAN_EQUAL,  // instruction.binary_op

    OPCODE_TEST_LESS_THAN,        // instruction.binary_op
    OPCODE_TEST_LESS_THAN_EQUAL,  // instruction.binary_op

    OPCODE_TEST_EQUAL,      // instruction.binary_op
    OPCODE_TEST_NOT_EQUAL,  // instruction.binary_op

    OPCODE_SHIFT_LEFT,   // still not implemented
    OPCODE_SHIFT_RIGHT,  // still not implemented

    OPCODE_UNARY_MINUS,  // uses instruction.destination_reg
    OPCODE_UNARY_PLUS,   // uses instruction.destination_reg
    OPCODE_UNARY_NOT,    // uses instruction.destination_reg

    OPCODE_JUMP,    // uses instruction.jmp
    OPCODE_JMP_IF,  // uses instruction.jmp_if
    OPCODE_RET,     // uses instruction.destination_reg
    OPCODE_RET_NONE,

    OPCODE_MAKE_FUNCTION,  // uses instruction.binary_op
    OPCODE_CALL,           // uses instruction.call

    OPCODE_NEW_ARRAY,     // uses instruction.destination_reg
    OPCODE_ARRAY_APPEND,  // uses instruction.pair
    OPCODE_LOAD_SUBSCR,   // uses instruction.binary_op
    OPCODE_STORE_SUBSCR,  // uses instruction.binary_op

    OPCODE_NEW_OBJECT,           // uses instruction.destination_reg
    OPCODE_OBJECT_SET_PROPERTY,  // uses instruction.binary_op
    OPCODE_OBJECT_GET_PROPERTY,

};

// this is high level representation of instruction
// later this will be represented in byte stream for better storage.
struct instruction {
    enum opcode opcode;
    union {
        struct {
            uint16_t constant_index;
            uint32_t destination_reg;
        } load_const;

        struct {
            uint32_t left_reg;
            uint32_t right_reg;
            uint32_t result_reg;
        } binary_op;

        struct {
            uint32_t condition_reg;
            uint32_t true_block_id;
            uint32_t false_block_id;
        } jmp_if;

        struct {
            uint32_t target_offset;
        } jmp;

        struct {
            uint32_t dest_reg;
            uint32_t src_reg;
        } mov;

        struct {
            uint32_t fst;
            uint32_t snd;
        } pair;

        struct {
            uint32_t callee_reg;
            uint32_t argc;
            uint32_t ret_reg;
            uint32_t self_reg;
            uint32_t* args_reg;
        } call;

        uint32_t destination_reg;
    };
};

struct basic_block {
    size_t id;
    char* label;
    struct instruction* instructions;
    struct span* instructions_spans;
    size_t start_offset;  // used when linearizing blocks
    bool visited;
};

struct executable {
    LUNA_OBJECT_HEADER;
    struct lu_string* name;
    struct instruction* instructions;
    size_t instructions_size;
    size_t constants_size;
    struct lu_value* constants;
    struct lu_string** identifier_table;
    size_t identifier_table_size;
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

struct loop {
    uint32_t start_block_id;
    uint32_t end_block_id;
};

struct generator {
    struct generator* prev;
    struct lu_istate* state;
    size_t current_block_id;
    struct basic_block* blocks;
    struct ast_node* node;
    struct ast_program program;
    size_t block_counter;
    struct lu_value* constants;
    size_t constant_counter;
    struct lu_string** identifier_table;
    size_t identifier_table_size;
    uint32_t register_counter;
    struct variable* local_variables;
    struct variable* global_variables;
    size_t local_variable_count;
    size_t global_variable_count;
    uint32_t scope_depth;
    struct loop* loop_stack;
};

void generator_init(struct generator* generator, struct ast_program program);
size_t generator_basic_block_new(struct generator* generator);
struct executable* generator_generate(struct lu_istate* state,
                                      struct ast_program program);
struct executable* generator_make_executable(struct generator* generator);

void print_executable(struct executable* executable);
