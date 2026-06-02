/*
 * semantic.c - Buccaneer semantic analysis implementation
 *
 * Part of the Buccaneer language implementation for Mutineer BBS.
 */

#define _POSIX_C_SOURCE 200809L
#include "include/bucc_semantic.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static int strcasecmp_local(const char* a, const char* b) {
    while (*a && *b) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb) return ca - cb;
        a++;
        b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

bucc_type_t* bucc_type_new(int kind) {
    bucc_type_t* t = calloc(1, sizeof(bucc_type_t));
    if (t) t->kind = kind;
    return t;
}

bucc_type_t* bucc_type_array(bucc_type_t* element) {
    bucc_type_t* t = bucc_type_new(TYPE_ARRAY);
    if (t) t->element_type = element;
    return t;
}

bucc_type_t* bucc_type_map(bucc_type_t* value) {
    bucc_type_t* t = bucc_type_new(TYPE_MAP);
    if (t) t->element_type = value;
    return t;
}

bucc_type_t* bucc_type_copy(bucc_type_t* type) {
    if (!type) return NULL;
    bucc_type_t* t = bucc_type_new(type->kind);
    if (t && type->element_type) {
        t->element_type = bucc_type_copy(type->element_type);
    }
    return t;
}

void bucc_type_free(bucc_type_t* type) {
    if (!type) return;
    bucc_type_free(type->element_type);
    free(type);
}

bool bucc_type_equal(bucc_type_t* a, bucc_type_t* b) {
    if (!a && !b) return true;
    if (!a || !b) return false;
    if (a->kind != b->kind) return false;
    if (a->kind == TYPE_ARRAY || a->kind == TYPE_MAP) {
        return bucc_type_equal(a->element_type, b->element_type);
    }
    return true;
}

bool bucc_type_assignable(bucc_type_t* target, bucc_type_t* source) {
    if (!target || !source) return false;
    if (target->kind == TYPE_ANY) return true;
    if (source->kind == TYPE_ANY) return true;
    if (source->kind == TYPE_NULL) return true;
    if (bucc_type_equal(target, source)) return true;
    
    if (target->kind == TYPE_DOUBLE && source->kind == TYPE_INTEGER) {
        return true;
    }
    
    return false;
}

bool bucc_type_is_numeric(bucc_type_t* type) {
    if (!type) return false;
    return type->kind == TYPE_INTEGER || type->kind == TYPE_DOUBLE;
}

const char* bucc_type_name(bucc_type_t* type) {
    if (!type) return "UNKNOWN";
    switch (type->kind) {
        case TYPE_VOID:     return "VOID";
        case TYPE_INTEGER:  return "INTEGER";
        case TYPE_DOUBLE:   return "DOUBLE";
        case TYPE_BOOLEAN:  return "BOOLEAN";
        case TYPE_STRING:   return "STRING";
        case TYPE_DATE:     return "DATE";
        case TYPE_DATETIME: return "DATETIME";
        case TYPE_NULL:     return "NULL";
        case TYPE_ARRAY:    return "ARRAY";
        case TYPE_MAP:      return "MAP";
        case TYPE_ERROR:    return "ERROR";
        case TYPE_ANY:      return "ANY";
        default:            return "UNKNOWN";
    }
}

bucc_type_t* bucc_type_from_ast(bucc_node_t* type_node) {
    if (!type_node) return NULL;
    
    switch (type_node->kind) {
        case NODE_TYPE_SCALAR:
            switch (type_node->data.type_scalar.scalar_type) {
                case SCALAR_INTEGER:  return bucc_type_new(TYPE_INTEGER);
                case SCALAR_DOUBLE:   return bucc_type_new(TYPE_DOUBLE);
                case SCALAR_BOOLEAN:  return bucc_type_new(TYPE_BOOLEAN);
                case SCALAR_STRING:   return bucc_type_new(TYPE_STRING);
                case SCALAR_DATE:     return bucc_type_new(TYPE_DATE);
                case SCALAR_DATETIME: return bucc_type_new(TYPE_DATETIME);
                default:              return bucc_type_new(TYPE_ERROR);
            }
        case NODE_TYPE_ARRAY:
            return bucc_type_array(bucc_type_from_ast(type_node->data.type_array.element_type));
        case NODE_TYPE_MAP:
            return bucc_type_map(bucc_type_from_ast(type_node->data.type_map.value_type));
        default:
            return bucc_type_new(TYPE_ERROR);
    }
}

bucc_scope_t* bucc_scope_new(bucc_scope_t* parent, int kind) {
    bucc_scope_t* scope = calloc(1, sizeof(bucc_scope_t));
    if (!scope) return NULL;
    scope->parent = parent;
    scope->kind = kind;
    if (parent) {
        scope->in_loop = parent->in_loop;
        scope->in_try = parent->in_try;
        scope->loop_depth = parent->loop_depth;
    }
    return scope;
}

void bucc_scope_free(bucc_scope_t* scope) {
    if (!scope) return;
    
    bucc_symbol_t* sym = scope->symbols;
    while (sym) {
        bucc_symbol_t* next = sym->next;
        free(sym->name);
        bucc_type_free(sym->type);
        free(sym);
        sym = next;
    }
    free(scope);
}

bucc_symbol_t* bucc_scope_define(bucc_scope_t* scope, const char* name,
                                 bucc_symbol_kind_t kind, bucc_type_t* type,
                                 bucc_node_t* decl) {
    if (!scope || !name) return NULL;
    
    bucc_symbol_t* sym = calloc(1, sizeof(bucc_symbol_t));
    if (!sym) return NULL;
    
    sym->name = strdup(name);
    sym->kind = kind;
    sym->type = type;
    sym->decl_node = decl;
    sym->slot = scope->local_count++;
    sym->next = scope->symbols;
    scope->symbols = sym;
    
    return sym;
}

