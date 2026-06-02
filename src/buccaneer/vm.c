/*
 * vm.c - Buccaneer virtual machine implementation
 *
 * Part of the Buccaneer language implementation for Mutineer BBS.
 */

#define _POSIX_C_SOURCE 200809L
#include "include/bucc_vm.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <ctype.h>


bucc_vm_t* bucc_vm_new(const bucc_module_t* module) {
    bucc_vm_t* vm = calloc(1, sizeof(bucc_vm_t));
    if (!vm) return NULL;
    
    vm->module = module;
    vm->status = VM_OK;
    
    vm->limits.max_instructions = 1000000;
    vm->limits.max_stack_depth = BUCC_STACK_MAX;
    vm->limits.max_heap_bytes = 1024 * 1024;
    vm->limits.max_string_bytes = 256 * 1024;
    vm->limits.max_host_calls = 10000;
    vm->limits.timeslice_ms = 5000;
    
    return vm;
}

void bucc_vm_free(bucc_vm_t* vm) {
    if (!vm) return;
    
    for (uint32_t i = 0; i < vm->sp; i++) {
        bucc_value_release(&vm->stack[i]);
    }
    
    for (uint32_t i = 0; i < vm->global_count; i++) {
        bucc_value_release(&vm->globals[i]);
    }
    
    for (uint32_t i = 0; i < vm->local_count; i++) {
        bucc_value_release(&vm->locals[i]);
    }
    
    bucc_value_release(&vm->exception.value);
    bucc_value_release(&vm->chain_args);
    
    bucc_try_frame_t* tf = vm->try_stack;
    while (tf) {
        bucc_try_frame_t* next = tf->next;
        free(tf);
        tf = next;
    }
    
    free(vm->error_message);
    free(vm->chain_target);
    free(vm);
}

void bucc_vm_reset(bucc_vm_t* vm) {
    if (!vm) return;
    
    for (uint32_t i = 0; i < vm->sp; i++) {
        bucc_value_release(&vm->stack[i]);
    }
    vm->sp = 0;
    
    vm->fp = 0;
    vm->ip = 0;
    
    for (uint32_t i = 0; i < vm->local_count; i++) {
        bucc_value_release(&vm->locals[i]);
    }
    vm->local_count = 0;
    
    bucc_value_release(&vm->exception.value);
    vm->exception.active = false;
    
    bucc_try_frame_t* tf = vm->try_stack;
    while (tf) {
        bucc_try_frame_t* next = tf->next;
        free(tf);
        tf = next;
    }
    vm->try_stack = NULL;
    
    memset(&vm->counters, 0, sizeof(vm->counters));
    
    vm->status = VM_OK;
    free(vm->error_message);
    vm->error_message = NULL;
    
    free(vm->chain_target);
    vm->chain_target = NULL;
    bucc_value_release(&vm->chain_args);
    vm->chain_args = BUCC_NULL_VAL;
    
    vm->running = false;
}

void bucc_vm_set_host_handler(bucc_vm_t* vm, bucc_host_fn_t handler, void* ctx) {
    if (!vm) return;
    vm->host_handler = handler;
    vm->host_ctx = ctx;
}

void bucc_vm_set_limits(bucc_vm_t* vm, const bucc_limits_t* limits) {
    if (!vm || !limits) return;
    vm->limits = *limits;
}

void bucc_vm_push(bucc_vm_t* vm, bucc_value_t value) {
    if (!vm) return;
    if (vm->sp >= BUCC_STACK_MAX) {
        vm->status = VM_STACK_OVERFLOW;
        return;
    }
    bucc_value_retain(&value);
    vm->stack[vm->sp++] = value;
    if (vm->sp > vm->counters.stack_depth) {
        vm->counters.stack_depth = vm->sp;
    }
}

bucc_value_t bucc_vm_pop(bucc_vm_t* vm) {
    if (!vm || vm->sp == 0) {
        if (vm) vm->status = VM_STACK_UNDERFLOW;
        return BUCC_NULL_VAL;
    }
    return vm->stack[--vm->sp];
}

bucc_value_t bucc_vm_peek(bucc_vm_t* vm, int offset) {
    if (!vm || vm->sp == 0) return BUCC_NULL_VAL;
    uint32_t idx = vm->sp - 1 - offset;
    if (idx >= vm->sp) return BUCC_NULL_VAL;
    return vm->stack[idx];
}

bucc_value_t bucc_vm_get_global(bucc_vm_t* vm, uint32_t index) {
    if (!vm || index >= vm->global_count) return BUCC_NULL_VAL;
    bucc_value_t v = vm->globals[index];
    bucc_value_retain(&v);
    return v;
}

void bucc_vm_set_global(bucc_vm_t* vm, uint32_t index, bucc_value_t value) {
    if (!vm || index >= BUCC_GLOBALS_MAX) return;
    if (index >= vm->global_count) {
        for (uint32_t i = vm->global_count; i <= index; i++) {
            vm->globals[i] = BUCC_NULL_VAL;
        }
        vm->global_count = index + 1;
    }
    bucc_value_release(&vm->globals[index]);
    bucc_value_retain(&value);
    vm->globals[index] = value;
}

bucc_value_t bucc_vm_get_local(bucc_vm_t* vm, uint32_t index) {
    if (!vm) return BUCC_NULL_VAL;
    
    uint32_t base = 0;
    if (vm->fp > 0) {
        base = vm->frames[vm->fp - 1].local_base;
    }
    
    uint32_t actual = base + index;
    if (actual >= vm->local_count) return BUCC_NULL_VAL;
    
    bucc_value_t v = vm->locals[actual];
    bucc_value_retain(&v);
    return v;
}

