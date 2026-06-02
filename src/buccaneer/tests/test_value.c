#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
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

static int test_null_value(void) {
    bucc_value_t v = BUCC_NULL_VAL;
    return BUCC_IS_NULL(v);
}

static int test_bool_value(void) {
    bucc_value_t t = BUCC_BOOL_VAL(true);
    bucc_value_t f = BUCC_BOOL_VAL(false);
    return BUCC_IS_BOOL(t) && BUCC_AS_BOOL(t) == true &&
           BUCC_IS_BOOL(f) && BUCC_AS_BOOL(f) == false;
}

static int test_int_value(void) {
    bucc_value_t v = BUCC_I64_VAL(42);
    return BUCC_IS_I64(v) && BUCC_AS_I64(v) == 42;
}

static int test_float_value(void) {
    bucc_value_t v = BUCC_F64_VAL(3.14);
    return BUCC_IS_F64(v) && BUCC_AS_F64(v) > 3.13 && BUCC_AS_F64(v) < 3.15;
}

static int test_string_new(void) {
    bucc_string_t* s = bucc_string_new("hello", 5);
    int ok = s != NULL && s->len == 5 && strcmp(s->data, "hello") == 0;
    bucc_string_release(s);
    return ok;
}

static int test_string_concat(void) {
    bucc_string_t* a = bucc_string_new("hello", 5);
    bucc_string_t* b = bucc_string_new(" world", 6);
    bucc_string_t* c = bucc_string_concat(a, b);
    int ok = c != NULL && c->len == 11 && strcmp(c->data, "hello world") == 0;
    bucc_string_release(a);
    bucc_string_release(b);
    bucc_string_release(c);
    return ok;
}

static int test_array_basic(void) {
    bucc_array_t* arr = bucc_array_new(4);
    if (!arr) return 0;
    
    bucc_value_t v1 = BUCC_I64_VAL(10);
    bucc_value_t v2 = BUCC_I64_VAL(20);
    bucc_value_t v3 = BUCC_I64_VAL(30);
    
    bucc_array_push(arr, v1);
    bucc_array_push(arr, v2);
    bucc_array_push(arr, v3);
    
    int ok = arr->len == 3;
    
    bucc_value_t got = bucc_array_get(arr, 1);
    ok = ok && BUCC_IS_I64(got) && BUCC_AS_I64(got) == 20;
    
    bucc_array_release(arr);
    return ok;
}

static int test_map_basic(void) {
    bucc_map_t* map = bucc_map_new(8);
    if (!map) return 0;
    
    bucc_string_t* key1 = bucc_string_new("name", 4);
    bucc_string_t* key2 = bucc_string_new("age", 3);
    
    bucc_string_t* val_str = bucc_string_new("Alice", 5);
    bucc_value_t v1 = BUCC_STRING_VAL(val_str);
    bucc_value_t v2 = BUCC_I64_VAL(30);
    
    bucc_map_set(map, key1, v1);
    bucc_map_set(map, key2, v2);
    
    int ok = map->len == 2;
    
    bucc_value_t got = bucc_map_get(map, key1);
    ok = ok && BUCC_IS_STRING(got);
    
    got = bucc_map_get(map, key2);
    ok = ok && BUCC_IS_I64(got) && BUCC_AS_I64(got) == 30;
    
    bucc_string_release(key1);
    bucc_string_release(key2);
    bucc_map_release(map);
    return ok;
}

static int test_map_cstr_helpers(void) {
    bucc_map_t* map = bucc_map_new(8);
    if (!map) return 0;
    
    bucc_value_t v1 = BUCC_I64_VAL(100);
    bucc_value_t v2 = BUCC_I64_VAL(200);
    
    bucc_map_set_cstr(map, "score", v1);
    bucc_map_set_cstr(map, "level", v2);
    
    int ok = bucc_map_has_cstr(map, "score");
    ok = ok && bucc_map_has_cstr(map, "level");
    ok = ok && !bucc_map_has_cstr(map, "missing");
    
    bucc_value_t* got = bucc_map_get_cstr(map, "score");
    ok = ok && got != NULL && BUCC_IS_I64(*got) && BUCC_AS_I64(*got) == 100;
    
    bucc_map_delete_cstr(map, "score");
    ok = ok && !bucc_map_has_cstr(map, "score");
    
    bucc_map_release(map);
    return ok;
}

static int test_value_equals(void) {
    bucc_value_t a = BUCC_I64_VAL(42);
    bucc_value_t b = BUCC_I64_VAL(42);
    bucc_value_t c = BUCC_I64_VAL(43);
    bucc_value_t d = BUCC_F64_VAL(42.0);
    
    int ok = bucc_value_equals(a, b);
    ok = ok && !bucc_value_equals(a, c);
    ok = ok && !bucc_value_equals(a, d);
    
    return ok;
}

static int test_make_string(void) {
    bucc_value_t v = bucc_make_string("test");
    int ok = BUCC_IS_STRING(v) && strcmp(BUCC_AS_STRING(v)->data, "test") == 0;
    bucc_value_release(&v);
    return ok;
}

static int test_make_error(void) {
    bucc_value_t v = bucc_make_error("something went wrong");
    int ok = BUCC_IS_ERROR(v) && strcmp(BUCC_AS_ERROR(v)->message, "something went wrong") == 0;
    bucc_value_release(&v);
    return ok;
}

int main(void) {
    printf("=== Value System Tests ===\n\n");
    
    TEST(null_value);
    TEST(bool_value);
    TEST(int_value);
    TEST(float_value);
    TEST(string_new);
    TEST(string_concat);
    TEST(array_basic);
    TEST(map_basic);
    TEST(map_cstr_helpers);
    TEST(value_equals);
    TEST(make_string);
    TEST(make_error);
    
    printf("\n=== Results: %d/%d tests passed ===\n", tests_passed, tests_run);
    
    return tests_passed == tests_run ? 0 : 1;
}
