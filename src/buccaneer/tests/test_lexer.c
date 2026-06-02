#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "include/bucc_lexer.h"
#include "include/bucc_diag.h"

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

static int test_empty_input(void) {
    bucc_diag_t* diag = bucc_diag_new();
    const char* src = "";
    uint32_t file_id = bucc_diag_add_file(diag, "test.bucc", src, 0);
    
    bucc_lexer_t lexer;
    bucc_lexer_init(&lexer, src, 0, file_id, diag);
    
    bucc_token_t tok = bucc_lexer_next(&lexer);
    int ok = tok.kind == TOK_EOF;
    
    bucc_diag_free(diag);
    return ok;
}

static int test_keywords(void) {
    bucc_diag_t* diag = bucc_diag_new();
    const char* src = "IF THEN ELSE END SUB DIM AS";
    size_t len = strlen(src);
    uint32_t file_id = bucc_diag_add_file(diag, "test.bucc", src, len);
    
    bucc_lexer_t lexer;
    bucc_lexer_init(&lexer, src, len, file_id, diag);
    
    bucc_token_t tok;
    
    tok = bucc_lexer_next(&lexer);
    int ok = tok.kind == TOK_KW_IF;
    
    tok = bucc_lexer_next(&lexer);
    ok = ok && tok.kind == TOK_KW_THEN;
    
    tok = bucc_lexer_next(&lexer);
    ok = ok && tok.kind == TOK_KW_ELSE;
    
    tok = bucc_lexer_next(&lexer);
    ok = ok && tok.kind == TOK_KW_END;
    
    tok = bucc_lexer_next(&lexer);
    ok = ok && tok.kind == TOK_KW_SUB;
    
    tok = bucc_lexer_next(&lexer);
    ok = ok && tok.kind == TOK_KW_DIM;
    
    tok = bucc_lexer_next(&lexer);
    ok = ok && tok.kind == TOK_KW_AS;
    
    bucc_diag_free(diag);
    return ok;
}

static int test_identifiers(void) {
    bucc_diag_t* diag = bucc_diag_new();
    const char* src = "myVar userName score123";
    size_t len = strlen(src);
    uint32_t file_id = bucc_diag_add_file(diag, "test.bucc", src, len);
    
    bucc_lexer_t lexer;
    bucc_lexer_init(&lexer, src, len, file_id, diag);
    
    bucc_token_t tok;
    
    tok = bucc_lexer_next(&lexer);
    int ok = tok.kind == TOK_IDENT;
    
    tok = bucc_lexer_next(&lexer);
    ok = ok && tok.kind == TOK_IDENT;
    
    tok = bucc_lexer_next(&lexer);
    ok = ok && tok.kind == TOK_IDENT;
    
    bucc_diag_free(diag);
    return ok;
}

static int test_numbers(void) {
    bucc_diag_t* diag = bucc_diag_new();
    const char* src = "42 3.14 0 100";
    size_t len = strlen(src);
    uint32_t file_id = bucc_diag_add_file(diag, "test.bucc", src, len);
    
    bucc_lexer_t lexer;
    bucc_lexer_init(&lexer, src, len, file_id, diag);
    
    bucc_token_t tok;
    
    tok = bucc_lexer_next(&lexer);
    int ok = tok.kind == TOK_INTEGER;
    
    tok = bucc_lexer_next(&lexer);
    ok = ok && tok.kind == TOK_DOUBLE;
    
    tok = bucc_lexer_next(&lexer);
    ok = ok && tok.kind == TOK_INTEGER;
    
    tok = bucc_lexer_next(&lexer);
    ok = ok && tok.kind == TOK_INTEGER;
    
    bucc_diag_free(diag);
    return ok;
}

static int test_strings(void) {
    bucc_diag_t* diag = bucc_diag_new();
    const char* src = "\"hello\" \"world\"";
    size_t len = strlen(src);
    uint32_t file_id = bucc_diag_add_file(diag, "test.bucc", src, len);
    
    bucc_lexer_t lexer;
    bucc_lexer_init(&lexer, src, len, file_id, diag);
    
    bucc_token_t tok;
    
    tok = bucc_lexer_next(&lexer);
    int ok = tok.kind == TOK_STRING;
    
    tok = bucc_lexer_next(&lexer);
    ok = ok && tok.kind == TOK_STRING;
    
    bucc_diag_free(diag);
    return ok;
}