void bucc_vm_set_local(bucc_vm_t* vm, uint32_t index, bucc_value_t value) {
    if (!vm) return;
    
    uint32_t base = 0;
    if (vm->fp > 0) {
        base = vm->frames[vm->fp - 1].local_base;
    }
    
    uint32_t actual = base + index;
    if (actual >= BUCC_LOCALS_MAX) return;
    
    if (actual >= vm->local_count) {
        for (uint32_t i = vm->local_count; i <= actual; i++) {
            vm->locals[i] = BUCC_NULL_VAL;
        }
        vm->local_count = actual + 1;
    }
    
    bucc_value_release(&vm->locals[actual]);
    bucc_value_retain(&value);
    vm->locals[actual] = value;
}

void bucc_vm_throw(bucc_vm_t* vm, bucc_value_t error) {
    if (!vm) return;
    bucc_value_release(&vm->exception.value);
    bucc_value_retain(&error);
    vm->exception.value = error;
    vm->exception.active = true;
    vm->exception.frame_depth = vm->fp;
}

bool bucc_vm_has_exception(bucc_vm_t* vm) {
    return vm && vm->exception.active;
}

bucc_value_t bucc_vm_get_exception(bucc_vm_t* vm) {
    if (!vm || !vm->exception.active) return BUCC_NULL_VAL;
    bucc_value_t v = vm->exception.value;
    bucc_value_retain(&v);
    return v;
}

const char* bucc_vm_get_chain_target(bucc_vm_t* vm) {
    return vm ? vm->chain_target : NULL;
}

bucc_value_t bucc_vm_get_chain_args(bucc_vm_t* vm) {
    if (!vm) return BUCC_NULL_VAL;
    bucc_value_t v = vm->chain_args;
    bucc_value_retain(&v);
    return v;
}

static void vm_error(bucc_vm_t* vm, bucc_vm_status_t status, const char* msg) {
    vm->status = status;
    free(vm->error_message);
    vm->error_message = msg ? strdup(msg) : NULL;
    vm->running = false;
}

static uint8_t read_u8(bucc_vm_t* vm) {
    if (vm->ip >= vm->module->bytecode_len) {
        vm_error(vm, VM_ERROR, "read past end of bytecode");
        return 0;
    }
    return vm->module->bytecode[vm->ip++];
}

static uint16_t read_u16(bucc_vm_t* vm) {
    if (vm->ip + 2 > vm->module->bytecode_len) {
        vm_error(vm, VM_ERROR, "read past end of bytecode");
        return 0;
    }
    uint16_t val = (vm->module->bytecode[vm->ip] << 8) |
                   vm->module->bytecode[vm->ip + 1];
    vm->ip += 2;
    return val;
}

static int32_t read_i32(bucc_vm_t* vm) {
    if (vm->ip + 4 > vm->module->bytecode_len) {
        vm_error(vm, VM_ERROR, "read past end of bytecode");
        return 0;
    }
    int32_t val = (vm->module->bytecode[vm->ip] << 24) |
                  (vm->module->bytecode[vm->ip + 1] << 16) |
                  (vm->module->bytecode[vm->ip + 2] << 8) |
                  vm->module->bytecode[vm->ip + 3];
    vm->ip += 4;
    return val;
}

static bool handle_exception(bucc_vm_t* vm) {
    if (!vm->exception.active) return false;
    
    while (vm->try_stack) {
        bucc_try_frame_t* tf = vm->try_stack;
        vm->try_stack = tf->next;
        
        while (vm->fp > tf->frame_depth) {
            vm->fp--;
        }
        
        while (vm->sp > tf->stack_depth) {
            bucc_value_t v = bucc_vm_pop(vm);
            bucc_value_release(&v);
        }
        
        bucc_vm_push(vm, vm->exception.value);
        vm->exception.active = false;
        vm->exception.value = BUCC_NULL_VAL;
        
        vm->ip = tf->handler_ip;
        free(tf);
        return true;
    }
    
    return false;
}