bucc_symbol_t* bucc_scope_lookup_local(bucc_scope_t* scope, const char* name) {
    if (!scope || !name) return NULL;
    
    for (bucc_symbol_t* sym = scope->symbols; sym; sym = sym->next) {
        if (strcasecmp_local(sym->name, name) == 0) {
            return sym;
        }
    }
    return NULL;
}

bucc_symbol_t* bucc_scope_lookup(bucc_scope_t* scope, const char* name) {
    while (scope) {
        bucc_symbol_t* sym = bucc_scope_lookup_local(scope, name);
        if (sym) return sym;
        scope = scope->parent;
    }
    return NULL;
}

bucc_semantic_t* bucc_semantic_new(bucc_diag_t* diag) {
    bucc_semantic_t* sem = calloc(1, sizeof(bucc_semantic_t));
    if (!sem) return NULL;
    
    sem->diag = diag;
    sem->global_scope = bucc_scope_new(NULL, SCOPE_GLOBAL);
    sem->current_scope = sem->global_scope;
    
    bucc_semantic_register_host_api(sem);
    
    return sem;
}

void bucc_semantic_free(bucc_semantic_t* sem) {
    if (!sem) return;
    
    bucc_scope_free(sem->global_scope);
    
    bucc_capability_t* cap = sem->capabilities;
    while (cap) {
        bucc_capability_t* next = cap->next;
        free(cap->name);
        free(cap);
        cap = next;
    }
    
    for (uint32_t i = 0; i < sem->host_ns_count; i++) {
        bucc_symbol_t* sym = sem->host_namespaces[i];
        while (sym) {
            bucc_symbol_t* next = sym->next;
            free(sym->name);
            bucc_type_free(sym->type);
            free(sym);
            sym = next;
        }
    }
    free(sem->host_namespaces);
    
    free(sem->program_name);
    free(sem->program_version);
    free(sem);
}

bool bucc_semantic_declare_capability(bucc_semantic_t* sem, const char* cap) {
    if (!sem || !cap) return false;
    
    for (bucc_capability_t* c = sem->capabilities; c; c = c->next) {
        if (strcasecmp_local(c->name, cap) == 0) {
            c->declared = true;
            return true;
        }
    }
    
    bucc_capability_t* c = calloc(1, sizeof(bucc_capability_t));
    if (!c) return false;
    c->name = strdup(cap);
    c->declared = true;
    c->next = sem->capabilities;
    sem->capabilities = c;
    return true;
}

bool bucc_semantic_check_capability(bucc_semantic_t* sem, const char* cap,
                                    bucc_source_span_t span) {
    if (!sem || !cap) return false;
    
    for (bucc_capability_t* c = sem->capabilities; c; c = c->next) {
        if (strcasecmp_local(c->name, cap) == 0) {
            c->used = true;
            if (!c->declared) {
                bucc_diag_error(sem->diag, span, BUCC_ERR_CAPABILITY,
                               "capability '%s' not declared", cap);
                return false;
            }
            return true;
        }
    }
    
    bucc_diag_error(sem->diag, span, BUCC_ERR_CAPABILITY,
                   "capability '%s' not declared", cap);
    return false;
}

typedef struct {
    const char* name;
    const char* capability;
    int         return_type;
    int         param_count;
} host_fn_def_t;

typedef struct {
    const char*       name;
    const host_fn_def_t* functions;
    int               fn_count;
} host_ns_def_t;

static const host_fn_def_t term_functions[] = {
    {"PRINT",         "term.io", TYPE_VOID, -1},
    {"PRINTLN",       "term.io", TYPE_VOID, -1},
    {"CLS",           "term.io", TYPE_VOID, 0},
    {"COLOR",         "term.io", TYPE_VOID, -1},
    {"PRINTAT",       "term.io", TYPE_VOID, 3},
    {"GETKEY",        "term.io", TYPE_STRING, 0},
    {"INPUT",         "term.io", TYPE_STRING, -1},
    {"INPUT_PASSWORD","term.io", TYPE_STRING, 1},
    {"PAUSE",         "term.io", TYPE_VOID, -1},
    {"WIDTH",         "term.io", TYPE_INTEGER, 0},
    {"HEIGHT",        "term.io", TYPE_INTEGER, 0},
    {"SUPPORTS_ANSI", "term.io", TYPE_BOOLEAN, 0},
    {"BOX",           "term.io", TYPE_VOID, 4},
    {"SAVE_ATTR",     "term.io", TYPE_VOID, 0},
    {"RESTORE_ATTR",  "term.io", TYPE_VOID, 0},
    {"PRINT_PAGED",   "term.io", TYPE_VOID, 1},
    {"PAGER_BEGIN",   "term.io", TYPE_VOID, 0},
    {"PAGER_LINE",    "term.io", TYPE_BOOLEAN, 1},
    {"PAGER_END",     "term.io", TYPE_VOID, 0},
};

static const host_fn_def_t user_functions[] = {
    {"NAME",          "user.read", TYPE_STRING, 0},
    {"ALIAS",         "user.read", TYPE_STRING, 0},
    {"ID",            "user.read", TYPE_INTEGER, 0},
    {"SECURITY",      "user.read", TYPE_INTEGER, 0},
    {"TIME_LEFT",     "user.read", TYPE_INTEGER, 0},
    {"FLAGS",         "user.read", TYPE_INTEGER, 0},
};

