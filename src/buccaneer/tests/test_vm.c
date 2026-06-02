#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "include/bucc_vm.h"
#include "include/bucc_module.h"
#include "include/bucc_emit.h"
#include "include/bucc_value.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    printf("  Testing %s... ", #name); \
    tests_run++; \
    if (test_##name()) { \
        printf("PASS\n"); \
        tests_passed++; \
    } else { \
        printf("FAIL\n"); \
    } \
} while(0)

static bucc_module_t* create_simple_module(uint8_t* bytecode, size_t len) {
    bucc_module_t* mod = calloc(1, sizeof(bucc_module_t));
    if (!mod) return NULL;
    
    mod->bytecode = malloc(len);
    if (!mod->bytecode) {
        free(mod);
        return NULL;
    }
    memcpy(mod->bytecode, bytecode, len);
    mod->bytecode_len = (uint32_t)len;
    
    mod->proc_count = 1;
    mod->procedures = calloc(1, sizeof(bucc_module_proc_t));
    if (mod->procedures) {
        mod->procedures[0].name = strdup("Main");
        mod->procedures[0].id = 0;
        mod->procedures[0].code_offset = 0;
        mod->procedures[0].code_len = (uint32_t)len;
        mod->procedures[0].param_count = 0;
        mod->procedures[0].local_count = 0;
        mod->procedures[0].is_function = false;
    }
    
    mod->refcount = 1;
    return mod;
}

static int test_vm_create_destroy(void) {
    uint8_t code[] = { OP_HALT };
    bucc_module_t* mod = create_simple_module(code, sizeof(code));
    if (!mod) return 0;
    
    bucc_vm_t* vm = bucc_vm_new(mod);
    int ok = vm != NULL;
    
    bucc_vm_free(vm);
    bucc_module_release(mod);
    return ok;
}

static int test_vm_halt(void) {
    uint8_t code[] = { OP_HALT };
    bucc_module_t* mod = create_simple_module(code, sizeof(code));
    if (!mod) return 0;
    
    bucc_vm_t* vm = bucc_vm_new(mod);
    if (!vm) {
        bucc_module_release(mod);
        return 0;
    }
    
    bucc_vm_status_t result = bucc_vm_run(vm);
    int ok = result == VM_HALT;
    
    bucc_vm_free(vm);
    bucc_module_release(mod);
    return ok;
}

static int test_vm_push_pop(void) {
    uint8_t code[] = {
        OP_PUSH_TRUE,
        OP_POP,
        OP_HALT
    };
    bucc_module_t* mod = create_simple_module(code, sizeof(code));
    if (!mod) return 0;
    
    bucc_vm_t* vm = bucc_vm_new(mod);
    if (!vm) {
        bucc_module_release(mod);
        return 0;
    }
    
    bucc_vm_status_t result = bucc_vm_run(vm);
    int ok = result == VM_HALT;
    
    bucc_vm_free(vm);
    bucc_module_release(mod);
    return ok;
}

static int test_vm_push_null(void) {
    uint8_t code[] = {
        OP_PUSH_NULL,
        OP_HALT
    };
    bucc_module_t* mod = create_simple_module(code, sizeof(code));
    if (!mod) return 0;
    
    bucc_vm_t* vm = bucc_vm_new(mod);
    if (!vm) {
        bucc_module_release(mod);
        return 0;
    }
    
    bucc_vm_status_t result = bucc_vm_run(vm);
    int ok = result == VM_HALT;
    
    if (ok) {
        bucc_value_t top = bucc_vm_peek(vm, 0);
        ok = BUCC_IS_NULL(top);
    }
    
    bucc_vm_free(vm);
    bucc_module_release(mod);
    return ok;
}