bucc_vm_status_t bucc_vm_step(bucc_vm_t* vm) {
    if (!vm || !vm->module || !vm->module->bytecode) {
        return VM_ERROR;
    }
    
    if (vm->status != VM_OK) {
        return vm->status;
    }
    
    if (vm->counters.instructions >= vm->limits.max_instructions) {
        vm_error(vm, VM_LIMIT_EXCEEDED, "instruction limit exceeded");
        return vm->status;
    }
    
    if (vm->ip >= vm->module->bytecode_len) {
        vm->status = VM_HALT;
        return vm->status;
    }
    
    vm->counters.instructions++;
    
    uint8_t op = read_u8(vm);
    
    switch (op) {
        case OP_NOP:
            break;
            
        case OP_HALT:
            vm->status = VM_HALT;
            vm->running = false;
            break;
            
        case OP_PUSH_NULL:
            bucc_vm_push(vm, BUCC_NULL_VAL);
            break;
            
        case OP_PUSH_TRUE:
            bucc_vm_push(vm, BUCC_BOOL_VAL(true));
            break;
            
        case OP_PUSH_FALSE:
            bucc_vm_push(vm, BUCC_BOOL_VAL(false));
            break;
            
        case OP_PUSH_I64: {
            uint16_t idx = read_u16(vm);
            bucc_value_t val = bucc_module_get_constant(vm->module, idx);
            bucc_vm_push(vm, val);
            break;
        }
            
        case OP_PUSH_F64: {
            uint16_t idx = read_u16(vm);
            bucc_value_t val = bucc_module_get_constant(vm->module, idx);
            bucc_vm_push(vm, val);
            break;
        }
            
        case OP_PUSH_STR: {
            uint16_t idx = read_u16(vm);
            const char* str = bucc_module_get_string(vm->module, idx);
            bucc_string_t* s = bucc_string_from_cstr(str);
            bucc_vm_push(vm, BUCC_STRING_VAL(s));
            bucc_string_release(s);
            break;
        }
            
        case OP_PUSH_DATE:
        case OP_PUSH_DATETIME: {
            uint16_t idx = read_u16(vm);
            bucc_value_t val = bucc_module_get_constant(vm->module, idx);
            bucc_vm_push(vm, val);
            break;
        }
            
        case OP_POP: {
            bucc_value_t v = bucc_vm_pop(vm);
            bucc_value_release(&v);
            break;
        }
            
        case OP_DUP: {
            bucc_value_t v = bucc_vm_peek(vm, 0);
            bucc_vm_push(vm, v);
            break;
        }
            
        case OP_SWAP: {
            if (vm->sp < 2) {
                vm_error(vm, VM_STACK_UNDERFLOW, "stack underflow on SWAP");
                break;
            }
            bucc_value_t a = vm->stack[vm->sp - 1];
            bucc_value_t b = vm->stack[vm->sp - 2];
            vm->stack[vm->sp - 1] = b;
            vm->stack[vm->sp - 2] = a;
            break;
        }
            
        case OP_LOAD_GLOBAL: {
            uint16_t idx = read_u16(vm);
            bucc_value_t v = bucc_vm_get_global(vm, idx);
            bucc_vm_push(vm, v);
            bucc_value_release(&v);
            break;
        }
            
        case OP_STORE_GLOBAL: {
            uint16_t idx = read_u16(vm);
            bucc_value_t v = bucc_vm_pop(vm);
            bucc_vm_set_global(vm, idx, v);
            bucc_value_release(&v);
            break;
        }
            
        case OP_LOAD_LOCAL:
        case OP_LOAD_ARG: {
            uint16_t idx = read_u16(vm);
            bucc_value_t v = bucc_vm_get_local(vm, idx);
            bucc_vm_push(vm, v);
            bucc_value_release(&v);
            break;
        }
            
        case OP_STORE_LOCAL: {
            uint16_t idx = read_u16(vm);
            bucc_value_t v = bucc_vm_pop(vm);
            bucc_vm_set_local(vm, idx, v);
            bucc_value_release(&v);
            break;
        }
            
        case OP_ADD: {
            bucc_value_t b = bucc_vm_pop(vm);
            bucc_value_t a = bucc_vm_pop(vm);
            
            if (BUCC_IS_STRING(a) || BUCC_IS_STRING(b)) {
                bucc_string_t* sa = bucc_value_to_string(&a);
                bucc_string_t* sb = bucc_value_to_string(&b);
                bucc_string_t* result = bucc_string_concat(sa, sb);
                bucc_vm_push(vm, BUCC_STRING_VAL(result));
                bucc_string_release(sa);
                bucc_string_release(sb);
                bucc_string_release(result);
            } else if (BUCC_IS_F64(a) || BUCC_IS_F64(b)) {
                double da = BUCC_IS_F64(a) ? BUCC_AS_F64(a) : (double)BUCC_AS_I64(a);
                double db = BUCC_IS_F64(b) ? BUCC_AS_F64(b) : (double)BUCC_AS_I64(b);
                bucc_vm_push(vm, BUCC_F64_VAL(da + db));
            } else {
                bucc_vm_push(vm, BUCC_I64_VAL(BUCC_AS_I64(a) + BUCC_AS_I64(b)));
            }
            
            bucc_value_release(&a);
            bucc_value_release(&b);
            break;
        }
            
        case OP_SUB: {
            bucc_value_t b = bucc_vm_pop(vm);
            bucc_value_t a = bucc_vm_pop(vm);
            
            if (BUCC_IS_F64(a) || BUCC_IS_F64(b)) {
                double da = BUCC_IS_F64(a) ? BUCC_AS_F64(a) : (double)BUCC_AS_I64(a);
                double db = BUCC_IS_F64(b) ? BUCC_AS_F64(b) : (double)BUCC_AS_I64(b);
                bucc_vm_push(vm, BUCC_F64_VAL(da - db));
            } else {
                bucc_vm_push(vm, BUCC_I64_VAL(BUCC_AS_I64(a) - BUCC_AS_I64(b)));
            }
            
            bucc_value_release(&a);
            bucc_value_release(&b);
            break;
        }
            
        case OP_MUL: {
            bucc_value_t b = bucc_vm_pop(vm);
            bucc_value_t a = bucc_vm_pop(vm);
            
            if (BUCC_IS_F64(a) || BUCC_IS_F64(b)) {
                double da = BUCC_IS_F64(a) ? BUCC_AS_F64(a) : (double)BUCC_AS_I64(a);
                double db = BUCC_IS_F64(b) ? BUCC_AS_F64(b) : (double)BUCC_AS_I64(b);
                bucc_vm_push(vm, BUCC_F64_VAL(da * db));
            } else {
                bucc_vm_push(vm, BUCC_I64_VAL(BUCC_AS_I64(a) * BUCC_AS_I64(b)));
            }
            
            bucc_value_release(&a);
            bucc_value_release(&b);
            break;
        }
            
        case OP_DIV: {
            bucc_value_t b = bucc_vm_pop(vm);
            bucc_value_t a = bucc_vm_pop(vm);
            
            if (BUCC_IS_F64(a) || BUCC_IS_F64(b)) {
                double da = BUCC_IS_F64(a) ? BUCC_AS_F64(a) : (double)BUCC_AS_I64(a);
                double db = BUCC_IS_F64(b) ? BUCC_AS_F64(b) : (double)BUCC_AS_I64(b);
                if (db == 0.0) {
                    bucc_vm_throw(vm, BUCC_ERROR_VAL(bucc_error_new("Division by zero", "DIV_ZERO", 0)));
                    handle_exception(vm);
                } else {
                    bucc_vm_push(vm, BUCC_F64_VAL(da / db));
                }
            } else {
                if (BUCC_AS_I64(b) == 0) {
                    bucc_vm_throw(vm, BUCC_ERROR_VAL(bucc_error_new("Division by zero", "DIV_ZERO", 0)));
                    handle_exception(vm);
                } else {
                    bucc_vm_push(vm, BUCC_I64_VAL(BUCC_AS_I64(a) / BUCC_AS_I64(b)));
                }
            }
            
            bucc_value_release(&a);
            bucc_value_release(&b);
            break;
        }
            
        case OP_MOD: {
            bucc_value_t b = bucc_vm_pop(vm);
            bucc_value_t a = bucc_vm_pop(vm);
            
            if (BUCC_AS_I64(b) == 0) {
                bucc_vm_throw(vm, BUCC_ERROR_VAL(bucc_error_new("Division by zero", "DIV_ZERO", 0)));
                handle_exception(vm);
            } else {
                bucc_vm_push(vm, BUCC_I64_VAL(BUCC_AS_I64(a) % BUCC_AS_I64(b)));
            }
            
            bucc_value_release(&a);
            bucc_value_release(&b);
            break;
        }
            
        case OP_NEG: {
            bucc_value_t a = bucc_vm_pop(vm);
            if (BUCC_IS_F64(a)) {
                bucc_vm_push(vm, BUCC_F64_VAL(-BUCC_AS_F64(a)));
            } else {
                bucc_vm_push(vm, BUCC_I64_VAL(-BUCC_AS_I64(a)));
            }
            bucc_value_release(&a);
            break;
        }
            
        case OP_EQ: {
            bucc_value_t b = bucc_vm_pop(vm);
            bucc_value_t a = bucc_vm_pop(vm);
            bucc_vm_push(vm, BUCC_BOOL_VAL(bucc_value_equal(&a, &b)));
            bucc_value_release(&a);
            bucc_value_release(&b);
            break;
        }
            
        case OP_NE: {
            bucc_value_t b = bucc_vm_pop(vm);
            bucc_value_t a = bucc_vm_pop(vm);
            bucc_vm_push(vm, BUCC_BOOL_VAL(!bucc_value_equal(&a, &b)));
            bucc_value_release(&a);
            bucc_value_release(&b);
            break;
        }
            
        case OP_LT: {
            bucc_value_t b = bucc_vm_pop(vm);
            bucc_value_t a = bucc_vm_pop(vm);
            bucc_vm_push(vm, BUCC_BOOL_VAL(bucc_value_compare(&a, &b) < 0));
            bucc_value_release(&a);
            bucc_value_release(&b);
            break;
        }
            
        case OP_LE: {
            bucc_value_t b = bucc_vm_pop(vm);
            bucc_value_t a = bucc_vm_pop(vm);
            bucc_vm_push(vm, BUCC_BOOL_VAL(bucc_value_compare(&a, &b) <= 0));
            bucc_value_release(&a);
            bucc_value_release(&b);
            break;
        }
            
        case OP_GT: {
            bucc_value_t b = bucc_vm_pop(vm);
            bucc_value_t a = bucc_vm_pop(vm);
            bucc_vm_push(vm, BUCC_BOOL_VAL(bucc_value_compare(&a, &b) > 0));
            bucc_value_release(&a);
            bucc_value_release(&b);
            break;
        }
            
        case OP_GE: {
            bucc_value_t b = bucc_vm_pop(vm);
            bucc_value_t a = bucc_vm_pop(vm);
            bucc_vm_push(vm, BUCC_BOOL_VAL(bucc_value_compare(&a, &b) >= 0));
            bucc_value_release(&a);
            bucc_value_release(&b);
            break;
        }
            
        case OP_AND: {
            bucc_value_t b = bucc_vm_pop(vm);
            bucc_value_t a = bucc_vm_pop(vm);
            bucc_vm_push(vm, BUCC_BOOL_VAL(bucc_value_truthy(&a) && bucc_value_truthy(&b)));
            bucc_value_release(&a);
            bucc_value_release(&b);
            break;
        }
            
        case OP_OR: {
            bucc_value_t b = bucc_vm_pop(vm);
            bucc_value_t a = bucc_vm_pop(vm);
            bucc_vm_push(vm, BUCC_BOOL_VAL(bucc_value_truthy(&a) || bucc_value_truthy(&b)));
            bucc_value_release(&a);
            bucc_value_release(&b);
            break;
        }
            
        case OP_NOT: {
            bucc_value_t a = bucc_vm_pop(vm);
            bucc_vm_push(vm, BUCC_BOOL_VAL(!bucc_value_truthy(&a)));
            bucc_value_release(&a);
            break;
        }
            
        case OP_CONCAT: {
            bucc_value_t b = bucc_vm_pop(vm);
            bucc_value_t a = bucc_vm_pop(vm);
            bucc_string_t* sa = bucc_value_to_string(&a);
            bucc_string_t* sb = bucc_value_to_string(&b);
            bucc_string_t* result = bucc_string_concat(sa, sb);
            bucc_vm_push(vm, BUCC_STRING_VAL(result));
            bucc_string_release(sa);
            bucc_string_release(sb);
            bucc_string_release(result);
            bucc_value_release(&a);
            bucc_value_release(&b);
            break;
        }
            
        case OP_JMP: {
            int32_t offset = read_i32(vm);
            vm->ip += offset;
            break;
        }
            
        case OP_JMP_FALSE: {
            int32_t offset = read_i32(vm);
            bucc_value_t cond = bucc_vm_pop(vm);
            if (!bucc_value_truthy(&cond)) {
                vm->ip += offset;
            }
            bucc_value_release(&cond);
            break;
        }
            
        case OP_JMP_TRUE: {
            int32_t offset = read_i32(vm);
            bucc_value_t cond = bucc_vm_pop(vm);
            if (bucc_value_truthy(&cond)) {
                vm->ip += offset;
            }
            bucc_value_release(&cond);
            break;
        }
            
        case OP_CALL: {
            uint16_t proc_id = read_u16(vm);
            uint8_t argc = read_u8(vm);
            
            if (proc_id >= vm->module->proc_count) {
                vm_error(vm, VM_ERROR, "invalid procedure id");
                break;
            }
            
            if (vm->fp >= BUCC_FRAMES_MAX) {
                vm_error(vm, VM_FRAME_OVERFLOW, "call stack overflow");
                break;
            }
            
            bucc_module_proc_t* proc = &vm->module->procedures[proc_id];
            
            bucc_frame_t* frame = &vm->frames[vm->fp++];
            frame->proc = proc;
            frame->ip = vm->ip;
            frame->bp = vm->sp - argc;
            frame->local_base = vm->local_count;
            
            for (uint8_t i = 0; i < argc; i++) {
                bucc_value_t arg = vm->stack[frame->bp + i];
                bucc_vm_set_local(vm, i, arg);
            }
            
            vm->ip = proc->code_offset;
            break;
        }
            
        case OP_CALL_HOST: {
            uint16_t import_id = read_u16(vm);
            uint8_t argc = read_u8(vm);
            
            if (import_id >= vm->module->import_count) {
                vm_error(vm, VM_ERROR, "invalid import id");
                break;
            }
            
            bucc_module_import_t* import = &vm->module->imports[import_id];
            
            if (vm->host_handler) {
                bucc_value_t args[16];
                int actual_argc = argc < 16 ? argc : 16;
                
                for (int i = actual_argc - 1; i >= 0; i--) {
                    args[i] = bucc_vm_pop(vm);
                }
                
                vm->counters.host_calls++;
                bucc_value_t result = vm->host_handler(vm, import->ns, import->fn,
                                                       args, actual_argc, vm->host_ctx);
                bucc_vm_push(vm, result);
                
                for (int i = 0; i < actual_argc; i++) {
                    bucc_value_release(&args[i]);
                }
            } else {
                for (int i = 0; i < argc; i++) {
                    bucc_value_t v = bucc_vm_pop(vm);
                    bucc_value_release(&v);
                }
                bucc_vm_push(vm, BUCC_NULL_VAL);
            }
            break;
        }
            
        case OP_RETURN: {
            if (vm->fp == 0) {
                vm->status = VM_HALT;
                vm->running = false;
            } else {
                bucc_frame_t* frame = &vm->frames[--vm->fp];
                
                while (vm->local_count > frame->local_base) {
                    bucc_value_release(&vm->locals[--vm->local_count]);
                }
                
                while (vm->sp > frame->bp) {
                    bucc_value_t v = bucc_vm_pop(vm);
                    bucc_value_release(&v);
                }
                
                vm->ip = frame->ip;
            }
            break;
        }
            
        case OP_RETURN_VALUE: {
            bucc_value_t ret = bucc_vm_pop(vm);
            
            if (vm->fp == 0) {
                bucc_vm_push(vm, ret);
                vm->status = VM_HALT;
                vm->running = false;
            } else {
                bucc_frame_t* frame = &vm->frames[--vm->fp];
                
                while (vm->local_count > frame->local_base) {
                    bucc_value_release(&vm->locals[--vm->local_count]);
                }
                
                while (vm->sp > frame->bp) {
                    bucc_value_t v = bucc_vm_pop(vm);
                    bucc_value_release(&v);
                }
                
                vm->ip = frame->ip;
                bucc_vm_push(vm, ret);
            }
            bucc_value_release(&ret);
            break;
        }
            
        case OP_CHAIN: {
            uint16_t target_idx = read_u16(vm);
            uint8_t has_args = read_u8(vm);
            
            const char* target = bucc_module_get_string(vm->module, target_idx);
            free(vm->chain_target);
            vm->chain_target = target ? strdup(target) : NULL;
            
            if (has_args) {
                bucc_value_release(&vm->chain_args);
                vm->chain_args = bucc_vm_pop(vm);
            }
            
            vm->status = VM_CHAIN;
            vm->running = false;
            break;
        }
            
        case OP_YIELD:
            vm->status = VM_YIELD;
            vm->running = false;
            break;
            
        case OP_ARRAY_NEW: {
            uint16_t count = read_u16(vm);
            bucc_array_t* arr = bucc_array_new(count > 0 ? count : 8);
            
            bucc_value_t* items = malloc(count * sizeof(bucc_value_t));
            for (int i = count - 1; i >= 0; i--) {
                items[i] = bucc_vm_pop(vm);
            }
            for (uint16_t i = 0; i < count; i++) {
                bucc_array_push(arr, items[i]);
                bucc_value_release(&items[i]);
            }
            free(items);
            
            bucc_vm_push(vm, BUCC_ARRAY_VAL(arr));
            bucc_array_release(arr);
            break;
        }
            
        case OP_ARRAY_GET: {
            bucc_value_t idx = bucc_vm_pop(vm);
            bucc_value_t arr = bucc_vm_pop(vm);
            
            if (BUCC_IS_ARRAY(arr) && BUCC_IS_I64(idx)) {
                bucc_value_t val = bucc_array_get(BUCC_AS_ARRAY(arr), (size_t)BUCC_AS_I64(idx));
                bucc_vm_push(vm, val);
                bucc_value_release(&val);
            } else {
                bucc_vm_push(vm, BUCC_NULL_VAL);
            }
            
            bucc_value_release(&arr);
            bucc_value_release(&idx);
            break;
        }
            
        case OP_ARRAY_SET: {
            bucc_value_t val = bucc_vm_pop(vm);
            bucc_value_t idx = bucc_vm_pop(vm);
            bucc_value_t arr = bucc_vm_pop(vm);
            
            if (BUCC_IS_ARRAY(arr) && BUCC_IS_I64(idx)) {
                bucc_array_set(BUCC_AS_ARRAY(arr), (size_t)BUCC_AS_I64(idx), val);
            }
            
            bucc_value_release(&arr);
            bucc_value_release(&idx);
            bucc_value_release(&val);
            break;
        }
            
        case OP_ARRAY_PUSH: {
            bucc_value_t val = bucc_vm_pop(vm);
            bucc_value_t arr = bucc_vm_pop(vm);
            
            if (BUCC_IS_ARRAY(arr)) {
                bucc_array_push(BUCC_AS_ARRAY(arr), val);
            }
            
            bucc_value_release(&arr);
            bucc_value_release(&val);
            break;
        }
            
        case OP_ARRAY_POP: {
            bucc_value_t arr = bucc_vm_pop(vm);
            
            if (BUCC_IS_ARRAY(arr)) {
                bucc_value_t val = bucc_array_pop(BUCC_AS_ARRAY(arr));
                bucc_vm_push(vm, val);
            } else {
                bucc_vm_push(vm, BUCC_NULL_VAL);
            }
            
            bucc_value_release(&arr);
            break;
        }
            
        case OP_MAP_NEW: {
            uint16_t count = read_u16(vm);
            bucc_map_t* map = bucc_map_new(count > 0 ? count : 8);
            
            for (uint16_t i = 0; i < count; i++) {
                bucc_value_t val = bucc_vm_pop(vm);
                bucc_value_t key = bucc_vm_pop(vm);
                
                if (BUCC_IS_STRING(key)) {
                    bucc_map_set(map, BUCC_AS_STRING(key), val);
                }
                
                bucc_value_release(&key);
                bucc_value_release(&val);
            }
            
            bucc_vm_push(vm, BUCC_MAP_VAL(map));
            bucc_map_release(map);
            break;
        }
            
        case OP_MAP_GET: {
            bucc_value_t key = bucc_vm_pop(vm);
            bucc_value_t map = bucc_vm_pop(vm);
            
            if (BUCC_IS_MAP(map) && BUCC_IS_STRING(key)) {
                bucc_value_t val = bucc_map_get(BUCC_AS_MAP(map), BUCC_AS_STRING(key));
                bucc_vm_push(vm, val);
                bucc_value_release(&val);
            } else {
                bucc_vm_push(vm, BUCC_NULL_VAL);
            }
            
            bucc_value_release(&map);
            bucc_value_release(&key);
            break;
        }
            
        case OP_MAP_SET: {
            bucc_value_t val = bucc_vm_pop(vm);
            bucc_value_t key = bucc_vm_pop(vm);
            bucc_value_t map = bucc_vm_pop(vm);
            
            if (BUCC_IS_MAP(map) && BUCC_IS_STRING(key)) {
                bucc_map_set(BUCC_AS_MAP(map), BUCC_AS_STRING(key), val);
            }
            
            bucc_value_release(&map);
            bucc_value_release(&key);
            bucc_value_release(&val);
            break;
        }
            
        case OP_MAP_HAS: {
            bucc_value_t key = bucc_vm_pop(vm);
            bucc_value_t map = bucc_vm_pop(vm);
            
            bool has = false;
            if (BUCC_IS_MAP(map) && BUCC_IS_STRING(key)) {
                has = bucc_map_has(BUCC_AS_MAP(map), BUCC_AS_STRING(key));
            }
            bucc_vm_push(vm, BUCC_BOOL_VAL(has));
            
            bucc_value_release(&map);
            bucc_value_release(&key);
            break;
        }
            
        case OP_MAP_KEYS: {
            bucc_value_t map = bucc_vm_pop(vm);
            
            if (BUCC_IS_MAP(map)) {
                bucc_array_t* keys = bucc_map_keys(BUCC_AS_MAP(map));
                bucc_vm_push(vm, BUCC_ARRAY_VAL(keys));
                bucc_array_release(keys);
            } else {
                bucc_vm_push(vm, BUCC_ARRAY_VAL(bucc_array_new(0)));
            }
            
            bucc_value_release(&map);
            break;
        }
            
        case OP_TRY_BEGIN: {
            int32_t handler_offset = read_i32(vm);
            
            bucc_try_frame_t* tf = calloc(1, sizeof(bucc_try_frame_t));
            if (tf) {
                tf->handler_ip = vm->ip + handler_offset;
                tf->frame_depth = vm->fp;
                tf->stack_depth = vm->sp;
                tf->next = vm->try_stack;
                vm->try_stack = tf;
            }
            break;
        }
            
        case OP_TRY_END: {
            if (vm->try_stack) {
                bucc_try_frame_t* tf = vm->try_stack;
                vm->try_stack = tf->next;
                free(tf);
            }
            break;
        }
            
        case OP_THROW: {
            bucc_value_t err = bucc_vm_pop(vm);
            bucc_vm_throw(vm, err);
            bucc_value_release(&err);
            
            if (!handle_exception(vm)) {
                vm_error(vm, VM_ERROR, "unhandled exception");
            }
            break;
        }
            
        case OP_RE_THROW: {
            if (vm->exception.active) {
                if (!handle_exception(vm)) {
                    vm_error(vm, VM_ERROR, "unhandled exception");
                }
            }
            break;
        }
            
        case OP_CAST_I64: {
            bucc_value_t v = bucc_vm_pop(vm);
            bool ok;
            double d = bucc_value_to_number(&v, &ok);
            bucc_vm_push(vm, BUCC_I64_VAL((int64_t)d));
            bucc_value_release(&v);
            break;
        }
            
        case OP_CAST_F64: {
            bucc_value_t v = bucc_vm_pop(vm);
            bool ok;
            double d = bucc_value_to_number(&v, &ok);
            bucc_vm_push(vm, BUCC_F64_VAL(d));
            bucc_value_release(&v);
            break;
        }
            
        case OP_CAST_STRING: {
            bucc_value_t v = bucc_vm_pop(vm);
            bucc_string_t* s = bucc_value_to_string(&v);
            bucc_vm_push(vm, BUCC_STRING_VAL(s));
            bucc_string_release(s);
            bucc_value_release(&v);
            break;
        }
            
        case OP_CAST_BOOL: {
            bucc_value_t v = bucc_vm_pop(vm);
            bucc_vm_push(vm, BUCC_BOOL_VAL(bucc_value_truthy(&v)));
            bucc_value_release(&v);
            break;
        }
            
        case OP_TYPEOF: {
            bucc_value_t v = bucc_vm_pop(vm);
            const char* name = bucc_value_kind_name(v.kind);
            bucc_string_t* s = bucc_string_from_cstr(name);
            bucc_vm_push(vm, BUCC_STRING_VAL(s));
            bucc_string_release(s);
            bucc_value_release(&v);
            break;
        }
            
        case OP_DEBUG_LINE: {
            read_u16(vm);
            break;
        }
            
        case OP_PROF_TICK:
            vm->counters.profile_ticks++;
            break;
            
        case OP_DISPATCH_CALL: {
            uint16_t base_proc = read_u16(vm);
            uint8_t target_count = read_u8(vm);
            bucc_value_t selector = bucc_vm_pop(vm);
            
            int64_t index = 0;
            if (BUCC_IS_I64(selector)) {
                index = selector.as.i64;
            } else if (BUCC_IS_F64(selector)) {
                index = (int64_t)selector.as.f64;
            }
            bucc_value_release(&selector);
            
            if (index >= 1 && index <= target_count) {
                uint32_t proc_id = base_proc + (uint32_t)(index - 1);
                if (proc_id < vm->module->proc_count) {
                    bucc_module_proc_t* proc = &((bucc_module_t*)vm->module)->procedures[proc_id];
                    if (vm->fp >= BUCC_FRAMES_MAX) {
                        vm_error(vm, VM_FRAME_OVERFLOW, "call stack overflow");
                        break;
                    }
                    vm->frames[vm->fp].proc = proc;
                    vm->frames[vm->fp].ip = vm->ip;
                    vm->frames[vm->fp].bp = vm->sp;
                    vm->frames[vm->fp].local_base = vm->local_count;
                    vm->fp++;
                    vm->ip = proc->code_offset;
                }
            }
            break;
        }
            
        case OP_RANGE_TEST: {
            read_u16(vm);
            read_u8(vm);
            bucc_value_t high = bucc_vm_pop(vm);
            bucc_value_t low = bucc_vm_pop(vm);
            bucc_value_t val = bucc_vm_pop(vm);
            
            bool in_range = false;
            if (BUCC_IS_I64(val) && BUCC_IS_I64(low) && BUCC_IS_I64(high)) {
                in_range = (val.as.i64 >= low.as.i64 && val.as.i64 <= high.as.i64);
            } else if (BUCC_IS_NUMERIC(val) && BUCC_IS_NUMERIC(low) && BUCC_IS_NUMERIC(high)) {
                double v = BUCC_IS_I64(val) ? (double)val.as.i64 : val.as.f64;
                double l = BUCC_IS_I64(low) ? (double)low.as.i64 : low.as.f64;
                double h = BUCC_IS_I64(high) ? (double)high.as.i64 : high.as.f64;
                in_range = (v >= l && v <= h);
            } else if (BUCC_IS_STRING(val) && BUCC_IS_STRING(low) && BUCC_IS_STRING(high)) {
                int cmp_low = bucc_string_cmp(val.as.str, low.as.str);
                int cmp_high = bucc_string_cmp(val.as.str, high.as.str);
                in_range = (cmp_low >= 0 && cmp_high <= 0);
            }
            
            bucc_value_release(&val);
            bucc_value_release(&low);
            bucc_value_release(&high);
            bucc_vm_push(vm, BUCC_BOOL_VAL(in_range));
            break;
        }
            
        case OP_CAST_DATE: {
            bucc_value_t v = bucc_vm_pop(vm);
            bucc_date_t date = {0};
            
            if (BUCC_IS_DATE(v)) {
                date = v.as.date;
            } else if (BUCC_IS_DATETIME(v)) {
                date.year = v.as.datetime.year;
                date.month = v.as.datetime.month;
                date.day = v.as.datetime.day;
            } else if (BUCC_IS_STRING(v) && v.as.str && v.as.str->data) {
                int y, m, d;
                if (sscanf(v.as.str->data, "%d-%d-%d", &y, &m, &d) == 3) {
                    date.year = (int16_t)y;
                    date.month = (uint8_t)m;
                    date.day = (uint8_t)d;
                }
            } else if (BUCC_IS_I64(v)) {
                int64_t days = v.as.i64;
                int64_t y = 1970;
                while (days >= 365) {
                    bool leap = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
                    int days_in_year = leap ? 366 : 365;
                    if (days >= days_in_year) {
                        days -= days_in_year;
                        y++;
                    } else {
                        break;
                    }
                }
                date.year = (int16_t)y;
                static const int days_in_month[] = {31,28,31,30,31,30,31,31,30,31,30,31};
                bool leap = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
                int m = 0;
                while (m < 12) {
                    int dim = days_in_month[m];
                    if (m == 1 && leap) dim = 29;
                    if (days < dim) break;
                    days -= dim;
                    m++;
                }
                date.month = (uint8_t)(m + 1);
                date.day = (uint8_t)(days + 1);
            }
            
            bucc_value_release(&v);
            bucc_vm_push(vm, BUCC_DATE_VAL(date));
            break;
        }
            
        case OP_CAST_DATETIME: {
            bucc_value_t v = bucc_vm_pop(vm);
            bucc_datetime_t dt = {0};
            
            if (BUCC_IS_DATETIME(v)) {
                dt = v.as.datetime;
            } else if (BUCC_IS_DATE(v)) {
                dt.year = v.as.date.year;
                dt.month = v.as.date.month;
                dt.day = v.as.date.day;
            } else if (BUCC_IS_STRING(v) && v.as.str && v.as.str->data) {
                int y, mo, d, h = 0, mi = 0, s = 0;
                if (sscanf(v.as.str->data, "%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &s) >= 3) {
                    dt.year = (int16_t)y;
                    dt.month = (uint8_t)mo;
                    dt.day = (uint8_t)d;
                    dt.hour = (uint8_t)h;
                    dt.minute = (uint8_t)mi;
                    dt.second = (uint8_t)s;
                }
            } else if (BUCC_IS_I64(v)) {
                int64_t ts = v.as.i64;
                int64_t days = ts / 86400;
                int64_t secs = ts % 86400;
                if (secs < 0) { secs += 86400; days--; }
                
                int64_t y = 1970;
                while (days >= 365) {
                    bool leap = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
                    int days_in_year = leap ? 366 : 365;
                    if (days >= days_in_year) {
                        days -= days_in_year;
                        y++;
                    } else {
                        break;
                    }
                }
                dt.year = (int16_t)y;
                static const int days_in_month[] = {31,28,31,30,31,30,31,31,30,31,30,31};
                bool leap = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
                int m = 0;
                while (m < 12) {
                    int dim = days_in_month[m];
                    if (m == 1 && leap) dim = 29;
                    if (days < dim) break;
                    days -= dim;
                    m++;
                }
                dt.month = (uint8_t)(m + 1);
                dt.day = (uint8_t)(days + 1);
                dt.hour = (uint8_t)(secs / 3600);
                dt.minute = (uint8_t)((secs % 3600) / 60);
                dt.second = (uint8_t)(secs % 60);
            }
            
            bucc_value_release(&v);
            bucc_vm_push(vm, BUCC_DATETIME_VAL(dt));
            break;
        }
            
        default:
            vm_error(vm, VM_INVALID_OPCODE, "invalid opcode");
            break;
    }
    
    return vm->status;
}

bucc_vm_status_t bucc_vm_run(bucc_vm_t* vm) {
    if (!vm) return VM_ERROR;
    
    vm->running = true;
    
    while (vm->running && vm->status == VM_OK) {
        bucc_vm_step(vm);
    }
    
    return vm->status;
}

bucc_vm_status_t bucc_vm_call(bucc_vm_t* vm, const char* proc_name,
                              bucc_value_t* args, int argc) {
    if (!vm || !vm->module || !proc_name) return VM_ERROR;
    
    bucc_module_proc_t* proc = bucc_module_find_proc((bucc_module_t*)vm->module, proc_name);
    if (!proc) {
        vm_error(vm, VM_ERROR, "procedure not found");
        return vm->status;
    }
    
    for (int i = 0; i < argc; i++) {
        bucc_vm_push(vm, args[i]);
    }
    
    vm->ip = proc->code_offset;
    
    return bucc_vm_run(vm);
}

const char* bucc_vm_status_name(bucc_vm_status_t status) {
    switch (status) {
        case VM_OK:             return "OK";
        case VM_HALT:           return "HALT";
        case VM_YIELD:          return "YIELD";
        case VM_CHAIN:          return "CHAIN";
        case VM_ERROR:          return "ERROR";
        case VM_STACK_OVERFLOW: return "STACK_OVERFLOW";
        case VM_STACK_UNDERFLOW:return "STACK_UNDERFLOW";
        case VM_FRAME_OVERFLOW: return "FRAME_OVERFLOW";
        case VM_INVALID_OPCODE: return "INVALID_OPCODE";
        case VM_DIVISION_BY_ZERO:return "DIVISION_BY_ZERO";
        case VM_TYPE_ERROR:     return "TYPE_ERROR";
        case VM_LIMIT_EXCEEDED: return "LIMIT_EXCEEDED";
        default:                return "UNKNOWN";
    }
}

const char* bucc_vm_get_error(bucc_vm_t* vm) {
    return vm ? vm->error_message : NULL;
}

void bucc_vm_dump_stack(bucc_vm_t* vm, FILE* out) {
    if (!vm || !out) return;
    
    fprintf(out, "Stack (sp=%u):\n", vm->sp);
    for (uint32_t i = 0; i < vm->sp; i++) {
        bucc_string_t* s = bucc_value_to_string(&vm->stack[i]);
        fprintf(out, "  [%u] %s: %s\n", i, bucc_value_kind_name(vm->stack[i].kind),
                s ? s->data : "?");
        bucc_string_release(s);
    }
}

void bucc_vm_dump_state(bucc_vm_t* vm, FILE* out) {
    if (!vm || !out) return;
    
    fprintf(out, "VM State:\n");
    fprintf(out, "  Status: %s\n", bucc_vm_status_name(vm->status));
    fprintf(out, "  IP: %u\n", vm->ip);
    fprintf(out, "  SP: %u\n", vm->sp);
    fprintf(out, "  FP: %u\n", vm->fp);
    fprintf(out, "  Instructions: %llu\n", (unsigned long long)vm->counters.instructions);
    fprintf(out, "  Host calls: %u\n", vm->counters.host_calls);
    
    if (vm->error_message) {
        fprintf(out, "  Error: %s\n", vm->error_message);
    }
    
    fprintf(out, "\n");
    bucc_vm_dump_stack(vm, out);
}