static const host_fn_def_t users_functions[] = {
    {"FIND_BY_HANDLE","users.read", TYPE_MAP, 1},
    {"FIND_BY_ID",    "users.read", TYPE_MAP, 1},
    {"COUNT",         "users.read", TYPE_INTEGER, 0},
};

static const host_fn_def_t data_functions[] = {
    {"INSERT",        "data.write", TYPE_INTEGER, 2},
    {"UPDATE",        "data.write", TYPE_INTEGER, 3},
    {"DELETE",        "data.write", TYPE_INTEGER, 2},
    {"GET",           "data.read", TYPE_MAP, 2},
    {"FIND",          "data.read", TYPE_ARRAY, -1},
    {"FIND_PAGED",    "data.read", TYPE_ARRAY, -1},
    {"COUNT",         "data.read", TYPE_INTEGER, -1},
    {"SUM",           "data.read", TYPE_DOUBLE, -1},
    {"MIN",           "data.read", TYPE_ANY, -1},
    {"MAX",           "data.read", TYPE_ANY, -1},
    {"BEGIN",         "data.write", TYPE_VOID, 0},
    {"COMMIT",        "data.write", TYPE_VOID, 0},
    {"ROLLBACK",      "data.write", TYPE_VOID, 0},
};

static const host_fn_def_t kv_functions[] = {
    {"GET",           "kv.read", TYPE_STRING, 2},
    {"SET",           "kv.write", TYPE_VOID, 2},
    {"DELETE",        "kv.write", TYPE_VOID, 1},
    {"EXISTS",        "kv.read", TYPE_BOOLEAN, 1},
};

static const host_fn_def_t app_functions[] = {
    {"GET",           "app.state", TYPE_ANY, 2},
    {"SET",           "app.state", TYPE_VOID, 2},
};

static const host_fn_def_t shared_functions[] = {
    {"GET",           "shared.read", TYPE_ANY, 2},
    {"SET",           "shared.write", TYPE_VOID, 2},
    {"DELETE",        "shared.write", TYPE_VOID, 1},
    {"CAS",           "shared.write", TYPE_BOOLEAN, 3},
    {"LIST_KEYS",     "shared.read", TYPE_ARRAY, 0},
};

static const host_fn_def_t text_functions[] = {
    {"READ_ALL",      "file.read", TYPE_STRING, 1},
    {"READ_LINES",    "file.read", TYPE_ARRAY, 1},
    {"WRITE_ALL",     "file.write", TYPE_VOID, 2},
    {"APPEND_LINE",   "file.write", TYPE_VOID, 2},
    {"EXISTS",        "file.read", TYPE_BOOLEAN, 1},
};

static const host_fn_def_t bbs_functions[] = {
    {"SEND_NODE_MSG", "bbs.node_msg", TYPE_VOID, 2},
    {"ONLINE_NODES",  "bbs.node_msg", TYPE_ARRAY, 0},
};

static const host_fn_def_t sys_functions[] = {
    {"NOW",           NULL, TYPE_DATETIME, 0},
    {"TODAY",         NULL, TYPE_DATE, 0},
};

static const host_fn_def_t session_functions[] = {
    {"NODE",          NULL, TYPE_INTEGER, 0},
    {"ELAPSED_MS",    NULL, TYPE_INTEGER, 0},
};

static const host_fn_def_t door_functions[] = {
    {"EXIT",          NULL, TYPE_VOID, 1},
};

static const host_ns_def_t host_namespaces[] = {
    {"TERM",    term_functions,    sizeof(term_functions)/sizeof(term_functions[0])},
    {"USER",    user_functions,    sizeof(user_functions)/sizeof(user_functions[0])},
    {"USERS",   users_functions,   sizeof(users_functions)/sizeof(users_functions[0])},
    {"DATA",    data_functions,    sizeof(data_functions)/sizeof(data_functions[0])},
    {"KV",      kv_functions,      sizeof(kv_functions)/sizeof(kv_functions[0])},
    {"APP",     app_functions,     sizeof(app_functions)/sizeof(app_functions[0])},
    {"SHARED",  shared_functions,  sizeof(shared_functions)/sizeof(shared_functions[0])},
    {"TEXT",    text_functions,    sizeof(text_functions)/sizeof(text_functions[0])},
    {"BBS",     bbs_functions,     sizeof(bbs_functions)/sizeof(bbs_functions[0])},
    {"SYS",     sys_functions,     sizeof(sys_functions)/sizeof(sys_functions[0])},
    {"SESSION", session_functions, sizeof(session_functions)/sizeof(session_functions[0])},
    {"DOOR",    door_functions,    sizeof(door_functions)/sizeof(door_functions[0])},
};

void bucc_semantic_register_host_api(bucc_semantic_t* sem) {
    if (!sem) return;
    
    int ns_count = sizeof(host_namespaces) / sizeof(host_namespaces[0]);
    sem->host_namespaces = calloc(ns_count, sizeof(bucc_symbol_t*));
    sem->host_ns_count = ns_count;
    
    for (int i = 0; i < ns_count; i++) {
        const host_ns_def_t* ns = &host_namespaces[i];
        bucc_symbol_t* ns_sym = calloc(1, sizeof(bucc_symbol_t));
        ns_sym->name = strdup(ns->name);
        ns_sym->kind = SYM_HOST_NS;
        
        for (int j = 0; j < ns->fn_count; j++) {
            const host_fn_def_t* fn = &ns->functions[j];
            bucc_symbol_t* fn_sym = calloc(1, sizeof(bucc_symbol_t));
            fn_sym->name = strdup(fn->name);
            fn_sym->kind = SYM_HOST_FN;
            fn_sym->type = bucc_type_new(fn->return_type);
            if (fn->capability && strcasecmp_local(ns->name, "USERS") == 0) {
                fn_sym->flags |= SYM_FLAG_THROTTLED;
            }
            fn_sym->next = ns_sym->next;
            ns_sym->next = fn_sym;
        }
        
        sem->host_namespaces[i] = ns_sym;
    }
}

