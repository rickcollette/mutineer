/*
 * package.c - Buccaneer door package management implementation
 *
 * Part of the Buccaneer language implementation for Mutineer BBS.
 */

#define _POSIX_C_SOURCE 200809L
#include "include/bucc_package.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <libgen.h>

typedef enum json_type {
    JSON_NULL,
    JSON_BOOL,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT
} json_type_t;

typedef struct json_value json_value_t;

typedef struct json_pair {
    char*           key;
    json_value_t*   value;
} json_pair_t;

struct json_value {
    json_type_t type;
    union {
        bool            b;
        double          num;
        char*           str;
        struct {
            json_value_t**  items;
            size_t          count;
        } array;
        struct {
            json_pair_t*    pairs;
            size_t          count;
        } object;
    } as;
};

static void json_free(json_value_t* v);

static void skip_ws(const char** p) {
    while (**p && isspace((unsigned char)**p)) (*p)++;
}

static char* parse_string(const char** p) {
    if (**p != '"') return NULL;
    (*p)++;
    
    size_t cap = 64;
    size_t len = 0;
    char* buf = malloc(cap);
    
    while (**p && **p != '"') {
        if (**p == '\\') {
            (*p)++;
            char c = **p;
            if (c == 'n') c = '\n';
            else if (c == 'r') c = '\r';
            else if (c == 't') c = '\t';
            else if (c == '\\') c = '\\';
            else if (c == '"') c = '"';
            
            if (len + 1 >= cap) {
                cap *= 2;
                buf = realloc(buf, cap);
            }
            buf[len++] = c;
            (*p)++;
        } else {
            if (len + 1 >= cap) {
                cap *= 2;
                buf = realloc(buf, cap);
            }
            buf[len++] = **p;
            (*p)++;
        }
    }
    
    if (**p == '"') (*p)++;
    buf[len] = '\0';
    return buf;
}

static json_value_t* parse_value(const char** p);

static json_value_t* parse_array(const char** p) {
    if (**p != '[') return NULL;
    (*p)++;
    
    json_value_t* arr = calloc(1, sizeof(json_value_t));
    arr->type = JSON_ARRAY;
    
    size_t cap = 8;
    arr->as.array.items = malloc(cap * sizeof(json_value_t*));
    arr->as.array.count = 0;
    
    skip_ws(p);
    while (**p && **p != ']') {
        json_value_t* item = parse_value(p);
        if (!item) {
            json_free(arr);
            return NULL;
        }
        
        if (arr->as.array.count >= cap) {
            cap *= 2;
            arr->as.array.items = realloc(arr->as.array.items, 
                                          cap * sizeof(json_value_t*));
        }
        arr->as.array.items[arr->as.array.count++] = item;
        
        skip_ws(p);
        if (**p == ',') {
            (*p)++;
            skip_ws(p);
        }
    }
    
    if (**p == ']') (*p)++;
    return arr;
}

static json_value_t* parse_object(const char** p) {
    if (**p != '{') return NULL;
    (*p)++;
    
    json_value_t* obj = calloc(1, sizeof(json_value_t));
    obj->type = JSON_OBJECT;
    
    size_t cap = 8;
    obj->as.object.pairs = malloc(cap * sizeof(json_pair_t));
    obj->as.object.count = 0;
    
    skip_ws(p);
    while (**p && **p != '}') {
        char* key = parse_string(p);
        if (!key) {
            json_free(obj);
            return NULL;
        }
        
        skip_ws(p);
        if (**p != ':') {
            free(key);
            json_free(obj);
            return NULL;
        }
        (*p)++;
        skip_ws(p);
        
        json_value_t* value = parse_value(p);
        if (!value) {
            free(key);
            json_free(obj);
            return NULL;
        }
        
        if (obj->as.object.count >= cap) {
            cap *= 2;
            obj->as.object.pairs = realloc(obj->as.object.pairs,
                                           cap * sizeof(json_pair_t));
        }
        obj->as.object.pairs[obj->as.object.count].key = key;
        obj->as.object.pairs[obj->as.object.count].value = value;
        obj->as.object.count++;
        
        skip_ws(p);
        if (**p == ',') {
            (*p)++;
            skip_ws(p);
        }
    }
    
    if (**p == '}') (*p)++;
    return obj;
}

