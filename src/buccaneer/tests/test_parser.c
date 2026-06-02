#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "include/bucc_lexer.h"
#include "include/bucc_parser.h"
#include "include/bucc_ast.h"
#include "include/bucc_diag.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    printf("  Testing %s... ", #name); \
    fflush(stdout); \
    tests_run++; \
    if (test_##name()) { \
        printf("PASS\n"); \
        tests_passed++; \
    } else { \
        printf("FAIL\n"); \
    } \
} while(0)

static bucc_node_t* parse_source(const char* src, bucc_diag_t* diag) {
    size_t len = strlen(src);
    uint32_t file_id = bucc_diag_add_file(diag, "test.bucc", src, len);
    
    bucc_lexer_t lexer;
    bucc_lexer_init(&lexer, src, len, file_id, diag);
    
    bucc_parser_t parser;
    bucc_parser_init(&parser, &lexer, diag);
    
    return bucc_parse_module(&parser);
}

static int test_empty_module(void) {
    bucc_diag_t* diag = bucc_diag_new();
    const char* src = "";
    
    bucc_node_t* ast = parse_source(src, diag);
    int ok = ast != NULL && ast->kind == NODE_MODULE;
    
    bucc_node_free(ast);
    bucc_diag_free(diag);
    return ok;
}

static int test_metadata(void) {
    bucc_diag_t* diag = bucc_diag_new();
    const char* src = 
        "PROGRAM \"Test Program\"\n"
        "VERSION \"1.0.0\"\n";
    
    bucc_node_t* ast = parse_source(src, diag);
    int ok = ast != NULL;
    
    bucc_node_free(ast);
    bucc_diag_free(diag);
    return ok;
}

static int test_sub_definition(void) {
    bucc_diag_t* diag = bucc_diag_new();
    const char* src = 
        "SUB Main()\n"
        "END SUB\n";
    
    bucc_node_t* ast = parse_source(src, diag);
    int ok = ast != NULL;
    
    bucc_node_free(ast);
    bucc_diag_free(diag);
    return ok;
}

int main(void) {
    printf("=== Parser Tests ===\n\n");
    
    TEST(empty_module);
    TEST(metadata);
    TEST(sub_definition);
    
    printf("\n=== Results: %d/%d tests passed ===\n", tests_passed, tests_run);
    
    return tests_passed == tests_run ? 0 : 1;
}