bucc_symbol_t* bucc_semantic_resolve_host_call(bucc_semantic_t* sem,
                                               const char* ns, const char* fn) {
    if (!sem || !ns || !fn) return NULL;
    
    for (uint32_t i = 0; i < sem->host_ns_count; i++) {
        bucc_symbol_t* ns_sym = sem->host_namespaces[i];
        if (strcasecmp_local(ns_sym->name, ns) == 0) {
            for (bucc_symbol_t* fn_sym = ns_sym->next; fn_sym; fn_sym = fn_sym->next) {
                if (strcasecmp_local(fn_sym->name, fn) == 0) {
                    return fn_sym;
                }
            }
        }
    }
    return NULL;
}

static const char* get_host_capability(const char* ns, const char* fn) {
    int ns_count = sizeof(host_namespaces) / sizeof(host_namespaces[0]);
    for (int i = 0; i < ns_count; i++) {
        if (strcasecmp_local(host_namespaces[i].name, ns) == 0) {
            for (int j = 0; j < host_namespaces[i].fn_count; j++) {
                if (strcasecmp_local(host_namespaces[i].functions[j].name, fn) == 0) {
                    return host_namespaces[i].functions[j].capability;
                }
            }
        }
    }
    return NULL;
}

static bucc_type_t* analyze_expr(bucc_semantic_t* sem, bucc_node_t* expr);
static bool analyze_stmt(bucc_semantic_t* sem, bucc_node_t* stmt);
static bool analyze_block(bucc_semantic_t* sem, bucc_node_t* block);