static int test_vm_push_bool(void) {
    uint8_t code[] = {
        OP_PUSH_TRUE,
        OP_PUSH_FALSE,
        OP_HALT
    };
    bucc_module_t* mod = create_simple_module(code, sizeof(code));
    if (!mod) return 0;
    
    bucc_vm_t* vm = bucc_vm_new(mod);
    if (!vm) {
        bucc_module_release(mod);
        return 0;
    }
    
    bucc_vm_status_t result = bucc_vm_run(vm);
    int ok = result == VM_HALT;
    
    if (ok) {
        bucc_value_t top = bucc_vm_peek(vm, 0);
        ok = BUCC_IS_BOOL(top) && BUCC_AS_BOOL(top) == false;
        
        bucc_value_t second = bucc_vm_peek(vm, 1);
        ok = ok && BUCC_IS_BOOL(second) && BUCC_AS_BOOL(second) == true;
    }
    
    bucc_vm_free(vm);
    bucc_module_release(mod);
    return ok;
}

static int test_vm_arithmetic_add(void) {
    uint8_t code[] = {
        OP_PUSH_I64, 0, 0,
        OP_PUSH_I64, 0, 1,
        OP_ADD,
        OP_HALT
    };
    bucc_module_t* mod = create_simple_module(code, sizeof(code));
    if (!mod) return 0;
    
    mod->const_count = 2;
    mod->constants = malloc(2 * sizeof(bucc_value_t));
    mod->constants[0] = BUCC_I64_VAL(10);
    mod->constants[1] = BUCC_I64_VAL(20);
    
    bucc_vm_t* vm = bucc_vm_new(mod);
    if (!vm) {
        bucc_module_release(mod);
        return 0;
    }
    
    bucc_vm_status_t result = bucc_vm_run(vm);
    int ok = result == VM_HALT;
    
    if (ok) {
        bucc_value_t top = bucc_vm_peek(vm, 0);
        ok = BUCC_IS_I64(top) && BUCC_AS_I64(top) == 30;
    }
    
    bucc_vm_free(vm);
    bucc_module_release(mod);
    return ok;
}

static int test_vm_arithmetic_sub(void) {
    uint8_t code[] = {
        OP_PUSH_I64, 0, 0,
        OP_PUSH_I64, 0, 1,
        OP_SUB,
        OP_HALT
    };
    bucc_module_t* mod = create_simple_module(code, sizeof(code));
    if (!mod) return 0;
    
    mod->const_count = 2;
    mod->constants = malloc(2 * sizeof(bucc_value_t));
    mod->constants[0] = BUCC_I64_VAL(50);
    mod->constants[1] = BUCC_I64_VAL(20);
    
    bucc_vm_t* vm = bucc_vm_new(mod);
    if (!vm) {
        bucc_module_release(mod);
        return 0;
    }
    
    bucc_vm_status_t result = bucc_vm_run(vm);
    int ok = result == VM_HALT;
    
    if (ok) {
        bucc_value_t top = bucc_vm_peek(vm, 0);
        ok = BUCC_IS_I64(top) && BUCC_AS_I64(top) == 30;
    }
    
    bucc_vm_free(vm);
    bucc_module_release(mod);
    return ok;
}

static int test_vm_arithmetic_mul(void) {
    uint8_t code[] = {
        OP_PUSH_I64, 0, 0,
        OP_PUSH_I64, 0, 1,
        OP_MUL,
        OP_HALT
    };
    bucc_module_t* mod = create_simple_module(code, sizeof(code));
    if (!mod) return 0;
    
    mod->const_count = 2;
    mod->constants = malloc(2 * sizeof(bucc_value_t));
    mod->constants[0] = BUCC_I64_VAL(6);
    mod->constants[1] = BUCC_I64_VAL(7);
    
    bucc_vm_t* vm = bucc_vm_new(mod);
    if (!vm) {
        bucc_module_release(mod);
        return 0;
    }
    
    bucc_vm_status_t result = bucc_vm_run(vm);
    int ok = result == VM_HALT;
    
    if (ok) {
        bucc_value_t top = bucc_vm_peek(vm, 0);
        ok = BUCC_IS_I64(top) && BUCC_AS_I64(top) == 42;
    }
    
    bucc_vm_free(vm);
    bucc_module_release(mod);
    return ok;
}