static json_value_t* parse_value(const char** p) {
    skip_ws(p);
    
    if (**p == '"') {
        json_value_t* v = calloc(1, sizeof(json_value_t));
        v->type = JSON_STRING;
        v->as.str = parse_string(p);
        return v;
    }
    
    if (**p == '[') {
        return parse_array(p);
    }
    
    if (**p == '{') {
        return parse_object(p);
    }
    
    if (strncmp(*p, "true", 4) == 0) {
        *p += 4;
        json_value_t* v = calloc(1, sizeof(json_value_t));
        v->type = JSON_BOOL;
        v->as.b = true;
        return v;
    }
    
    if (strncmp(*p, "false", 5) == 0) {
        *p += 5;
        json_value_t* v = calloc(1, sizeof(json_value_t));
        v->type = JSON_BOOL;
        v->as.b = false;
        return v;
    }
    
    if (strncmp(*p, "null", 4) == 0) {
        *p += 4;
        json_value_t* v = calloc(1, sizeof(json_value_t));
        v->type = JSON_NULL;
        return v;
    }
    
    if (**p == '-' || isdigit((unsigned char)**p)) {
        char* end;
        double num = strtod(*p, &end);
        if (end != *p) {
            *p = end;
            json_value_t* v = calloc(1, sizeof(json_value_t));
            v->type = JSON_NUMBER;
            v->as.num = num;
            return v;
        }
    }
    
    return NULL;
}

static void json_free(json_value_t* v) {
    if (!v) return;
    
    switch (v->type) {
        case JSON_STRING:
            free(v->as.str);
            break;
        case JSON_ARRAY:
            for (size_t i = 0; i < v->as.array.count; i++) {
                json_free(v->as.array.items[i]);
            }
            free(v->as.array.items);
            break;
        case JSON_OBJECT:
            for (size_t i = 0; i < v->as.object.count; i++) {
                free(v->as.object.pairs[i].key);
                json_free(v->as.object.pairs[i].value);
            }
            free(v->as.object.pairs);
            break;
        default:
            break;
    }
    
    free(v);
}

static json_value_t* json_get(json_value_t* obj, const char* key) {
    if (!obj || obj->type != JSON_OBJECT) return NULL;
    
    for (size_t i = 0; i < obj->as.object.count; i++) {
        if (strcmp(obj->as.object.pairs[i].key, key) == 0) {
            return obj->as.object.pairs[i].value;
        }
    }
    return NULL;
}

static const char* json_get_string(json_value_t* obj, const char* key) {
    json_value_t* v = json_get(obj, key);
    if (v && v->type == JSON_STRING) return v->as.str;
    return NULL;
}

static double json_get_number(json_value_t* obj, const char* key, double def) {
    json_value_t* v = json_get(obj, key);
    if (v && v->type == JSON_NUMBER) return v->as.num;
    return def;
}

static bool json_get_bool(json_value_t* obj, const char* key, bool def) {
    json_value_t* v = json_get(obj, key);
    if (v && v->type == JSON_BOOL) return v->as.b;
    return def;
}

const char* bucc_pkg_error_string(bucc_pkg_error_t err) {
    switch (err) {
        case PKG_OK: return "OK";
        case PKG_FILE_NOT_FOUND: return "File not found";
        case PKG_PARSE_ERROR: return "JSON parse error";
        case PKG_MISSING_FIELD: return "Missing required field";
        case PKG_INVALID_FIELD: return "Invalid field value";
        case PKG_MODULE_NOT_FOUND: return "Entry module not found";
        case PKG_VERSION_MISMATCH: return "Version mismatch";
        default: return "Unknown error";
    }
}