static int test_operators(void) {
    bucc_diag_t* diag = bucc_diag_new();
    const char* src = "+ - * / = <> < > <= >= ( ) , .";
    size_t len = strlen(src);
    uint32_t file_id = bucc_diag_add_file(diag, "test.bucc", src, len);
    
    bucc_lexer_t lexer;
    bucc_lexer_init(&lexer, src, len, file_id, diag);
    
    bucc_token_t tok;
    int count = 0;
    
    while ((tok = bucc_lexer_next(&lexer)).kind != TOK_EOF) {
        count++;
    }
    
    bucc_diag_free(diag);
    return count == 14;
}

static int test_comments(void) {
    bucc_diag_t* diag = bucc_diag_new();
    const char* src = "DIM x ' this is a comment\nDIM y";
    size_t len = strlen(src);
    uint32_t file_id = bucc_diag_add_file(diag, "test.bucc", src, len);
    
    bucc_lexer_t lexer;
    bucc_lexer_init(&lexer, src, len, file_id, diag);
    
    bucc_token_t tok;
    
    tok = bucc_lexer_next(&lexer);
    int ok = tok.kind == TOK_KW_DIM;
    
    tok = bucc_lexer_next(&lexer);
    ok = ok && tok.kind == TOK_IDENT;
    
    tok = bucc_lexer_next(&lexer);
    while (tok.kind == TOK_COMMENT || tok.kind == TOK_NEWLINE) {
        tok = bucc_lexer_next(&lexer);
    }
    ok = ok && tok.kind == TOK_KW_DIM;
    
    bucc_diag_free(diag);
    return ok;
}

static int test_logical_operators(void) {
    bucc_diag_t* diag = bucc_diag_new();
    const char* src = "AND OR NOT";
    size_t len = strlen(src);
    uint32_t file_id = bucc_diag_add_file(diag, "test.bucc", src, len);
    
    bucc_lexer_t lexer;
    bucc_lexer_init(&lexer, src, len, file_id, diag);
    
    bucc_token_t tok;
    
    tok = bucc_lexer_next(&lexer);
    int ok = tok.kind == TOK_KW_AND;
    
    tok = bucc_lexer_next(&lexer);
    ok = ok && tok.kind == TOK_KW_OR;
    
    tok = bucc_lexer_next(&lexer);
    ok = ok && tok.kind == TOK_KW_NOT;
    
    bucc_diag_free(diag);
    return ok;
}

static int test_type_keywords(void) {
    bucc_diag_t* diag = bucc_diag_new();
    const char* src = "INTEGER DOUBLE BOOLEAN STRING";
    size_t len = strlen(src);
    uint32_t file_id = bucc_diag_add_file(diag, "test.bucc", src, len);
    
    bucc_lexer_t lexer;
    bucc_lexer_init(&lexer, src, len, file_id, diag);
    
    bucc_token_t tok;
    
    tok = bucc_lexer_next(&lexer);
    int ok = tok.kind == TOK_KW_INTEGER;
    
    tok = bucc_lexer_next(&lexer);
    ok = ok && tok.kind == TOK_KW_DOUBLE;
    
    tok = bucc_lexer_next(&lexer);
    ok = ok && tok.kind == TOK_KW_BOOLEAN;
    
    tok = bucc_lexer_next(&lexer);
    ok = ok && tok.kind == TOK_KW_STRING;
    
    bucc_diag_free(diag);
    return ok;
}

int main(void) {
    printf("=== Lexer Tests ===\n\n");
    
    TEST(empty_input);
    TEST(keywords);
    TEST(identifiers);
    TEST(numbers);
    TEST(strings);
    TEST(operators);
    TEST(comments);
    TEST(logical_operators);
    TEST(type_keywords);
    
    printf("\n=== Results: %d/%d tests passed ===\n", tests_passed, tests_run);
    
    return tests_passed == tests_run ? 0 : 1;
}