static bucc_type_t* analyze_expr(bucc_semantic_t* sem, bucc_node_t* expr) {
    if (!sem || !expr) return NULL;
    
    switch (expr->kind) {
        case NODE_EXPR_LITERAL: {
            switch (expr->data.literal.value.kind) {
                case BUCC_VAL_NULL:     return bucc_type_new(TYPE_NULL);
                case BUCC_VAL_BOOL:     return bucc_type_new(TYPE_BOOLEAN);
                case BUCC_VAL_I64:      return bucc_type_new(TYPE_INTEGER);
                case BUCC_VAL_F64:      return bucc_type_new(TYPE_DOUBLE);
                case BUCC_VAL_STRING:   return bucc_type_new(TYPE_STRING);
                case BUCC_VAL_DATE:     return bucc_type_new(TYPE_DATE);
                case BUCC_VAL_DATETIME: return bucc_type_new(TYPE_DATETIME);
                default:                return bucc_type_new(TYPE_ERROR);
            }
        }
        
        case NODE_EXPR_IDENT: {
            bucc_symbol_t* sym = bucc_scope_lookup(sem->current_scope, expr->data.ident.name);
            if (!sym) {
                bucc_diag_error(sem->diag, expr->span, BUCC_ERR_UNDEFINED_SYM,
                               "undefined identifier '%s'", expr->data.ident.name);
                return bucc_type_new(TYPE_ERROR);
            }
            sym->is_used = true;
            return bucc_type_copy(sym->type);
        }
        
        case NODE_EXPR_QUALIFIED: {
            bucc_symbol_t* fn_sym = bucc_semantic_resolve_host_call(sem,
                expr->data.qualified.ns, expr->data.qualified.name);
            if (!fn_sym) {
                bucc_diag_error(sem->diag, expr->span, BUCC_ERR_UNDEFINED_SYM,
                               "unknown host function '%s.%s'",
                               expr->data.qualified.ns, expr->data.qualified.name);
                return bucc_type_new(TYPE_ERROR);
            }
            
            const char* cap = get_host_capability(expr->data.qualified.ns,
                                                  expr->data.qualified.name);
            if (cap) {
                bucc_semantic_check_capability(sem, cap, expr->span);
            }
            
            if (fn_sym->flags & SYM_FLAG_THROTTLED) {
                if (sem->current_scope->loop_depth > 0) {
                    bucc_diag_warning(sem->diag, expr->span, BUCC_WARN_LOOP_LOOKUP,
                                     "throttled function '%s.%s' called inside loop",
                                     expr->data.qualified.ns, expr->data.qualified.name);
                }
            }
            
            return bucc_type_copy(fn_sym->type);
        }
        
        case NODE_EXPR_CALL: {
            bucc_type_t* callee_type = analyze_expr(sem, expr->data.call.callee);
            
            for (size_t i = 0; i < expr->data.call.args.count; i++) {
                bucc_type_t* arg_type = analyze_expr(sem, expr->data.call.args.items[i]);
                bucc_type_free(arg_type);
            }
            
            return callee_type;
        }
        
        case NODE_EXPR_UNARY: {
            bucc_type_t* operand = analyze_expr(sem, expr->data.unary.operand);
            
            if (expr->data.unary.op == UNARY_NEG) {
                if (!bucc_type_is_numeric(operand)) {
                    bucc_diag_error(sem->diag, expr->span, BUCC_ERR_TYPE_MISMATCH,
                                   "unary minus requires numeric operand, got %s",
                                   bucc_type_name(operand));
                }
                return operand;
            } else if (expr->data.unary.op == UNARY_NOT) {
                if (operand && operand->kind != TYPE_BOOLEAN) {
                    bucc_diag_error(sem->diag, expr->span, BUCC_ERR_TYPE_MISMATCH,
                                   "NOT requires boolean operand, got %s",
                                   bucc_type_name(operand));
                }
                bucc_type_free(operand);
                return bucc_type_new(TYPE_BOOLEAN);
            }
            return operand;
        }
        
        case NODE_EXPR_BINARY: {
            bucc_type_t* left = analyze_expr(sem, expr->data.binary.left);
            bucc_type_t* right = analyze_expr(sem, expr->data.binary.right);
            
            switch (expr->data.binary.op) {
                case BINARY_ADD:
                    if (left && left->kind == TYPE_STRING) {
                        bucc_type_free(left);
                        bucc_type_free(right);
                        return bucc_type_new(TYPE_STRING);
                    }
                    /* fall through */
                case BINARY_SUB:
                case BINARY_MUL:
                case BINARY_DIV:
                case BINARY_MOD:
                    if (!bucc_type_is_numeric(left) || !bucc_type_is_numeric(right)) {
                        bucc_diag_error(sem->diag, expr->span, BUCC_ERR_TYPE_MISMATCH,
                                       "arithmetic operator requires numeric operands");
                    }
                    if ((left && left->kind == TYPE_DOUBLE) ||
                        (right && right->kind == TYPE_DOUBLE)) {
                        bucc_type_free(left);
                        bucc_type_free(right);
                        return bucc_type_new(TYPE_DOUBLE);
                    }
                    bucc_type_free(left);
                    bucc_type_free(right);
                    return bucc_type_new(TYPE_INTEGER);
                    
                case BINARY_EQ:
                case BINARY_NE:
                case BINARY_LT:
                case BINARY_LE:
                case BINARY_GT:
                case BINARY_GE:
                    bucc_type_free(left);
                    bucc_type_free(right);
                    return bucc_type_new(TYPE_BOOLEAN);
                    
                case BINARY_AND:
                case BINARY_OR:
                    if ((left && left->kind != TYPE_BOOLEAN) ||
                        (right && right->kind != TYPE_BOOLEAN)) {
                        bucc_diag_error(sem->diag, expr->span, BUCC_ERR_TYPE_MISMATCH,
                                       "logical operator requires boolean operands");
                    }
                    bucc_type_free(left);
                    bucc_type_free(right);
                    return bucc_type_new(TYPE_BOOLEAN);
                    
                case BINARY_CONCAT:
                    bucc_type_free(left);
                    bucc_type_free(right);
                    return bucc_type_new(TYPE_STRING);
            }
            bucc_type_free(left);
            bucc_type_free(right);
            return bucc_type_new(TYPE_ERROR);
        }
        
        case NODE_EXPR_INDEX: {
            bucc_type_t* obj = analyze_expr(sem, expr->data.index_expr.object);
            bucc_type_t* idx = analyze_expr(sem, expr->data.index_expr.index);
            
            if (obj && obj->kind == TYPE_ARRAY) {
                if (idx && idx->kind != TYPE_INTEGER) {
                    bucc_diag_error(sem->diag, expr->span, BUCC_ERR_TYPE_MISMATCH,
                                   "array index must be integer");
                }
                bucc_type_t* elem = obj->element_type ? bucc_type_copy(obj->element_type) : bucc_type_new(TYPE_ANY);
                bucc_type_free(obj);
                bucc_type_free(idx);
                return elem;
            } else if (obj && obj->kind == TYPE_MAP) {
                if (idx && idx->kind != TYPE_STRING) {
                    bucc_diag_error(sem->diag, expr->span, BUCC_ERR_TYPE_MISMATCH,
                                   "map key must be string");
                }
                bucc_type_t* val = obj->element_type ? bucc_type_copy(obj->element_type) : bucc_type_new(TYPE_ANY);
                bucc_type_free(obj);
                bucc_type_free(idx);
                return val;
            }
            
            bucc_type_free(obj);
            bucc_type_free(idx);
            return bucc_type_new(TYPE_ERROR);
        }
        
        case NODE_EXPR_MEMBER: {
            bucc_type_t* obj = analyze_expr(sem, expr->data.member.object);
            bucc_type_free(obj);
            return bucc_type_new(TYPE_ANY);
        }
        
        case NODE_EXPR_ARRAY_LIT: {
            bucc_type_t* elem_type = NULL;
            for (size_t i = 0; i < expr->data.array_lit.elements.count; i++) {
                bucc_type_t* t = analyze_expr(sem, expr->data.array_lit.elements.items[i]);
                if (!elem_type) {
                    elem_type = t;
                } else {
                    bucc_type_free(t);
                }
            }
            return bucc_type_array(elem_type ? elem_type : bucc_type_new(TYPE_ANY));
        }
        
        case NODE_EXPR_MAP_LIT: {
            bucc_type_t* val_type = NULL;
            for (size_t i = 0; i < expr->data.map_lit.pairs.count; i++) {
                bucc_node_t* pair = expr->data.map_lit.pairs.items[i];
                if (pair->kind == NODE_EXPR_MAP_PAIR) {
                    bucc_type_t* t = analyze_expr(sem, pair->data.map_pair.value);
                    if (!val_type) {
                        val_type = t;
                    } else {
                        bucc_type_free(t);
                    }
                }
            }
            return bucc_type_map(val_type ? val_type : bucc_type_new(TYPE_ANY));
        }
        
        default:
            return bucc_type_new(TYPE_ERROR);
    }
}

