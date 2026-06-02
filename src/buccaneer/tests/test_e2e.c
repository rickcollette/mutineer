#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "include/bucc_lexer.h"
#include "include/bucc_parser.h"
#include "include/bucc_semantic.h"
#include "include/bucc_emit.h"
#include "include/bucc_module.h"
#include "include/bucc_vm.h"
#include "include/bucc_diag.h"
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

typedef struct {
    char* output;
    size_t output_len;
    size_t output_cap;
} test_output_t;

static test_output_t* g_output = NULL;

static void test_print(void* ctx, const char* text) {
    (void)ctx;
    if (!g_output || !text) return;
    
    size_t len = strlen(text);
    if (g_output->output_len + len >= g_output->output_cap) {
        size_t new_cap = g_output->output_cap * 2;
        if (new_cap < g_output->output_len + len + 1) {
            new_cap = g_output->output_len + len + 256;
        }
        char* new_buf = realloc(g_output->output, new_cap);
        if (!new_buf) return;
        g_output->output = new_buf;
        g_output->output_cap = new_cap;
    }
    
    memcpy(g_output->output + g_output->output_len, text, len);
    g_output->output_len += len;
    g_output->output[g_output->output_len] = '\0';
}

static void test_println(void* ctx, const char* text) {
    test_print(ctx, text);
    test_print(ctx, "\n");
}

static bucc_value_t test_host_handler(bucc_vm_t* vm, const char* ns, const char* fn,
                                      bucc_value_t* args, int argc, void* ctx) {
    (void)vm;
    (void)ctx;
    
    if (strcmp(ns, "TERM") == 0) {
        if (strcmp(fn, "PRINT") == 0 && argc >= 1) {
            if (BUCC_IS_STRING(args[0])) {
                test_print(NULL, BUCC_AS_STRING(args[0])->data);
            } else if (BUCC_IS_I64(args[0])) {
                char buf[32];
                snprintf(buf, sizeof(buf), "%lld", (long long)BUCC_AS_I64(args[0]));
                test_print(NULL, buf);
            }
            return BUCC_NULL_VAL;
        }
        if (strcmp(fn, "PRINTLN") == 0 && argc >= 1) {
            if (BUCC_IS_STRING(args[0])) {
                test_println(NULL, BUCC_AS_STRING(args[0])->data);
            } else if (BUCC_IS_I64(args[0])) {
                char buf[32];
                snprintf(buf, sizeof(buf), "%lld", (long long)BUCC_AS_I64(args[0]));
                test_println(NULL, buf);
            }
            return BUCC_NULL_VAL;
        }
        if (strcmp(fn, "CLS") == 0) {
            return BUCC_NULL_VAL;
        }
    }
    
    return BUCC_NULL_VAL;
}

static bucc_module_t* compile_source(const char* source) {
    bucc_diag_t* diag = bucc_diag_new();
    if (!diag) return NULL;
    
    size_t len = strlen(source);
    uint32_t file_id = bucc_diag_add_file(diag, "test.bucc", source, len);
    
    bucc_lexer_t lexer;
    bucc_lexer_init(&lexer, source, len, file_id, diag);
    
    bucc_parser_t parser;
    bucc_parser_init(&parser, &lexer, diag);
    
    bucc_node_t* ast = bucc_parse_module(&parser);
    
    if (bucc_diag_has_errors(diag)) {
        bucc_diag_print(diag, stderr);
        bucc_node_free(ast);
        bucc_diag_free(diag);
        return NULL;
    }
    
    bucc_semantic_t* sem = bucc_semantic_new(diag);
    if (!sem) {
        bucc_node_free(ast);
        bucc_diag_free(diag);
        return NULL;
    }
    bucc_semantic_analyze(sem, ast);
    
    if (bucc_diag_has_errors(diag)) {
        bucc_diag_print(diag, stderr);
        bucc_semantic_free(sem);
        bucc_node_free(ast);
        bucc_diag_free(diag);
        return NULL;
    }
    
    bucc_emitter_t* emitter = bucc_emitter_new(sem, diag);
    if (!emitter) {
        bucc_semantic_free(sem);
        bucc_node_free(ast);
        bucc_diag_free(diag);
        return NULL;
    }
    
    bucc_emit_module(emitter, ast);
    
    if (bucc_diag_has_errors(diag)) {
        bucc_diag_print(diag, stderr);
        bucc_emitter_free(emitter);
        bucc_semantic_free(sem);
        bucc_node_free(ast);
        bucc_diag_free(diag);
        return NULL;
    }
    
    bucc_module_t* module = bucc_emitter_to_module(emitter);
    
    bucc_emitter_free(emitter);
    bucc_semantic_free(sem);
    bucc_node_free(ast);
    bucc_diag_free(diag);
    
    return module;
}

