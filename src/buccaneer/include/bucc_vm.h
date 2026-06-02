/*
 * bucc_vm.h - Buccaneer virtual machine
 *
 * Part of the Buccaneer language implementation for Mutineer BBS.
 * Stack-based VM executing .bc bytecode modules.
 */

#ifndef BUCC_VM_H
#define BUCC_VM_H

#include "bucc_module.h"
#include "bucc_value.h"
#include "bucc_emit.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BUCC_STACK_MAX      1024
#define BUCC_FRAMES_MAX     256
#define BUCC_GLOBALS_MAX    256
#define BUCC_LOCALS_MAX     256

typedef enum bucc_vm_status {
    VM_OK = 0,
    VM_HALT,
    VM_YIELD,
    VM_CHAIN,
    VM_ERROR,
    VM_STACK_OVERFLOW,
    VM_STACK_UNDERFLOW,
    VM_FRAME_OVERFLOW,
    VM_INVALID_OPCODE,
    VM_DIVISION_BY_ZERO,
    VM_TYPE_ERROR,
    VM_LIMIT_EXCEEDED
} bucc_vm_status_t;

typedef struct bucc_frame {
    const bucc_module_proc_t* proc;
    uint32_t    ip;
    uint32_t    bp;
    uint32_t    local_base;
} bucc_frame_t;

typedef struct bucc_exception {
    bucc_value_t    value;
    uint32_t        frame_depth;
    uint32_t        handler_ip;
    bool            active;
} bucc_exception_t;

typedef struct bucc_limits {
    uint32_t max_instructions;
    uint32_t max_stack_depth;
    uint32_t max_heap_bytes;
    uint32_t max_string_bytes;
    uint32_t max_host_calls;
    uint32_t timeslice_ms;
} bucc_limits_t;

typedef struct bucc_counters {
    uint64_t instructions;
    uint32_t stack_depth;
    uint32_t heap_bytes;
    uint32_t string_bytes;
    uint32_t host_calls;
    uint32_t elapsed_ms;
    uint32_t profile_ticks;
} bucc_counters_t;

typedef struct bucc_try_frame {
    uint32_t handler_ip;
    uint32_t frame_depth;
    uint32_t stack_depth;
    struct bucc_try_frame* next;
} bucc_try_frame_t;

typedef struct bucc_vm bucc_vm_t;

typedef bucc_value_t (*bucc_host_fn_t)(bucc_vm_t* vm, const char* ns,
                                       const char* fn, bucc_value_t* args,
                                       int argc, void* ctx);

struct bucc_vm {
    const bucc_module_t* module;
    
    bucc_value_t    stack[BUCC_STACK_MAX];
    uint32_t        sp;
    
    bucc_frame_t    frames[BUCC_FRAMES_MAX];
    uint32_t        fp;
    
    bucc_value_t    globals[BUCC_GLOBALS_MAX];
    uint32_t        global_count;
    
    bucc_value_t    locals[BUCC_LOCALS_MAX];
    uint32_t        local_count;
    
    uint32_t        ip;
    
    bucc_exception_t exception;
    bucc_try_frame_t* try_stack;
    
    bucc_limits_t   limits;
    bucc_counters_t counters;
    
    bucc_host_fn_t  host_handler;
    void*           host_ctx;
    
    bucc_vm_status_t status;
    char*           error_message;
    
    char*           chain_target;
    bucc_value_t    chain_args;
    
    bool            running;
    bool            debug_enabled;
};

bucc_vm_t* bucc_vm_new(const bucc_module_t* module);
void bucc_vm_free(bucc_vm_t* vm);

void bucc_vm_reset(bucc_vm_t* vm);

void bucc_vm_set_host_handler(bucc_vm_t* vm, bucc_host_fn_t handler, void* ctx);
void bucc_vm_set_limits(bucc_vm_t* vm, const bucc_limits_t* limits);

bucc_vm_status_t bucc_vm_run(bucc_vm_t* vm);
bucc_vm_status_t bucc_vm_step(bucc_vm_t* vm);
bucc_vm_status_t bucc_vm_call(bucc_vm_t* vm, const char* proc_name,
                              bucc_value_t* args, int argc);

void bucc_vm_push(bucc_vm_t* vm, bucc_value_t value);
bucc_value_t bucc_vm_pop(bucc_vm_t* vm);
bucc_value_t bucc_vm_peek(bucc_vm_t* vm, int offset);

bucc_value_t bucc_vm_get_global(bucc_vm_t* vm, uint32_t index);
void bucc_vm_set_global(bucc_vm_t* vm, uint32_t index, bucc_value_t value);

bucc_value_t bucc_vm_get_local(bucc_vm_t* vm, uint32_t index);
void bucc_vm_set_local(bucc_vm_t* vm, uint32_t index, bucc_value_t value);

void bucc_vm_throw(bucc_vm_t* vm, bucc_value_t error);
bool bucc_vm_has_exception(bucc_vm_t* vm);
bucc_value_t bucc_vm_get_exception(bucc_vm_t* vm);

const char* bucc_vm_get_chain_target(bucc_vm_t* vm);
bucc_value_t bucc_vm_get_chain_args(bucc_vm_t* vm);

const char* bucc_vm_status_name(bucc_vm_status_t status);
const char* bucc_vm_get_error(bucc_vm_t* vm);

void bucc_vm_dump_stack(bucc_vm_t* vm, FILE* out);
void bucc_vm_dump_state(bucc_vm_t* vm, FILE* out);

#ifdef __cplusplus
}
#endif

#endif /* BUCC_VM_H */