static bool is_lvalue(bucc_node_t* expr) {
    if (!expr) return false;
    switch (expr->kind) {
        case NODE_EXPR_IDENT:
        case NODE_EXPR_INDEX:
            return true;
        case NODE_EXPR_MEMBER:
            return true;
        default:
            return false;
    }
}

static bool analyze_stmt(bucc_semantic_t* sem, bucc_node_t* stmt) {
    if (!sem || !stmt) return true;
    
    switch (stmt->kind) {
        case NODE_STMT_VAR_DECL: {
            bucc_symbol_t* existing = bucc_scope_lookup_local(sem->current_scope,
                                                              stmt->data.var_decl.name);
            if (existing) {
                bucc_diag_error(sem->diag, stmt->span, BUCC_ERR_DUPLICATE_DECL,
                               "variable '%s' already declared", stmt->data.var_decl.name);
                return false;
            }
            
            bucc_type_t* type = bucc_type_from_ast(stmt->data.var_decl.type_ref);
            
            if (stmt->data.var_decl.initializer) {
                bucc_type_t* init_type = analyze_expr(sem, stmt->data.var_decl.initializer);
                if (!bucc_type_assignable(type, init_type)) {
                    bucc_diag_error(sem->diag, stmt->span, BUCC_ERR_TYPE_MISMATCH,
                                   "cannot assign %s to %s",
                                   bucc_type_name(init_type), bucc_type_name(type));
                }
                bucc_type_free(init_type);
            }
            
            bucc_scope_define(sem->current_scope, stmt->data.var_decl.name,
                             SYM_LOCAL, type, stmt);
            return true;
        }
        
        case NODE_STMT_ASSIGN: {
            if (!is_lvalue(stmt->data.assign.target)) {
                bucc_diag_error(sem->diag, stmt->span, BUCC_ERR_INVALID_LVALUE,
                               "invalid assignment target");
                return false;
            }
            
            bucc_type_t* target_type = analyze_expr(sem, stmt->data.assign.target);
            bucc_type_t* value_type = analyze_expr(sem, stmt->data.assign.value);
            
            if (!bucc_type_assignable(target_type, value_type)) {
                bucc_diag_error(sem->diag, stmt->span, BUCC_ERR_TYPE_MISMATCH,
                               "cannot assign %s to %s",
                               bucc_type_name(value_type), bucc_type_name(target_type));
            }
            
            bucc_type_free(target_type);
            bucc_type_free(value_type);
            return true;
        }
        
        case NODE_STMT_EXPR: {
            bucc_type_t* type = analyze_expr(sem, stmt->data.expr_stmt.expr);
            bucc_type_free(type);
            return true;
        }
        
        case NODE_STMT_IF: {
            bucc_type_t* cond = analyze_expr(sem, stmt->data.if_stmt.condition);
            if (cond && cond->kind != TYPE_BOOLEAN) {
                bucc_diag_error(sem->diag, stmt->span, BUCC_ERR_TYPE_MISMATCH,
                               "IF condition must be boolean");
            }
            bucc_type_free(cond);
            
            analyze_block(sem, stmt->data.if_stmt.then_block);
            
            for (size_t i = 0; i < stmt->data.if_stmt.elseif_clauses.count; i++) {
                bucc_node_t* clause = stmt->data.if_stmt.elseif_clauses.items[i];
                if (clause->kind == NODE_STMT_IF) {
                    bucc_type_t* ec = analyze_expr(sem, clause->data.if_stmt.condition);
                    bucc_type_free(ec);
                    analyze_block(sem, clause->data.if_stmt.then_block);
                }
            }
            
            if (stmt->data.if_stmt.else_block) {
                analyze_block(sem, stmt->data.if_stmt.else_block);
            }
            return true;
        }
        
        case NODE_STMT_SELECT: {
            bucc_type_t* expr = analyze_expr(sem, stmt->data.select_stmt.expr);
            bucc_type_free(expr);
            
            for (size_t i = 0; i < stmt->data.select_stmt.cases.count; i++) {
                bucc_node_t* case_node = stmt->data.select_stmt.cases.items[i];
                if (case_node->kind == NODE_STMT_CASE) {
                    for (size_t j = 0; j < case_node->data.case_clause.values.count; j++) {
                        bucc_type_t* vt = analyze_expr(sem, case_node->data.case_clause.values.items[j]);
                        bucc_type_free(vt);
                    }
                    analyze_block(sem, case_node->data.case_clause.body);
                }
            }
            
            if (stmt->data.select_stmt.else_case) {
                analyze_block(sem, stmt->data.select_stmt.else_case->data.case_clause.body);
            }
            return true;
        }
        
        case NODE_STMT_WHILE: {
            bucc_type_t* cond = analyze_expr(sem, stmt->data.while_stmt.condition);
            if (cond && cond->kind != TYPE_BOOLEAN) {
                bucc_diag_error(sem->diag, stmt->span, BUCC_ERR_TYPE_MISMATCH,
                               "WHILE condition must be boolean");
            }
            bucc_type_free(cond);
            
            sem->current_scope->in_loop = true;
            sem->current_scope->loop_depth++;
            analyze_block(sem, stmt->data.while_stmt.body);
            sem->current_scope->loop_depth--;
            sem->current_scope->in_loop = sem->current_scope->loop_depth > 0;
            return true;
        }
        
        case NODE_STMT_DO_LOOP: {
            sem->current_scope->in_loop = true;
            sem->current_scope->loop_depth++;
            analyze_block(sem, stmt->data.do_loop.body);
            sem->current_scope->loop_depth--;
            sem->current_scope->in_loop = sem->current_scope->loop_depth > 0;
            
            if (stmt->data.do_loop.condition) {
                bucc_type_t* cond = analyze_expr(sem, stmt->data.do_loop.condition);
                if (cond && cond->kind != TYPE_BOOLEAN) {
                    bucc_diag_error(sem->diag, stmt->span, BUCC_ERR_TYPE_MISMATCH,
                                   "LOOP condition must be boolean");
                }
                bucc_type_free(cond);
            }
            return true;
        }
        
        case NODE_STMT_FOR: {
            bucc_type_t* start = analyze_expr(sem, stmt->data.for_stmt.start_expr);
            bucc_type_t* end = analyze_expr(sem, stmt->data.for_stmt.end_expr);
            
            if (!bucc_type_is_numeric(start) || !bucc_type_is_numeric(end)) {
                bucc_diag_error(sem->diag, stmt->span, BUCC_ERR_TYPE_MISMATCH,
                               "FOR bounds must be numeric");
            }
            bucc_type_free(start);
            bucc_type_free(end);
            
            if (stmt->data.for_stmt.step_expr) {
                bucc_type_t* step = analyze_expr(sem, stmt->data.for_stmt.step_expr);
                if (!bucc_type_is_numeric(step)) {
                    bucc_diag_error(sem->diag, stmt->span, BUCC_ERR_TYPE_MISMATCH,
                                   "FOR STEP must be numeric");
                }
                bucc_type_free(step);
            }
            
            bucc_scope_t* loop_scope = bucc_scope_new(sem->current_scope, SCOPE_BLOCK);
            loop_scope->in_loop = true;
            loop_scope->loop_depth = sem->current_scope->loop_depth + 1;
            
            bucc_scope_define(loop_scope, stmt->data.for_stmt.var_name,
                             SYM_LOCAL, bucc_type_new(TYPE_INTEGER), stmt);
            
            sem->current_scope = loop_scope;
            analyze_block(sem, stmt->data.for_stmt.body);
            sem->current_scope = loop_scope->parent;
            bucc_scope_free(loop_scope);
            return true;
        }
        
        case NODE_STMT_TRY_CATCH: {
            sem->current_scope->in_try = true;
            analyze_block(sem, stmt->data.try_catch.try_block);
            sem->current_scope->in_try = false;
            
            bucc_scope_t* catch_scope = bucc_scope_new(sem->current_scope, SCOPE_BLOCK);
            if (stmt->data.try_catch.error_var) {
                bucc_scope_define(catch_scope, stmt->data.try_catch.error_var,
                                 SYM_LOCAL, bucc_type_new(TYPE_MAP), stmt);
            }
            
            sem->current_scope = catch_scope;
            analyze_block(sem, stmt->data.try_catch.catch_block);
            sem->current_scope = catch_scope->parent;
            bucc_scope_free(catch_scope);
            return true;
        }
        
        case NODE_STMT_RETURN: {
            if (stmt->data.return_stmt.value) {
                bucc_type_t* ret = analyze_expr(sem, stmt->data.return_stmt.value);
                if (sem->in_function && sem->current_return_type) {
                    if (!bucc_type_assignable(sem->current_return_type, ret)) {
                        bucc_diag_error(sem->diag, stmt->span, BUCC_ERR_TYPE_MISMATCH,
                                       "return type mismatch: expected %s, got %s",
                                       bucc_type_name(sem->current_return_type),
                                       bucc_type_name(ret));
                    }
                }
                bucc_type_free(ret);
            } else if (sem->in_function) {
                bucc_diag_error(sem->diag, stmt->span, BUCC_ERR_NO_RETURN,
                               "function must return a value");
            }
            return true;
        }
        
        case NODE_STMT_EXIT: {
            if (!sem->current_scope->in_loop &&
                stmt->data.exit_stmt.exit_kind != EXIT_SELECT) {
                bucc_diag_error(sem->diag, stmt->span, BUCC_ERR_INVALID_EXIT,
                               "EXIT outside of loop");
            }
            return true;
        }
        
        case NODE_STMT_HALT:
            return true;
            
        case NODE_STMT_THROW: {
            bucc_type_t* t = analyze_expr(sem, stmt->data.throw_stmt.expr);
            bucc_type_free(t);
            return true;
        }
        
        case NODE_STMT_CHAIN: {
            bucc_semantic_check_capability(sem, "door.chain", stmt->span);
            if (stmt->data.chain_stmt.args) {
                bucc_type_t* t = analyze_expr(sem, stmt->data.chain_stmt.args);
                bucc_type_free(t);
            }
            return true;
        }
        
        case NODE_STMT_ON_CALL: {
            bucc_type_t* sel = analyze_expr(sem, stmt->data.on_call.selector);
            if (sel && sel->kind != TYPE_INTEGER) {
                bucc_diag_error(sem->diag, stmt->span, BUCC_ERR_TYPE_MISMATCH,
                               "ON selector must be integer");
            }
            bucc_type_free(sel);
            
            for (size_t i = 0; i < stmt->data.on_call.targets.count; i++) {
                bucc_node_t* target = stmt->data.on_call.targets.items[i];
                if (target->kind == NODE_EXPR_IDENT) {
                    bucc_symbol_t* sym = bucc_scope_lookup(sem->current_scope,
                                                          target->data.ident.name);
                    if (!sym || (sym->kind != SYM_PROCEDURE && sym->kind != SYM_FUNCTION)) {
                        bucc_diag_error(sem->diag, target->span, BUCC_ERR_UNDEFINED_SYM,
                                       "ON CALL target '%s' is not a procedure",
                                       target->data.ident.name);
                    }
                }
            }
            return true;
        }
        
        default:
            return true;
    }
}

