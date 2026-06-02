/*
 * bucc_emit.h - Buccaneer bytecode emitter
 *
 * Part of the Buccaneer language implementation for Mutineer BBS.
 * Emits bytecode from analyzed AST per BUCCANEER_BYTECODE_VM_SPEC.md.
 */

#ifndef BUCC_EMIT_H
#define BUCC_EMIT_H

#include "bucc_ast.h"
#include "bucc_semantic.h"
#include "bucc_module.h"
#include "bucc_diag.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum bucc_opcode {
    OP_NOP          = 0x00,
    OP_HALT         = 0x01,
    OP_PUSH_NULL    = 0x02,
    OP_PUSH_TRUE    = 0x03,
    OP_PUSH_FALSE   = 0x04,
    OP_PUSH_I64     = 0x05,
    OP_PUSH_F64     = 0x06,
    OP_PUSH_STR     = 0x07,
    OP_PUSH_DATE    = 0x08,
    OP_PUSH_DATETIME= 0x09,
    OP_POP          = 0x0A,
    OP_DUP          = 0x0B,
    OP_SWAP         = 0x0C,
    
    OP_LOAD_GLOBAL  = 0x10,
    OP_STORE_GLOBAL = 0x11,
    OP_LOAD_LOCAL   = 0x12,
    OP_STORE_LOCAL  = 0x13,
    OP_LOAD_ARG     = 0x14,
    
    OP_ADD          = 0x20,
    OP_SUB          = 0x21,
    OP_MUL          = 0x22,
    OP_DIV          = 0x23,
    OP_MOD          = 0x24,
    OP_NEG          = 0x25,
    OP_EQ           = 0x26,
    OP_NE           = 0x27,
    OP_LT           = 0x28,
    OP_LE           = 0x29,
    OP_GT           = 0x2A,
    OP_GE           = 0x2B,
    OP_AND          = 0x2C,
    OP_OR           = 0x2D,
    OP_NOT          = 0x2E,
    OP_CONCAT       = 0x2F,
    
    OP_JMP          = 0x30,
    OP_JMP_FALSE    = 0x31,
    OP_JMP_TRUE     = 0x32,
    OP_CALL         = 0x33,
    OP_CALL_HOST    = 0x34,
    OP_RETURN       = 0x35,
    OP_RETURN_VALUE = 0x36,
    OP_CHAIN        = 0x37,
    OP_YIELD        = 0x38,
    
    OP_ARRAY_NEW    = 0x40,
    OP_ARRAY_GET    = 0x41,
    OP_ARRAY_SET    = 0x42,
    OP_ARRAY_PUSH   = 0x43,
    OP_ARRAY_POP    = 0x44,
    OP_MAP_NEW      = 0x45,
    OP_MAP_GET      = 0x46,
    OP_MAP_SET      = 0x47,
    OP_MAP_HAS      = 0x48,
    OP_MAP_KEYS     = 0x49,
    
    OP_DISPATCH_CALL= 0x50,
    OP_RANGE_TEST   = 0x51,
    
    OP_TRY_BEGIN    = 0x60,
    OP_TRY_END      = 0x61,
    OP_THROW        = 0x62,
    OP_RE_THROW     = 0x63,
    
    OP_CAST_I64     = 0x70,
    OP_CAST_F64     = 0x71,
    OP_CAST_STRING  = 0x72,
    OP_CAST_BOOL    = 0x73,
    OP_CAST_DATE    = 0x74,
    OP_CAST_DATETIME= 0x75,
    OP_TYPEOF       = 0x76,
    
    OP_DEBUG_LINE   = 0x7E,
    OP_PROF_TICK    = 0x7F
} bucc_opcode_t;

typedef struct bucc_const_entry {
    bucc_value_t value;
    uint32_t     index;
    struct bucc_const_entry* next;
} bucc_const_entry_t;

typedef struct bucc_string_entry {
    char*    str;
    uint32_t index;
    struct bucc_string_entry* next;
} bucc_string_entry_t;