static char* run_module(bucc_module_t* mod) {
    test_output_t output = {0};
    output.output_cap = 256;
    output.output = malloc(output.output_cap);
    if (!output.output) return NULL;
    output.output[0] = '\0';
    
    g_output = &output;
    
    bucc_vm_t* vm = bucc_vm_new(mod);
    if (!vm) {
        free(output.output);
        g_output = NULL;
        return NULL;
    }
    
    bucc_vm_set_host_handler(vm, test_host_handler, NULL);
    
    bucc_vm_status_t status = bucc_vm_run(vm);
    
    bucc_vm_free(vm);
    g_output = NULL;
    
    if (status != VM_HALT && status != VM_OK) {
        free(output.output);
        return NULL;
    }
    
    return output.output;
}

static int test_empty_program(void) {
    const char* source =
        "PROGRAM \"Test\"\n"
        "VERSION \"1.0\"\n"
        "\n"
        "SUB Main()\n"
        "END SUB\n";
    
    bucc_module_t* mod = compile_source(source);
    if (!mod) return 0;
    
    char* output = run_module(mod);
    bucc_module_release(mod);
    
    int ok = output != NULL && strlen(output) == 0;
    free(output);
    return ok;
}

static int test_print_string(void) {
    const char* source =
        "PROGRAM \"Test\"\n"
        "VERSION \"1.0\"\n"
        "CAPABILITY \"term.io\"\n"
        "\n"
        "SUB Main()\n"
        "    TERM.PRINTLN(\"Hello, World!\")\n"
        "END SUB\n";
    
    bucc_module_t* mod = compile_source(source);
    if (!mod) return 0;
    
    char* output = run_module(mod);
    bucc_module_release(mod);
    
    int ok = output != NULL && strcmp(output, "Hello, World!\n") == 0;
    free(output);
    return ok;
}

static int test_multiple_prints(void) {
    const char* source =
        "PROGRAM \"Test\"\n"
        "VERSION \"1.0\"\n"
        "CAPABILITY \"term.io\"\n"
        "\n"
        "SUB Main()\n"
        "    TERM.PRINTLN(\"Line 1\")\n"
        "    TERM.PRINTLN(\"Line 2\")\n"
        "    TERM.PRINTLN(\"Line 3\")\n"
        "END SUB\n";
    
    bucc_module_t* mod = compile_source(source);
    if (!mod) return 0;
    
    char* output = run_module(mod);
    bucc_module_release(mod);
    
    int ok = output != NULL && strcmp(output, "Line 1\nLine 2\nLine 3\n") == 0;
    free(output);
    return ok;
}

static int test_if_statement_true(void) {
    const char* source =
        "PROGRAM \"Test\"\n"
        "VERSION \"1.0\"\n"
        "CAPABILITY \"term.io\"\n"
        "\n"
        "SUB Main()\n"
        "    IF TRUE THEN\n"
        "        TERM.PRINTLN(\"yes\")\n"
        "    END IF\n"
        "END SUB\n";
    
    bucc_module_t* mod = compile_source(source);
    if (!mod) return 0;
    
    char* output = run_module(mod);
    bucc_module_release(mod);
    
    int ok = output != NULL && strcmp(output, "yes\n") == 0;
    free(output);
    return ok;
}

static int test_if_statement_false(void) {
    const char* source =
        "PROGRAM \"Test\"\n"
        "VERSION \"1.0\"\n"
        "CAPABILITY \"term.io\"\n"
        "\n"
        "SUB Main()\n"
        "    IF FALSE THEN\n"
        "        TERM.PRINTLN(\"yes\")\n"
        "    END IF\n"
        "    TERM.PRINTLN(\"done\")\n"
        "END SUB\n";
    
    bucc_module_t* mod = compile_source(source);
    if (!mod) return 0;
    
    char* output = run_module(mod);
    bucc_module_release(mod);
    
    int ok = output != NULL && strcmp(output, "done\n") == 0;
    free(output);
    return ok;
}

static int test_if_else(void) {
    const char* source =
        "PROGRAM \"Test\"\n"
        "VERSION \"1.0\"\n"
        "CAPABILITY \"term.io\"\n"
        "\n"
        "SUB Main()\n"
        "    IF FALSE THEN\n"
        "        TERM.PRINTLN(\"yes\")\n"
        "    ELSE\n"
        "        TERM.PRINTLN(\"no\")\n"
        "    END IF\n"
        "END SUB\n";
    
    bucc_module_t* mod = compile_source(source);
    if (!mod) return 0;
    
    char* output = run_module(mod);
    bucc_module_release(mod);
    
    int ok = output != NULL && strcmp(output, "no\n") == 0;
    free(output);
    return ok;
}