static bool analyze_block(bucc_semantic_t* sem, bucc_node_t* block) {
    if (!sem || !block || block->kind != NODE_STMT_BLOCK) return true;
    
    for (size_t i = 0; i < block->data.block.stmts.count; i++) {
        analyze_stmt(sem, block->data.block.stmts.items[i]);
    }
    return true;
}

static bool analyze_procedure(bucc_semantic_t* sem, bucc_node_t* proc) {
    if (!sem || !proc) return false;
    
    bucc_scope_t* proc_scope = bucc_scope_new(sem->global_scope, SCOPE_PROCEDURE);
    sem->current_scope = proc_scope;
    sem->in_function = false;
    sem->current_return_type = NULL;
    
    for (size_t i = 0; i < proc->data.proc_decl.params.count; i++) {
        bucc_node_t* param = proc->data.proc_decl.params.items[i];
        bucc_type_t* type = bucc_type_from_ast(param->data.param_decl.type_ref);
        bucc_scope_define(proc_scope, param->data.param_decl.name, SYM_PARAM, type, param);
    }
    
    analyze_block(sem, proc->data.proc_decl.body);
    
    sem->current_scope = sem->global_scope;
    bucc_scope_free(proc_scope);
    return true;
}

static bool analyze_function(bucc_semantic_t* sem, bucc_node_t* func) {
    if (!sem || !func) return false;
    
    bucc_scope_t* func_scope = bucc_scope_new(sem->global_scope, SCOPE_FUNCTION);
    sem->current_scope = func_scope;
    sem->in_function = true;
    sem->current_return_type = bucc_type_from_ast(func->data.func_decl.return_type);
    
    for (size_t i = 0; i < func->data.func_decl.params.count; i++) {
        bucc_node_t* param = func->data.func_decl.params.items[i];
        bucc_type_t* type = bucc_type_from_ast(param->data.param_decl.type_ref);
        bucc_scope_define(func_scope, param->data.param_decl.name, SYM_PARAM, type, param);
    }
    
    analyze_block(sem, func->data.func_decl.body);
    
    bucc_type_free(sem->current_return_type);
    sem->current_return_type = NULL;
    sem->in_function = false;
    sem->current_scope = sem->global_scope;
    bucc_scope_free(func_scope);
    return true;
}

