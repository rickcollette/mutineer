#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "include/bucc_package.h"

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

static int test_pkg_error_strings(void) {
    const char* s1 = bucc_pkg_error_string(PKG_OK);
    const char* s2 = bucc_pkg_error_string(PKG_FILE_NOT_FOUND);
    const char* s3 = bucc_pkg_error_string(PKG_PARSE_ERROR);
    
    return s1 != NULL && s2 != NULL && s3 != NULL;
}

static int test_manifest_free_null(void) {
    bucc_manifest_free(NULL);
    return 1;
}

static int test_manifest_validate_null(void) {
    char buf[256];
    int ok = !bucc_manifest_validate(NULL, buf, sizeof(buf));
    return ok;
}

static int test_manifest_has_capability_null(void) {
    int ok = !bucc_manifest_has_capability(NULL, "test");
    return ok;
}

static int test_manifest_get_module_path_null(void) {
    char* path = bucc_manifest_get_module_path(NULL);
    return path == NULL;
}

int main(void) {
    printf("=== Package/Manifest Tests ===\n\n");
    
    TEST(pkg_error_strings);
    TEST(manifest_free_null);
    TEST(manifest_validate_null);
    TEST(manifest_has_capability_null);
    TEST(manifest_get_module_path_null);
    
    printf("\n=== Results: %d/%d tests passed ===\n", tests_passed, tests_run);
    
    return tests_passed == tests_run ? 0 : 1;
}