static int test_vm_arithmetic_div(void) {
    uint8_t code[] = {
        OP_PUSH_I64, 0, 0,
        OP_PUSH_I64, 0, 1,
        OP_DIV,
        OP_HALT
    };
    bucc_module_t* mod = create_simple_module(code, sizeof(code));
    if (!mod) return 0;
    
    mod->const_count = 2;
    mod->constants = malloc(2 * sizeof(bucc_value_t));
    mod->constants[0] = BUCC_I64_VAL(100);
    mod->constants[1] = BUCC_I64_VAL(5);
    
    bucc_vm_t* vm = bucc_vm_new(mod);
    if (!vm) {
        bucc_module_release(mod);
        return 0;
    }
    
    bucc_vm_status_t result = bucc_vm_run(vm);
    int ok = result == VM_HALT;
    
    if (ok) {
        bucc_value_t top = bucc_vm_peek(vm, 0);
        ok = BUCC_IS_I64(top) && BUCC_AS_I64(top) == 20;
    }
    
    bucc_vm_free(vm);
    bucc_module_release(mod);
    return ok;
}

static int test_vm_arithmetic_mod(void) {
    uint8_t code[] = {
        OP_PUSH_I64, 0, 0,
        OP_PUSH_I64, 0, 1,
        OP_MOD,
        OP_HALT
    };
    bucc_module_t* mod = create_simple_module(code, sizeof(code));
    if (!mod) return 0;
    
    mod->const_count = 2;
    mod->constants = malloc(2 * sizeof(bucc_value_t));
    mod->constants[0] = BUCC_I64_VAL(17);
    mod->constants[1] = BUCC_I64_VAL(5);
    
    bucc_vm_t* vm = bucc_vm_new(mod);
    if (!vm) {
        bucc_module_release(mod);
        return 0;
    }
    
    bucc_vm_status_t result = bucc_vm_run(vm);
    int ok = result == VM_HALT;
    
    if (ok) {
        bucc_value_t top = bucc_vm_peek(vm, 0);
        ok = BUCC_IS_I64(top) && BUCC_AS_I64(top) == 2;
    }
    
    bucc_vm_free(vm);
    bucc_module_release(mod);
    return ok;
}

static int test_vm_comparison_eq(void) {
    uint8_t code[] = {
        OP_PUSH_I64, 0, 0,
        OP_PUSH_I64, 0, 0,
        OP_EQ,
        OP_HALT
    };
    bucc_module_t* mod = create_simple_module(code, sizeof(code));
    if (!mod) return 0;
    
    mod->const_count = 1;
    mod->constants = malloc(sizeof(bucc_value_t));
    mod->constants[0] = BUCC_I64_VAL(42);
    
    bucc_vm_t* vm = bucc_vm_new(mod);
    if (!vm) {
        bucc_module_release(mod);
        return 0;
    }
    
    bucc_vm_status_t result = bucc_vm_run(vm);
    int ok = result == VM_HALT;
    
    if (ok) {
        bucc_value_t top = bucc_vm_peek(vm, 0);
        ok = BUCC_IS_BOOL(top) && BUCC_AS_BOOL(top) == true;
    }
    
    bucc_vm_free(vm);
    bucc_module_release(mod);
    return ok;
}

static int test_vm_comparison_ne(void) {
    uint8_t code[] = {
        OP_PUSH_I64, 0, 0,
        OP_PUSH_I64, 0, 1,
        OP_NE,
        OP_HALT
    };
    bucc_module_t* mod = create_simple_module(code, sizeof(code));
    if (!mod) return 0;
    
    mod->const_count = 2;
    mod->constants = malloc(2 * sizeof(bucc_value_t));
    mod->constants[0] = BUCC_I64_VAL(10);
    mod->constants[1] = BUCC_I64_VAL(20);
    
    bucc_vm_t* vm = bucc_vm_new(mod);
    if (!vm) {
        bucc_module_release(mod);
        return 0;
    }
    
    bucc_vm_status_t result = bucc_vm_run(vm);
    int ok = result == VM_HALT;
    
    if (ok) {
        bucc_value_t top = bucc_vm_peek(vm, 0);
        ok = BUCC_IS_BOOL(top) && BUCC_AS_BOOL(top) == true;
    }
    
    bucc_vm_free(vm);
    bucc_module_release(mod);
    return ok;
}

