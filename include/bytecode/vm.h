#pragma once
#include "ir.h"
#include "value.h"

struct lu_globals {
    struct lu_value* fast_slots;
    struct lu_object* named_slots;
};

// unused
enum activation_record_type {
    ACTIVATION_RECORD_TYPE_USER,
    ACTIVATION_RECORD_TYPE_NATIVE,
};
//

struct activation_record {
    enum activation_record_type type;  // unused
    struct executable* executable;
    struct lu_function* function;
    struct lu_value* registers;
    size_t max_register_count;
    size_t ip;
    // struct lu_value* globals;
    struct lu_globals* globals;
    size_t caller_ret_reg;
};

enum lu_vm_status {
    VM_STATUS_RUNNING,
    VM_STATUS_HALT,
};

struct lu_vm {
    enum lu_vm_status status;
    struct activation_record* records;
    struct lu_object* global_object;
    size_t rp;
    struct lu_istate* istate;
    struct lu_globals* globals;
};

#define lu_vm_active_record(vm) ((vm)->records[(vm)->rp - 1])

#define lu_vm_current_ip_span(vm) \
    (lu_vm_active_record(vm)      \
         .executable->instructions_span[(lu_vm_active_record(vm).ip) - 1])

struct lu_vm* lu_vm_new(struct lu_istate* istate);
void lu_vm_destroy(struct lu_vm* vm);

struct lu_value lu_vm_run_record(struct lu_vm* vm,
                                 struct activation_record* record,
                                 bool as_callback);