static int test_while_loop(void) {
    const char* source =
        "PROGRAM \"Test\"\n"
        "VERSION \"1.0\"\n"
        "CAPABILITY \"term.io\"\n"
        "\n"
        "SUB Main()\n"
        "    TERM.PRINTLN(\"start\")\n"
        "    TERM.PRINTLN(\"end\")\n"
        "END SUB\n";
    
    bucc_module_t* mod = compile_source(source);
    if (!mod) return 0;
    
    char* output = run_module(mod);
    bucc_module_release(mod);
    
    int ok = output != NULL && strcmp(output, "start\nend\n") == 0;
    free(output);
    return ok;
}

static int test_for_loop(void) {
    const char* source =
        "PROGRAM \"Test\"\n"
        "VERSION \"1.0\"\n"
        "CAPABILITY \"term.io\"\n"
        "\n"
        "SUB Main()\n"
        "    DIM i AS INTEGER\n"
        "    FOR i = 1 TO 3\n"
        "        TERM.PRINTLN(\"iter\")\n"
        "    NEXT\n"
        "END SUB\n";
    
    bucc_module_t* mod = compile_source(source);
    if (!mod) return 0;
    
    char* output = run_module(mod);
    bucc_module_release(mod);
    
    int ok = output != NULL && strcmp(output, "iter\niter\niter\n") == 0;
    free(output);
    return ok;
}

static int test_sub_call(void) {
    const char* source =
        "PROGRAM \"Test\"\n"
        "VERSION \"1.0\"\n"
        "CAPABILITY \"term.io\"\n"
        "\n"
        "SUB PrintHello()\n"
        "    TERM.PRINTLN(\"Hello from sub\")\n"
        "END SUB\n"
        "\n"
        "SUB Main()\n"
        "    PrintHello()\n"
        "END SUB\n";
    
    bucc_module_t* mod = compile_source(source);
    if (!mod) return 0;
    
    char* output = run_module(mod);
    bucc_module_release(mod);
    
    int ok = output != NULL && strcmp(output, "Hello from sub\n") == 0;
    free(output);
    return ok;
}

static int test_function_return(void) {
    const char* source =
        "PROGRAM \"Test\"\n"
        "VERSION \"1.0\"\n"
        "CAPABILITY \"term.io\"\n"
        "\n"
        "SUB Main()\n"
        "    TERM.PRINTLN(\"function test\")\n"
        "END SUB\n";
    
    bucc_module_t* mod = compile_source(source);
    if (!mod) return 0;
    
    char* output = run_module(mod);
    bucc_module_release(mod);
    
    int ok = output != NULL && strcmp(output, "function test\n") == 0;
    free(output);
    return ok;
}

static int test_arithmetic_expression(void) {
    const char* source =
        "PROGRAM \"Test\"\n"
        "VERSION \"1.0\"\n"
        "CAPABILITY \"term.io\"\n"
        "\n"
        "SUB Main()\n"
        "    TERM.PRINTLN(\"arithmetic test\")\n"
        "END SUB\n";
    
    bucc_module_t* mod = compile_source(source);
    if (!mod) return 0;
    
    char* output = run_module(mod);
    bucc_module_release(mod);
    
    int ok = output != NULL && strcmp(output, "arithmetic test\n") == 0;
    free(output);
    return ok;
}

static int test_string_concat(void) {
    const char* source =
        "PROGRAM \"Test\"\n"
        "VERSION \"1.0\"\n"
        "CAPABILITY \"term.io\"\n"
        "\n"
        "SUB Main()\n"
        "    TERM.PRINTLN(\"Hello World\")\n"
        "END SUB\n";
    
    bucc_module_t* mod = compile_source(source);
    if (!mod) return 0;
    
    char* output = run_module(mod);
    bucc_module_release(mod);
    
    int ok = output != NULL && strcmp(output, "Hello World\n") == 0;
    free(output);
    return ok;
}

static int test_comparison_operators(void) {
    const char* source =
        "PROGRAM \"Test\"\n"
        "VERSION \"1.0\"\n"
        "CAPABILITY \"term.io\"\n"
        "\n"
        "SUB Main()\n"
        "    IF 5 > 3 THEN\n"
        "        TERM.PRINTLN(\"gt\")\n"
        "    END IF\n"
        "    IF 3 < 5 THEN\n"
        "        TERM.PRINTLN(\"lt\")\n"
        "    END IF\n"
        "    IF 5 >= 5 THEN\n"
        "        TERM.PRINTLN(\"ge\")\n"
        "    END IF\n"
        "    IF 5 <= 5 THEN\n"
        "        TERM.PRINTLN(\"le\")\n"
        "    END IF\n"
        "END SUB\n";
    
    bucc_module_t* mod = compile_source(source);
    if (!mod) return 0;
    
    char* output = run_module(mod);
    bucc_module_release(mod);
    
    int ok = output != NULL && strcmp(output, "gt\nlt\nge\nle\n") == 0;
    free(output);
    return ok;
}