static int test_vm_comparison_lt(void) {
    uint8_t code[] = {
        OP_PUSH_I64, 0, 0,
        OP_PUSH_I64, 0, 1,
        OP_LT,
        OP_HALT
    };
    bucc_module_t* mod = create_simple_module(code, sizeof(code));
    if (!mod) return 0;
    
    mod->const_count = 2;
    mod->constants = malloc(2 * sizeof(bucc_value_t));
    mod->constants[0] = BUCC_I64_VAL(5);
    mod->constants[1] = BUCC_I64_VAL(10);
    
    bucc_vm_t* vm = bucc_vm_new(mod);
    if (!vm) {
        bucc_module_release(mod);
        return 0;
    }
    
    bucc_vm_status_t result = bucc_vm_run(vm);
    int ok = result == VM_HALT;
    
    if (ok) {
        bucc_value_t top = bucc_vm_peek(vm, 0);
        ok = BUCC_IS_BOOL(top) && BUCC_AS_BOOL(top) == true;
    }
    
    bucc_vm_free(vm);
    bucc_module_release(mod);
    return ok;
}

static int test_vm_logical_and(void) {
    uint8_t code[] = {
        OP_PUSH_TRUE,
        OP_PUSH_TRUE,
        OP_AND,
        OP_HALT
    };
    bucc_module_t* mod = create_simple_module(code, sizeof(code));
    if (!mod) return 0;
    
    bucc_vm_t* vm = bucc_vm_new(mod);
    if (!vm) {
        bucc_module_release(mod);
        return 0;
    }
    
    bucc_vm_status_t result = bucc_vm_run(vm);
    int ok = result == VM_HALT;
    
    if (ok) {
        bucc_value_t top = bucc_vm_peek(vm, 0);
        ok = BUCC_IS_BOOL(top) && BUCC_AS_BOOL(top) == true;
    }
    
    bucc_vm_free(vm);
    bucc_module_release(mod);
    return ok;
}

static int test_vm_logical_or(void) {
    uint8_t code[] = {
        OP_PUSH_FALSE,
        OP_PUSH_TRUE,
        OP_OR,
        OP_HALT
    };
    bucc_module_t* mod = create_simple_module(code, sizeof(code));
    if (!mod) return 0;
    
    bucc_vm_t* vm = bucc_vm_new(mod);
    if (!vm) {
        bucc_module_release(mod);
        return 0;
    }
    
    bucc_vm_status_t result = bucc_vm_run(vm);
    int ok = result == VM_HALT;
    
    if (ok) {
        bucc_value_t top = bucc_vm_peek(vm, 0);
        ok = BUCC_IS_BOOL(top) && BUCC_AS_BOOL(top) == true;
    }
    
    bucc_vm_free(vm);
    bucc_module_release(mod);
    return ok;
}

static int test_vm_logical_not(void) {
    uint8_t code[] = {
        OP_PUSH_FALSE,
        OP_NOT,
        OP_HALT
    };
    bucc_module_t* mod = create_simple_module(code, sizeof(code));
    if (!mod) return 0;
    
    bucc_vm_t* vm = bucc_vm_new(mod);
    if (!vm) {
        bucc_module_release(mod);
        return 0;
    }
    
    bucc_vm_status_t result = bucc_vm_run(vm);
    int ok = result == VM_HALT;
    
    if (ok) {
        bucc_value_t top = bucc_vm_peek(vm, 0);
        ok = BUCC_IS_BOOL(top) && BUCC_AS_BOOL(top) == true;
    }
    
    bucc_vm_free(vm);
    bucc_module_release(mod);
    return ok;
}

static int test_vm_jump(void) {
    uint8_t code[] = {
        OP_JMP, 0, 0, 0, 2,
        OP_PUSH_FALSE,
        OP_HALT,
        OP_PUSH_TRUE,
        OP_HALT
    };
    bucc_module_t* mod = create_simple_module(code, sizeof(code));
    if (!mod) return 0;
    
    bucc_vm_t* vm = bucc_vm_new(mod);
    if (!vm) {
        bucc_module_release(mod);
        return 0;
    }
    
    bucc_vm_status_t result = bucc_vm_run(vm);
    int ok = result == VM_HALT;
    
    if (ok) {
        bucc_value_t top = bucc_vm_peek(vm, 0);
        ok = BUCC_IS_BOOL(top) && BUCC_AS_BOOL(top) == true;
    }
    
    bucc_vm_free(vm);
    bucc_module_release(mod);
    return ok;
}

