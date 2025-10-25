#pragma once
#include "ir.h"
#include "value.h"

struct activation_record {
    struct exectuable* executable;
    struct lu_value* registers;
    size_t max_register_count;
    size_t ip;
};

struct lu_vm {
    struct activation_record* records;
    struct lu_object* global_object;
    size_t rp;
    struct lu_istate* istate;
};

struct lu_vm* lu_vm_new(struct lu_istate* istate);
void lu_vm_destroy(struct lu_vm* vm);

struct lu_value lu_vm_run_record(struct lu_vm* vm,
                                 struct activation_record* record);
