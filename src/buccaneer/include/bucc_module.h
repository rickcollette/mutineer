/*
 * bucc_module.h - Buccaneer module format
 *
 * Part of the Buccaneer language implementation for Mutineer BBS.
 * Defines the .bc module file format per BUCCANEER_BYTECODE_VM_SPEC.md.
 */

#ifndef BUCC_MODULE_H
#define BUCC_MODULE_H

#include "bucc_value.h"
#include "bucc_diag.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BUCC_MAGIC          "BUCC"
#define BUCC_FORMAT_MAJOR   1
#define BUCC_FORMAT_MINOR   0

#define BUCC_FLAG_DEBUG     0x0001
#define BUCC_FLAG_SIGNED    0x0002

#pragma pack(push, 1)
typedef struct bucc_module_header {
    uint8_t  magic[4];
    uint16_t format_major;
    uint16_t format_minor;
    uint32_t flags;
    uint32_t header_size;
    uint32_t metadata_offset;
    uint32_t string_pool_offset;
    uint32_t const_pool_offset;
    uint32_t symbol_table_offset;
    uint32_t proc_table_offset;
    uint32_t dataset_table_offset;
    uint32_t import_table_offset;
    uint32_t bytecode_offset;
    uint32_t debug_map_offset;
    uint32_t file_length;
    uint32_t crc32;
} bucc_module_header_t;
#pragma pack(pop)

typedef struct bucc_module_proc {
    char*    name;
    uint32_t id;
    uint32_t code_offset;
    uint32_t code_len;
    uint32_t param_count;
    uint32_t local_count;
    bool     is_function;
} bucc_module_proc_t;

typedef struct bucc_module_import {
    char*    ns;
    char*    fn;
    uint32_t id;
    char*    capability;
} bucc_module_import_t;

typedef struct bucc_debug_line_entry {
    uint32_t bytecode_offset;
    uint32_t source_line;
    uint32_t source_column;
} bucc_debug_line_entry_t;

typedef struct bucc_debug_map {
    char*    source_file;
    bucc_debug_line_entry_t* entries;
    uint32_t entry_count;
} bucc_debug_map_t;

typedef struct bucc_module {
    bucc_module_header_t header;
    
    char*    program_name;
    char*    program_version;
    
    char**   strings;
    uint32_t string_count;
    
    bucc_value_t* constants;
    uint32_t const_count;
    
    bucc_module_proc_t* procedures;
    uint32_t proc_count;
    
    bucc_module_import_t* imports;
    uint32_t import_count;
    
    uint8_t* bytecode;
    uint32_t bytecode_len;
    
    bucc_debug_map_t* debug_map;
    
    uint32_t refcount;
} bucc_module_t;

typedef struct bucc_load_options {
    bool require_signature;
    bool enable_debug_map;
    bool verify_capabilities;
} bucc_load_options_t;

bucc_module_t* bucc_module_load_file(const char* path,
                                     const bucc_load_options_t* opts,
                                     bucc_diag_t* diag);

bucc_module_t* bucc_module_load_memory(const uint8_t* data, size_t len,
                                       const bucc_load_options_t* opts,
                                       bucc_diag_t* diag);

bool bucc_module_save_file(bucc_module_t* mod, const char* path,
                           bucc_diag_t* diag);

void bucc_module_retain(bucc_module_t* mod);
void bucc_module_release(bucc_module_t* mod);

bucc_module_t* bucc_module_load(const char* path);
bool bucc_module_save(bucc_module_t* mod, const char* path);
void bucc_module_free(bucc_module_t* mod);
void bucc_module_disasm(bucc_module_t* mod, FILE* out);

bucc_module_proc_t* bucc_module_find_proc(bucc_module_t* mod, const char* name);
bucc_module_proc_t* bucc_module_get_main(bucc_module_t* mod);

const char* bucc_module_get_string(const bucc_module_t* mod, uint32_t index);
bucc_value_t bucc_module_get_constant(const bucc_module_t* mod, uint32_t index);

bool bucc_module_verify(bucc_module_t* mod, bucc_diag_t* diag);

void bucc_module_disassemble(bucc_module_t* mod, FILE* out);

bucc_debug_map_t* bucc_debug_map_new(const char* source_file);
void bucc_debug_map_free(bucc_debug_map_t* map);
void bucc_debug_map_add_entry(bucc_debug_map_t* map, uint32_t offset, uint32_t line, uint32_t col);
bool bucc_debug_map_save(bucc_debug_map_t* map, const char* path);
bucc_debug_map_t* bucc_debug_map_load(const char* path);
int bucc_debug_map_get_line(bucc_debug_map_t* map, uint32_t offset);

#ifdef __cplusplus
}
#endif

#endif /* BUCC_MODULE_H */