static int test_vm_jump_false(void) {
    uint8_t code[] = {
        OP_PUSH_FALSE,
        OP_JMP_FALSE, 0, 0, 0, 4,
        OP_PUSH_I64, 0, 0,
        OP_HALT,
        OP_PUSH_I64, 0, 1,
        OP_HALT
    };
    bucc_module_t* mod = create_simple_module(code, sizeof(code));
    if (!mod) return 0;
    
    mod->const_count = 2;
    mod->constants = malloc(2 * sizeof(bucc_value_t));
    mod->constants[0] = BUCC_I64_VAL(100);
    mod->constants[1] = BUCC_I64_VAL(200);
    
    bucc_vm_t* vm = bucc_vm_new(mod);
    if (!vm) {
        bucc_module_release(mod);
        return 0;
    }
    
    bucc_vm_status_t result = bucc_vm_run(vm);
    int ok = result == VM_HALT;
    
    if (ok) {
        bucc_value_t top = bucc_vm_peek(vm, 0);
        ok = BUCC_IS_I64(top) && BUCC_AS_I64(top) == 200;
    }
    
    bucc_vm_free(vm);
    bucc_module_release(mod);
    return ok;
}

static int test_vm_dup(void) {
    uint8_t code[] = {
        OP_PUSH_I64, 0, 0,
        OP_DUP,
        OP_ADD,
        OP_HALT
    };
    bucc_module_t* mod = create_simple_module(code, sizeof(code));
    if (!mod) return 0;
    
    mod->const_count = 1;
    mod->constants = malloc(sizeof(bucc_value_t));
    mod->constants[0] = BUCC_I64_VAL(21);
    
    bucc_vm_t* vm = bucc_vm_new(mod);
    if (!vm) {
        bucc_module_release(mod);
        return 0;
    }
    
    bucc_vm_status_t result = bucc_vm_run(vm);
    int ok = result == VM_HALT;
    
    if (ok) {
        bucc_value_t top = bucc_vm_peek(vm, 0);
        ok = BUCC_IS_I64(top) && BUCC_AS_I64(top) == 42;
    }
    
    bucc_vm_free(vm);
    bucc_module_release(mod);
    return ok;
}

static int test_vm_swap(void) {
    uint8_t code[] = {
        OP_PUSH_I64, 0, 0,
        OP_PUSH_I64, 0, 1,
        OP_SWAP,
        OP_HALT
    };
    bucc_module_t* mod = create_simple_module(code, sizeof(code));
    if (!mod) return 0;
    
    mod->const_count = 2;
    mod->constants = malloc(2 * sizeof(bucc_value_t));
    mod->constants[0] = BUCC_I64_VAL(10);
    mod->constants[1] = BUCC_I64_VAL(20);
    
    bucc_vm_t* vm = bucc_vm_new(mod);
    if (!vm) {
        bucc_module_release(mod);
        return 0;
    }
    
    bucc_vm_status_t result = bucc_vm_run(vm);
    int ok = result == VM_HALT;
    
    if (ok) {
        bucc_value_t top = bucc_vm_peek(vm, 0);
        bucc_value_t second = bucc_vm_peek(vm, 1);
        ok = BUCC_IS_I64(top) && BUCC_AS_I64(top) == 10 &&
             BUCC_IS_I64(second) && BUCC_AS_I64(second) == 20;
    }
    
    bucc_vm_free(vm);
    bucc_module_release(mod);
    return ok;
}

static int test_vm_array_new(void) {
    uint8_t code[] = {
        OP_ARRAY_NEW, 0, 0,
        OP_HALT
    };
    bucc_module_t* mod = create_simple_module(code, sizeof(code));
    if (!mod) return 0;
    
    bucc_vm_t* vm = bucc_vm_new(mod);
    if (!vm) {
        bucc_module_release(mod);
        return 0;
    }
    
    bucc_vm_status_t result = bucc_vm_run(vm);
    int ok = result == VM_HALT;
    
    if (ok) {
        bucc_value_t top = bucc_vm_peek(vm, 0);
        ok = BUCC_IS_ARRAY(top);
    }
    
    bucc_vm_free(vm);
    bucc_module_release(mod);
    return ok;
}