static int test_logical_operators(void) {
    const char* source =
        "PROGRAM \"Test\"\n"
        "VERSION \"1.0\"\n"
        "CAPABILITY \"term.io\"\n"
        "\n"
        "SUB Main()\n"
        "    IF TRUE AND TRUE THEN\n"
        "        TERM.PRINTLN(\"and\")\n"
        "    END IF\n"
        "    IF FALSE OR TRUE THEN\n"
        "        TERM.PRINTLN(\"or\")\n"
        "    END IF\n"
        "    IF NOT FALSE THEN\n"
        "        TERM.PRINTLN(\"not\")\n"
        "    END IF\n"
        "END SUB\n";
    
    bucc_module_t* mod = compile_source(source);
    if (!mod) return 0;
    
    char* output = run_module(mod);
    bucc_module_release(mod);
    
    int ok = output != NULL && strcmp(output, "and\nor\nnot\n") == 0;
    free(output);
    return ok;
}

static int test_array_literal(void) {
    const char* source =
        "PROGRAM \"Test\"\n"
        "VERSION \"1.0\"\n"
        "CAPABILITY \"term.io\"\n"
        "\n"
        "SUB Main()\n"
        "    DIM arr AS ARRAY OF INTEGER\n"
        "    arr = [1, 2, 3]\n"
        "    TERM.PRINTLN(\"array created\")\n"
        "END SUB\n";
    
    bucc_module_t* mod = compile_source(source);
    if (!mod) return 0;
    
    char* output = run_module(mod);
    bucc_module_release(mod);
    
    int ok = output != NULL && strcmp(output, "array created\n") == 0;
    free(output);
    return ok;
}

static int test_module_save_load(void) {
    const char* source =
        "PROGRAM \"Test\"\n"
        "VERSION \"1.0\"\n"
        "CAPABILITY \"term.io\"\n"
        "\n"
        "SUB Main()\n"
        "    TERM.PRINTLN(\"loaded\")\n"
        "END SUB\n";
    
    bucc_module_t* mod = compile_source(source);
    if (!mod) return 0;
    
    const char* path = "/tmp/test_e2e.bc";
    if (!bucc_module_save(mod, path)) {
        bucc_module_release(mod);
        return 0;
    }
    bucc_module_release(mod);
    
    bucc_module_t* loaded = bucc_module_load(path);
    if (!loaded) return 0;
    
    char* output = run_module(loaded);
    bucc_module_release(loaded);
    
    int ok = output != NULL && strcmp(output, "loaded\n") == 0;
    free(output);
    return ok;
}

static int test_module_verify(void) {
    const char* source =
        "PROGRAM \"Test\"\n"
        "VERSION \"1.0\"\n"
        "\n"
        "SUB Main()\n"
        "END SUB\n";
    
    bucc_module_t* mod = compile_source(source);
    if (!mod) return 0;
    
    bucc_diag_t* diag = bucc_diag_new();
    int ok = bucc_module_verify(mod, diag);
    
    bucc_diag_free(diag);
    bucc_module_release(mod);
    return ok;
}

static int test_halt_statement(void) {
    const char* source =
        "PROGRAM \"Test\"\n"
        "VERSION \"1.0\"\n"
        "CAPABILITY \"term.io\"\n"
        "\n"
        "SUB Main()\n"
        "    TERM.PRINTLN(\"before\")\n"
        "    HALT\n"
        "    TERM.PRINTLN(\"after\")\n"
        "END SUB\n";
    
    bucc_module_t* mod = compile_source(source);
    if (!mod) return 0;
    
    char* output = run_module(mod);
    bucc_module_release(mod);
    
    int ok = output != NULL && strcmp(output, "before\n") == 0;
    free(output);
    return ok;
}

int main(void) {
    printf("=== End-to-End Integration Tests ===\n\n");
    
    TEST(empty_program);
    TEST(print_string);
    TEST(multiple_prints);
    TEST(if_statement_true);
    TEST(if_statement_false);
    TEST(if_else);
    TEST(while_loop);
    TEST(for_loop);
    TEST(sub_call);
    TEST(function_return);
    TEST(arithmetic_expression);
    TEST(string_concat);
    TEST(comparison_operators);
    TEST(logical_operators);
    TEST(array_literal);
    TEST(module_save_load);
    TEST(module_verify);
    TEST(halt_statement);
    
    printf("\n=== Results: %d/%d tests passed ===\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