typedef struct bucc_proc_entry {
    char*    name;
    uint32_t id;
    uint32_t code_offset;
    uint32_t code_len;
    uint32_t param_count;
    uint32_t local_count;
    bool     is_function;
    struct bucc_proc_entry* next;
} bucc_proc_entry_t;

typedef struct bucc_host_import {
    char*    ns;
    char*    fn;
    uint32_t id;
    struct bucc_host_import* next;
} bucc_host_import_t;

typedef struct bucc_label {
    uint32_t offset;
    struct bucc_label* next;
} bucc_label_t;

typedef struct bucc_patch {
    uint32_t offset;
    uint32_t label_id;
    struct bucc_patch* next;
} bucc_patch_t;

typedef struct bucc_debug_line_info {
    uint32_t bytecode_offset;
    uint32_t source_line;
    uint32_t source_column;
    struct bucc_debug_line_info* next;
} bucc_debug_line_info_t;

typedef struct bucc_emitter {
    bucc_diag_t*        diag;
    bucc_semantic_t*    sem;
    
    uint8_t*            code;
    size_t              code_len;
    size_t              code_cap;
    
    bucc_const_entry_t* constants;
    uint32_t            const_count;
    
    bucc_string_entry_t* strings;
    uint32_t            string_count;
    
    bucc_proc_entry_t*  procedures;
    uint32_t            proc_count;
    
    bucc_host_import_t* imports;
    uint32_t            import_count;
    
    bucc_proc_entry_t*  current_proc;
    uint32_t            current_local_count;
    
    uint32_t            next_label;
    bucc_label_t**      labels;
    uint32_t            label_cap;
    bucc_patch_t*       patches;
    
    uint32_t            loop_start_label;
    uint32_t            loop_end_label;
    
    bucc_debug_line_info_t* debug_lines;
    uint32_t            debug_line_count;
    uint32_t            last_debug_line;
    bool                emit_debug_info;
    const char*         source_file;
} bucc_emitter_t;

bucc_emitter_t* bucc_emitter_new(bucc_semantic_t* sem, bucc_diag_t* diag);
void bucc_emitter_free(bucc_emitter_t* emit);

bool bucc_emit_module(bucc_emitter_t* emit, bucc_node_t* module);
bucc_module_t* bucc_emitter_to_module(bucc_emitter_t* emit);

void bucc_emit_byte(bucc_emitter_t* emit, uint8_t byte);
void bucc_emit_u16(bucc_emitter_t* emit, uint16_t val);
void bucc_emit_u32(bucc_emitter_t* emit, uint32_t val);
void bucc_emit_i32(bucc_emitter_t* emit, int32_t val);

void bucc_emit_op(bucc_emitter_t* emit, bucc_opcode_t op);
void bucc_emit_op_u8(bucc_emitter_t* emit, bucc_opcode_t op, uint8_t arg);
void bucc_emit_op_u16(bucc_emitter_t* emit, bucc_opcode_t op, uint16_t arg);
void bucc_emit_op_u32(bucc_emitter_t* emit, bucc_opcode_t op, uint32_t arg);

uint32_t bucc_emit_string(bucc_emitter_t* emit, const char* str);
uint32_t bucc_emit_constant_i64(bucc_emitter_t* emit, int64_t val);
uint32_t bucc_emit_constant_f64(bucc_emitter_t* emit, double val);

uint32_t bucc_emit_label(bucc_emitter_t* emit);
void bucc_emit_label_here(bucc_emitter_t* emit, uint32_t label);
void bucc_emit_jump(bucc_emitter_t* emit, bucc_opcode_t op, uint32_t label);
void bucc_emit_resolve_patches(bucc_emitter_t* emit);

uint32_t bucc_emit_host_import(bucc_emitter_t* emit, const char* ns, const char* fn);

void bucc_emit_debug_line(bucc_emitter_t* emit, uint32_t line, uint32_t col);
void bucc_emitter_set_debug(bucc_emitter_t* emit, bool enable, const char* source_file);
bucc_debug_map_t* bucc_emitter_get_debug_map(bucc_emitter_t* emit);

const char* bucc_opcode_name(bucc_opcode_t op);

#ifdef __cplusplus
}
#endif

#endif /* BUCC_EMIT_H */