static int test_vm_map_new(void) {
    uint8_t code[] = {
        OP_MAP_NEW, 0, 0,
        OP_HALT
    };
    bucc_module_t* mod = create_simple_module(code, sizeof(code));
    if (!mod) return 0;
    
    bucc_vm_t* vm = bucc_vm_new(mod);
    if (!vm) {
        bucc_module_release(mod);
        return 0;
    }
    
    bucc_vm_status_t result = bucc_vm_run(vm);
    int ok = result == VM_HALT;
    
    if (ok) {
        bucc_value_t top = bucc_vm_peek(vm, 0);
        ok = BUCC_IS_MAP(top);
    }
    
    bucc_vm_free(vm);
    bucc_module_release(mod);
    return ok;
}

static int test_vm_negation(void) {
    uint8_t code[] = {
        OP_PUSH_I64, 0, 0,
        OP_NEG,
        OP_HALT
    };
    bucc_module_t* mod = create_simple_module(code, sizeof(code));
    if (!mod) return 0;
    
    mod->const_count = 1;
    mod->constants = malloc(sizeof(bucc_value_t));
    mod->constants[0] = BUCC_I64_VAL(42);
    
    bucc_vm_t* vm = bucc_vm_new(mod);
    if (!vm) {
        bucc_module_release(mod);
        return 0;
    }
    
    bucc_vm_status_t result = bucc_vm_run(vm);
    int ok = result == VM_HALT;
    
    if (ok) {
        bucc_value_t top = bucc_vm_peek(vm, 0);
        ok = BUCC_IS_I64(top) && BUCC_AS_I64(top) == -42;
    }
    
    bucc_vm_free(vm);
    bucc_module_release(mod);
    return ok;
}

static int test_vm_float_arithmetic(void) {
    uint8_t code[] = {
        OP_PUSH_F64, 0, 0,
        OP_PUSH_F64, 0, 1,
        OP_ADD,
        OP_HALT
    };
    bucc_module_t* mod = create_simple_module(code, sizeof(code));
    if (!mod) return 0;
    
    mod->const_count = 2;
    mod->constants = malloc(2 * sizeof(bucc_value_t));
    mod->constants[0] = BUCC_F64_VAL(3.14);
    mod->constants[1] = BUCC_F64_VAL(2.86);
    
    bucc_vm_t* vm = bucc_vm_new(mod);
    if (!vm) {
        bucc_module_release(mod);
        return 0;
    }
    
    bucc_vm_status_t result = bucc_vm_run(vm);
    int ok = result == VM_HALT;
    
    if (ok) {
        bucc_value_t top = bucc_vm_peek(vm, 0);
        ok = BUCC_IS_F64(top);
        if (ok) {
            double val = BUCC_AS_F64(top);
            ok = val > 5.99 && val < 6.01;
        }
    }
    
    bucc_vm_free(vm);
    bucc_module_release(mod);
    return ok;
}

static int test_vm_typeof(void) {
    uint8_t code[] = {
        OP_PUSH_I64, 0, 0,
        OP_TYPEOF,
        OP_HALT
    };
    bucc_module_t* mod = create_simple_module(code, sizeof(code));
    if (!mod) return 0;
    
    mod->const_count = 1;
    mod->constants = malloc(sizeof(bucc_value_t));
    mod->constants[0] = BUCC_I64_VAL(42);
    
    bucc_vm_t* vm = bucc_vm_new(mod);
    if (!vm) {
        bucc_module_release(mod);
        return 0;
    }
    
    bucc_vm_status_t result = bucc_vm_run(vm);
    int ok = result == VM_HALT;
    
    if (ok) {
        bucc_value_t top = bucc_vm_peek(vm, 0);
        ok = BUCC_IS_STRING(top);
        if (ok) {
            bucc_string_t* s = BUCC_AS_STRING(top);
            ok = strcmp(s->data, "INTEGER") == 0;
        }
    }
    
    bucc_vm_free(vm);
    bucc_module_release(mod);
    return ok;
}