bucc_door_manifest_t* bucc_manifest_load(const char* path, bucc_pkg_error_t* err) {
    FILE* f = fopen(path, "r");
    if (!f) {
        if (err) *err = PKG_FILE_NOT_FOUND;
        return NULL;
    }
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char* content = malloc(size + 1);
    size_t read = fread(content, 1, size, f);
    content[read] = '\0';
    fclose(f);
    
    const char* p = content;
    json_value_t* root = parse_value(&p);
    
    if (!root || root->type != JSON_OBJECT) {
        free(content);
        if (root) json_free(root);
        if (err) *err = PKG_PARSE_ERROR;
        return NULL;
    }
    
    bucc_door_manifest_t* manifest = calloc(1, sizeof(bucc_door_manifest_t));
    
    char* path_copy = strdup(path);
    manifest->base_path = strdup(dirname(path_copy));
    free(path_copy);
    
    const char* name = json_get_string(root, "name");
    if (!name) {
        json_free(root);
        free(content);
        bucc_manifest_free(manifest);
        if (err) *err = PKG_MISSING_FIELD;
        return NULL;
    }
    manifest->name = strdup(name);
    
    const char* version = json_get_string(root, "version");
    if (version) manifest->version = strdup(version);
    
    const char* author = json_get_string(root, "author");
    if (author) manifest->author = strdup(author);
    
    const char* description = json_get_string(root, "description");
    if (description) manifest->description = strdup(description);
    
    const char* license = json_get_string(root, "license");
    if (license) manifest->license = strdup(license);
    
    const char* homepage = json_get_string(root, "homepage");
    if (homepage) manifest->homepage = strdup(homepage);
    
    const char* entry = json_get_string(root, "entry");
    if (!entry) entry = "main.bc";
    manifest->entry_module = strdup(entry);
    
    const char* entry_sub = json_get_string(root, "entry_sub");
    if (entry_sub) manifest->entry_sub = strdup(entry_sub);
    
    manifest->min_security = (int)json_get_number(root, "min_security", 0);
    manifest->max_time_minutes = (int)json_get_number(root, "max_time", 0);
    manifest->allow_chain = json_get_bool(root, "allow_chain", true);
    
    json_value_t* caps = json_get(root, "capabilities");
    if (caps && caps->type == JSON_ARRAY) {
        manifest->capability_count = caps->as.array.count;
        manifest->capabilities = calloc(caps->as.array.count, 
                                        sizeof(bucc_pkg_capability_t));
        
        for (size_t i = 0; i < caps->as.array.count; i++) {
            json_value_t* cap = caps->as.array.items[i];
            if (cap->type == JSON_STRING) {
                manifest->capabilities[i].name = strdup(cap->as.str);
                manifest->capabilities[i].required = true;
            } else if (cap->type == JSON_OBJECT) {
                const char* cap_name = json_get_string(cap, "name");
                if (cap_name) {
                    manifest->capabilities[i].name = strdup(cap_name);
                    manifest->capabilities[i].required = 
                        json_get_bool(cap, "required", true);
                }
            }
        }
    }
    
    json_value_t* deps = json_get(root, "dependencies");
    if (deps && deps->type == JSON_OBJECT) {
        manifest->dependency_count = deps->as.object.count;
        manifest->dependencies = calloc(deps->as.object.count,
                                        sizeof(bucc_dependency_t));
        
        for (size_t i = 0; i < deps->as.object.count; i++) {
            manifest->dependencies[i].name = strdup(deps->as.object.pairs[i].key);
            json_value_t* ver = deps->as.object.pairs[i].value;
            if (ver->type == JSON_STRING) {
                manifest->dependencies[i].version_min = strdup(ver->as.str);
            }
        }
    }
    
    json_value_t* config = json_get(root, "config");
    if (config && config->type == JSON_OBJECT) {
        manifest->config = bucc_map_new(config->as.object.count);
        
        for (size_t i = 0; i < config->as.object.count; i++) {
            const char* key = config->as.object.pairs[i].key;
            json_value_t* val = config->as.object.pairs[i].value;
            
            bucc_value_t bval = BUCC_NULL_VAL;
            if (val->type == JSON_STRING) {
                bval = bucc_make_string(val->as.str);
            } else if (val->type == JSON_NUMBER) {
                if (val->as.num == (int64_t)val->as.num) {
                    bval = BUCC_I64_VAL((int64_t)val->as.num);
                } else {
                    bval = BUCC_F64_VAL(val->as.num);
                }
            } else if (val->type == JSON_BOOL) {
                bval = BUCC_BOOL_VAL(val->as.b);
            }
            
            bucc_map_set_cstr(manifest->config, key, bval);
        }
    }
    
    json_free(root);
    free(content);
    
    if (err) *err = PKG_OK;
    return manifest;
}