bool bucc_semantic_analyze(bucc_semantic_t* sem, bucc_node_t* module) {
    if (!sem || !module || module->kind != NODE_MODULE) return false;
    
    sem->module = module;
    
    for (size_t i = 0; i < module->data.module.metadata.count; i++) {
        bucc_node_t* meta = module->data.module.metadata.items[i];
        switch (meta->kind) {
            case NODE_META_PROGRAM:
                sem->program_name = meta->data.meta_string.value ?
                    strdup(meta->data.meta_string.value) : NULL;
                break;
            case NODE_META_VERSION:
                sem->program_version = meta->data.meta_string.value ?
                    strdup(meta->data.meta_string.value) : NULL;
                break;
            case NODE_META_CAPABILITY:
                bucc_semantic_declare_capability(sem, meta->data.meta_string.value);
                break;
            default:
                break;
        }
    }
    
    for (size_t i = 0; i < module->data.module.globals.count; i++) {
        bucc_node_t* global = module->data.module.globals.items[i];
        if (global->kind == NODE_VAR_DECL) {
            bucc_type_t* type = bucc_type_from_ast(global->data.var_decl.type_ref);
            bucc_scope_define(sem->global_scope, global->data.var_decl.name,
                             SYM_GLOBAL, type, global);
            sem->global_count++;
        }
    }
    
    for (size_t i = 0; i < module->data.module.procedures.count; i++) {
        bucc_node_t* proc = module->data.module.procedures.items[i];
        const char* name = NULL;
        bucc_symbol_kind_t kind;
        bucc_type_t* type;
        
        if (proc->kind == NODE_PROC_DECL) {
            name = proc->data.proc_decl.name;
            kind = SYM_PROCEDURE;
            type = bucc_type_new(TYPE_VOID);
            
            if (strcasecmp_local(name, "Main") == 0) {
                sem->has_main = true;
            }
        } else if (proc->kind == NODE_FUNC_DECL) {
            name = proc->data.func_decl.name;
            kind = SYM_FUNCTION;
            type = bucc_type_from_ast(proc->data.func_decl.return_type);
        } else {
            continue;
        }
        
        bucc_symbol_t* existing = bucc_scope_lookup_local(sem->global_scope, name);
        if (existing) {
            bucc_diag_error(sem->diag, proc->span, BUCC_ERR_DUPLICATE_DECL,
                           "procedure '%s' already declared", name);
            bucc_type_free(type);
            continue;
        }
        
        bucc_scope_define(sem->global_scope, name, kind, type, proc);
        sem->proc_count++;
    }
    
    if (!sem->has_main) {
        bucc_diag_error(sem->diag, module->span, BUCC_ERR_MISSING_MAIN,
                       "module must have a Main procedure");
    }
    
    for (size_t i = 0; i < module->data.module.procedures.count; i++) {
        bucc_node_t* proc = module->data.module.procedures.items[i];
        if (proc->kind == NODE_PROC_DECL) {
            analyze_procedure(sem, proc);
        } else if (proc->kind == NODE_FUNC_DECL) {
            analyze_function(sem, proc);
        }
    }
    
    return !bucc_diag_has_errors(sem->diag);
}
