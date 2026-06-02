/*
 * bucc_package.h - Buccaneer door package management
 *
 * Part of the Buccaneer language implementation for Mutineer BBS.
 * Handles door.json validation and package loading.
 */

#ifndef BUCC_PACKAGE_H
#define BUCC_PACKAGE_H

#include "bucc_value.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum bucc_pkg_error {
    PKG_OK = 0,
    PKG_FILE_NOT_FOUND,
    PKG_PARSE_ERROR,
    PKG_MISSING_FIELD,
    PKG_INVALID_FIELD,
    PKG_MODULE_NOT_FOUND,
    PKG_VERSION_MISMATCH
} bucc_pkg_error_t;

typedef struct bucc_pkg_capability {
    char*   name;
    bool    required;
} bucc_pkg_capability_t;

typedef struct bucc_dependency {
    char*   name;
    char*   version_min;
    char*   version_max;
} bucc_dependency_t;

typedef struct bucc_door_manifest {
    char*   name;
    char*   version;
    char*   author;
    char*   description;
    char*   license;
    char*   homepage;
    
    char*   entry_module;
    char*   entry_sub;
    
    bucc_pkg_capability_t*  capabilities;
    size_t                  capability_count;
    
    bucc_dependency_t*  dependencies;
    size_t              dependency_count;
    
    int     min_security;
    int     max_time_minutes;
    bool    allow_chain;
    
    bucc_map_t* config;
    
    char*   base_path;
} bucc_door_manifest_t;

bucc_door_manifest_t* bucc_manifest_load(const char* path, bucc_pkg_error_t* err);
void bucc_manifest_free(bucc_door_manifest_t* manifest);

bool bucc_manifest_validate(bucc_door_manifest_t* manifest, char* error_buf, size_t error_size);

const char* bucc_pkg_error_string(bucc_pkg_error_t err);

bool bucc_manifest_has_capability(bucc_door_manifest_t* manifest, const char* cap);

char* bucc_manifest_get_module_path(bucc_door_manifest_t* manifest);

#ifdef __cplusplus
}
#endif

#endif /* BUCC_PACKAGE_H */