void bucc_manifest_free(bucc_door_manifest_t* manifest) {
    if (!manifest) return;
    
    free(manifest->name);
    free(manifest->version);
    free(manifest->author);
    free(manifest->description);
    free(manifest->license);
    free(manifest->homepage);
    free(manifest->entry_module);
    free(manifest->entry_sub);
    free(manifest->base_path);
    
    for (size_t i = 0; i < manifest->capability_count; i++) {
        free(manifest->capabilities[i].name);
    }
    free(manifest->capabilities);
    
    for (size_t i = 0; i < manifest->dependency_count; i++) {
        free(manifest->dependencies[i].name);
        free(manifest->dependencies[i].version_min);
        free(manifest->dependencies[i].version_max);
    }
    free(manifest->dependencies);
    
    if (manifest->config) {
        bucc_map_release(manifest->config);
    }
    
    free(manifest);
}

bool bucc_manifest_validate(bucc_door_manifest_t* manifest, 
                           char* error_buf, size_t error_size) {
    if (!manifest) {
        if (error_buf) snprintf(error_buf, error_size, "Manifest is NULL");
        return false;
    }
    
    if (!manifest->name || !*manifest->name) {
        if (error_buf) snprintf(error_buf, error_size, "Missing 'name' field");
        return false;
    }
    
    for (const char* p = manifest->name; *p; p++) {
        if (!isalnum((unsigned char)*p) && *p != '-' && *p != '_') {
            if (error_buf) {
                snprintf(error_buf, error_size, 
                        "Invalid character in name: '%c'", *p);
            }
            return false;
        }
    }
    
    if (manifest->version) {
        int major, minor, patch;
        if (sscanf(manifest->version, "%d.%d.%d", &major, &minor, &patch) < 2) {
            if (error_buf) {
                snprintf(error_buf, error_size, 
                        "Invalid version format: '%s' (expected X.Y or X.Y.Z)",
                        manifest->version);
            }
            return false;
        }
    }
    
    char module_path[512];
    snprintf(module_path, sizeof(module_path), "%s/%s", 
             manifest->base_path, manifest->entry_module);
    
    FILE* f = fopen(module_path, "r");
    if (!f) {
        if (error_buf) {
            snprintf(error_buf, error_size, 
                    "Entry module not found: %s", module_path);
        }
        return false;
    }
    fclose(f);
    
    return true;
}

bool bucc_manifest_has_capability(bucc_door_manifest_t* manifest, const char* cap) {
    if (!manifest || !cap) return false;
    
    for (size_t i = 0; i < manifest->capability_count; i++) {
        if (manifest->capabilities[i].name &&
            strcmp(manifest->capabilities[i].name, cap) == 0) {
            return true;
        }
    }
    return false;
}

char* bucc_manifest_get_module_path(bucc_door_manifest_t* manifest) {
    if (!manifest) return NULL;
    
    char* path = malloc(512);
    snprintf(path, 512, "%s/%s", manifest->base_path, manifest->entry_module);
    return path;
}