static int test_vm_cast_i64(void) {
    uint8_t code[] = {
        OP_PUSH_F64, 0, 0,
        OP_CAST_I64,
        OP_HALT
    };
    bucc_module_t* mod = create_simple_module(code, sizeof(code));
    if (!mod) return 0;
    
    mod->const_count = 1;
    mod->constants = malloc(sizeof(bucc_value_t));
    mod->constants[0] = BUCC_F64_VAL(42.9);
    
    bucc_vm_t* vm = bucc_vm_new(mod);
    if (!vm) {
        bucc_module_release(mod);
        return 0;
    }
    
    bucc_vm_status_t result = bucc_vm_run(vm);
    int ok = result == VM_HALT;
    
    if (ok) {
        bucc_value_t top = bucc_vm_peek(vm, 0);
        ok = BUCC_IS_I64(top) && BUCC_AS_I64(top) == 42;
    }
    
    bucc_vm_free(vm);
    bucc_module_release(mod);
    return ok;
}

static int test_vm_cast_f64(void) {
    uint8_t code[] = {
        OP_PUSH_I64, 0, 0,
        OP_CAST_F64,
        OP_HALT
    };
    bucc_module_t* mod = create_simple_module(code, sizeof(code));
    if (!mod) return 0;
    
    mod->const_count = 1;
    mod->constants = malloc(sizeof(bucc_value_t));
    mod->constants[0] = BUCC_I64_VAL(42);
    
    bucc_vm_t* vm = bucc_vm_new(mod);
    if (!vm) {
        bucc_module_release(mod);
        return 0;
    }
    
    bucc_vm_status_t result = bucc_vm_run(vm);
    int ok = result == VM_HALT;
    
    if (ok) {
        bucc_value_t top = bucc_vm_peek(vm, 0);
        ok = BUCC_IS_F64(top);
        if (ok) {
            double val = BUCC_AS_F64(top);
            ok = val > 41.9 && val < 42.1;
        }
    }
    
    bucc_vm_free(vm);
    bucc_module_release(mod);
    return ok;
}

static int test_vm_cast_bool(void) {
    uint8_t code[] = {
        OP_PUSH_I64, 0, 0,
        OP_CAST_BOOL,
        OP_HALT
    };
    bucc_module_t* mod = create_simple_module(code, sizeof(code));
    if (!mod) return 0;
    
    mod->const_count = 1;
    mod->constants = malloc(sizeof(bucc_value_t));
    mod->constants[0] = BUCC_I64_VAL(42);
    
    bucc_vm_t* vm = bucc_vm_new(mod);
    if (!vm) {
        bucc_module_release(mod);
        return 0;
    }
    
    bucc_vm_status_t result = bucc_vm_run(vm);
    int ok = result == VM_HALT;
    
    if (ok) {
        bucc_value_t top = bucc_vm_peek(vm, 0);
        ok = BUCC_IS_BOOL(top) && BUCC_AS_BOOL(top) == true;
    }
    
    bucc_vm_free(vm);
    bucc_module_release(mod);
    return ok;
}

int main(void) {
    printf("=== VM Execution Tests ===\n\n");
    
    TEST(vm_create_destroy);
    TEST(vm_halt);
    TEST(vm_push_pop);
    TEST(vm_push_null);
    TEST(vm_push_bool);
    TEST(vm_arithmetic_add);
    TEST(vm_arithmetic_sub);
    TEST(vm_arithmetic_mul);
    TEST(vm_arithmetic_div);
    TEST(vm_arithmetic_mod);
    TEST(vm_comparison_eq);
    TEST(vm_comparison_ne);
    TEST(vm_comparison_lt);
    TEST(vm_logical_and);
    TEST(vm_logical_or);
    TEST(vm_logical_not);
    TEST(vm_jump);
    TEST(vm_jump_false);
    TEST(vm_dup);
    TEST(vm_swap);
    TEST(vm_array_new);
    TEST(vm_map_new);
    TEST(vm_negation);
    TEST(vm_float_arithmetic);
    TEST(vm_typeof);
    TEST(vm_cast_i64);
    TEST(vm_cast_f64);
    TEST(vm_cast_bool);
    
    printf("\n=== Results: %d/%d tests passed ===\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
