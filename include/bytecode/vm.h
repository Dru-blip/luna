#pragma once
#include "ir.h"
#include "value.h"

struct lu_globals {
    struct lu_value* fast_slots;
    struct lu_object* named_slots;
};

struct activation_record {
    struct executable* executable;
    struct lu_value* registers;
    size_t max_register_count;
    size_t ip;
    // struct lu_value* globals;
    struct lu_globals* globals;

    size_t caller_ret_reg;
};

struct lu_vm {
    struct activation_record* records;
    struct lu_object* global_object;
    size_t rp;
    struct lu_istate* istate;
};

#define lu_vm_active_record(vm) ((vm)->records[(vm)->rp - 1])

#define lu_vm_current_ip_span(vm) \
    (lu_vm_active_record(vm)      \
         .executable->instructions_span[(lu_vm_active_record(vm).ip) - 1])

struct lu_vm* lu_vm_new(struct lu_istate* istate);
void lu_vm_destroy(struct lu_vm* vm);

struct lu_value lu_vm_run_record(struct lu_vm* vm,
                                 struct activation_record* record);
