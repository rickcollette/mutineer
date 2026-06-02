/*
 * bucc_semantic.h - Buccaneer semantic analysis
 *
 * Part of the Buccaneer language implementation for Mutineer BBS.
 * Performs name binding, type checking, control flow analysis,
 * and capability validation per BUCCANEER_SEMANTIC_ANALYSIS_SPEC.md.
 */

#ifndef BUCC_SEMANTIC_H
#define BUCC_SEMANTIC_H

#include "bucc_ast.h"
#include "bucc_diag.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum bucc_symbol_kind {
    SYM_GLOBAL,
    SYM_LOCAL,
    SYM_PARAM,
    SYM_PROCEDURE,
    SYM_FUNCTION,
    SYM_DATASET,
    SYM_BUILTIN,
    SYM_HOST_NS,
    SYM_HOST_FN
} bucc_symbol_kind_t;

typedef struct bucc_type bucc_type_t;

struct bucc_type {
    enum {
        TYPE_VOID,
        TYPE_INTEGER,
        TYPE_DOUBLE,
        TYPE_BOOLEAN,
        TYPE_STRING,
        TYPE_DATE,
        TYPE_DATETIME,
        TYPE_NULL,
        TYPE_ARRAY,
        TYPE_MAP,
        TYPE_ERROR,
        TYPE_ANY
    } kind;
    bucc_type_t* element_type;
};

typedef struct bucc_symbol {
    bucc_symbol_kind_t  kind;
    char*               name;
    bucc_type_t*        type;
    bucc_node_t*        decl_node;
    uint32_t            slot;
    uint32_t            flags;
    bool                is_used;
    bool                is_assigned;
    struct bucc_symbol* next;
} bucc_symbol_t;

#define SYM_FLAG_HANDLER    0x0001
#define SYM_FLAG_THROTTLED  0x0002
#define SYM_FLAG_READONLY   0x0004

typedef struct bucc_scope {
    bucc_symbol_t*      symbols;
    struct bucc_scope*  parent;
    uint32_t            local_count;
    bool                in_loop;
    bool                in_try;
    int                 loop_depth;
    enum {
        SCOPE_GLOBAL,
        SCOPE_PROCEDURE,
        SCOPE_FUNCTION,
        SCOPE_BLOCK
    } kind;
} bucc_scope_t;

typedef struct bucc_capability {
    char*                   name;
    bool                    declared;
    bool                    used;
    struct bucc_capability* next;
} bucc_capability_t;

typedef struct bucc_semantic {
    bucc_diag_t*        diag;
    bucc_node_t*        module;
    bucc_scope_t*       global_scope;
    bucc_scope_t*       current_scope;
    bucc_capability_t*  capabilities;
    
    char*               program_name;
    char*               program_version;
    
    bool                has_main;
    bool                in_function;
    bucc_type_t*        current_return_type;
    
    uint32_t            global_count;
    uint32_t            proc_count;
    
    bucc_symbol_t**     host_namespaces;
    uint32_t            host_ns_count;
} bucc_semantic_t;

bucc_semantic_t* bucc_semantic_new(bucc_diag_t* diag);
void bucc_semantic_free(bucc_semantic_t* sem);

bool bucc_semantic_analyze(bucc_semantic_t* sem, bucc_node_t* module);

bucc_scope_t* bucc_scope_new(bucc_scope_t* parent, int kind);
void bucc_scope_free(bucc_scope_t* scope);

bucc_symbol_t* bucc_scope_define(bucc_scope_t* scope, const char* name,
                                 bucc_symbol_kind_t kind, bucc_type_t* type,
                                 bucc_node_t* decl);
bucc_symbol_t* bucc_scope_lookup(bucc_scope_t* scope, const char* name);
bucc_symbol_t* bucc_scope_lookup_local(bucc_scope_t* scope, const char* name);

bucc_type_t* bucc_type_new(int kind);
bucc_type_t* bucc_type_array(bucc_type_t* element);
bucc_type_t* bucc_type_map(bucc_type_t* value);
bucc_type_t* bucc_type_copy(bucc_type_t* type);
void bucc_type_free(bucc_type_t* type);
bool bucc_type_equal(bucc_type_t* a, bucc_type_t* b);
bool bucc_type_assignable(bucc_type_t* target, bucc_type_t* source);
bool bucc_type_is_numeric(bucc_type_t* type);
const char* bucc_type_name(bucc_type_t* type);

bucc_type_t* bucc_type_from_ast(bucc_node_t* type_node);

void bucc_semantic_register_host_api(bucc_semantic_t* sem);

bool bucc_semantic_declare_capability(bucc_semantic_t* sem, const char* cap);
bool bucc_semantic_check_capability(bucc_semantic_t* sem, const char* cap,
                                    bucc_source_span_t span);

bucc_symbol_t* bucc_semantic_resolve_host_call(bucc_semantic_t* sem,
                                               const char* ns, const char* fn);

#ifdef __cplusplus
}
#endif

#endif /* BUCC_SEMANTIC_H */
