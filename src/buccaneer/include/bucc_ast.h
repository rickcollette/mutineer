/*
 * bucc_ast.h - Buccaneer Abstract Syntax Tree
 *
 * Part of the Buccaneer language implementation for Mutineer BBS.
 * Defines AST node types per BUCCANEER_AST_SPEC.md.
 */

#ifndef BUCC_AST_H
#define BUCC_AST_H

#include "bucc_diag.h"
#include "bucc_value.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum bucc_node_kind {
    NODE_MODULE,
    
    NODE_META_PROGRAM,
    NODE_META_VERSION,
    NODE_META_AUTHOR,
    NODE_META_DESCRIPTION,
    NODE_META_CAPABILITY,
    NODE_META_OPTION,
    NODE_META_DATASET,
    
    NODE_VAR_DECL,
    NODE_PROC_DECL,
    NODE_FUNC_DECL,
    NODE_PARAM_DECL,
    
    NODE_TYPE_SCALAR,
    NODE_TYPE_ARRAY,
    NODE_TYPE_MAP,
    
    NODE_STMT_BLOCK,
    NODE_STMT_VAR_DECL,
    NODE_STMT_ASSIGN,
    NODE_STMT_EXPR,
    NODE_STMT_IF,
    NODE_STMT_SELECT,
    NODE_STMT_CASE,
    NODE_STMT_WHILE,
    NODE_STMT_DO_LOOP,
    NODE_STMT_FOR,
    NODE_STMT_TRY_CATCH,
    NODE_STMT_RETURN,
    NODE_STMT_EXIT,
    NODE_STMT_HALT,
    NODE_STMT_THROW,
    NODE_STMT_CHAIN,
    NODE_STMT_ON_CALL,
    
    NODE_EXPR_IDENT,
    NODE_EXPR_LITERAL,
    NODE_EXPR_UNARY,
    NODE_EXPR_BINARY,
    NODE_EXPR_CALL,
    NODE_EXPR_MEMBER,
    NODE_EXPR_INDEX,
    NODE_EXPR_ARRAY_LIT,
    NODE_EXPR_MAP_LIT,
    NODE_EXPR_MAP_PAIR,
    NODE_EXPR_QUALIFIED,
    
    NODE_ERROR
} bucc_node_kind_t;

typedef enum bucc_scalar_type {
    SCALAR_INTEGER,
    SCALAR_DOUBLE,
    SCALAR_BOOLEAN,
    SCALAR_STRING,
    SCALAR_DATE,
    SCALAR_DATETIME
} bucc_scalar_type_t;

typedef enum bucc_unary_op {
    UNARY_NEG,
    UNARY_NOT
} bucc_unary_op_t;

typedef enum bucc_binary_op {
    BINARY_ADD,
    BINARY_SUB,
    BINARY_MUL,
    BINARY_DIV,
    BINARY_MOD,
    BINARY_EQ,
    BINARY_NE,
    BINARY_LT,
    BINARY_LE,
    BINARY_GT,
    BINARY_GE,
    BINARY_AND,
    BINARY_OR,
    BINARY_CONCAT
} bucc_binary_op_t;

typedef enum bucc_exit_kind {
    EXIT_FOR,
    EXIT_DO,
    EXIT_WHILE,
    EXIT_SELECT
} bucc_exit_kind_t;

typedef enum bucc_loop_kind {
    LOOP_PLAIN,
    LOOP_UNTIL,
    LOOP_WHILE
} bucc_loop_kind_t;

typedef struct bucc_node bucc_node_t;

typedef struct bucc_node_list {
    bucc_node_t** items;
    size_t        count;
    size_t        cap;
} bucc_node_list_t;

struct bucc_node {
    bucc_node_kind_t   kind;
    bucc_source_span_t span;
    uint32_t           node_id;
    
    union {
        struct {
            bucc_node_list_t metadata;
            bucc_node_list_t globals;
            bucc_node_list_t procedures;
        } module;
        
        struct {
            char* value;
        } meta_string;
        
        struct {
            char* name;
            char* value;
        } meta_option;
        
        struct {
            char* name;
            bucc_node_list_t fields;
        } meta_dataset;
        
        struct {
            char*        name;
            bucc_node_t* type_ref;
            bucc_node_t* initializer;
            bool         is_global;
        } var_decl;
        
        struct {
            char*            name;
            bucc_node_list_t params;
            bucc_node_t*     body;
            bool             is_handler;
        } proc_decl;
        
        struct {
            char*            name;
            bucc_node_list_t params;
            bucc_node_t*     return_type;
            bucc_node_t*     body;
        } func_decl;
        
        struct {
            char*        name;
            bucc_node_t* type_ref;
        } param_decl;
        
        struct {
            bucc_scalar_type_t scalar_type;
        } type_scalar;
        
        struct {
            bucc_node_t* element_type;
        } type_array;
        
        struct {
            bucc_node_t* value_type;
        } type_map;
        
        struct {
            bucc_node_list_t stmts;
        } block;
        
        struct {
            bucc_node_t* target;
            bucc_node_t* value;
        } assign;
        
        struct {
            bucc_node_t* expr;
        } expr_stmt;
        
        struct {
            bucc_node_t*     condition;
            bucc_node_t*     then_block;
            bucc_node_list_t elseif_clauses;
            bucc_node_t*     else_block;
        } if_stmt;
        
        struct {
            bucc_node_t*     condition;
            bucc_node_t*     body;
        } elseif_clause;
        
        struct {
            bucc_node_t*     expr;
            bucc_node_list_t cases;
            bucc_node_t*     else_case;
        } select_stmt;
        
        struct {
            bucc_node_list_t values;
            bucc_node_t*     body;
            bool             is_else;
        } case_clause;
        
        struct {
            bucc_node_t* condition;
            bucc_node_t* body;
        } while_stmt;
        
        struct {
            bucc_node_t*     body;
            bucc_node_t*     condition;
            bucc_loop_kind_t loop_kind;
        } do_loop;
        
        struct {
            char*        var_name;
            bucc_node_t* start_expr;
            bucc_node_t* end_expr;
            bucc_node_t* step_expr;
            bucc_node_t* body;
        } for_stmt;
        
        struct {
            bucc_node_t* try_block;
            char*        error_var;
            bucc_node_t* catch_block;
        } try_catch;
        
        struct {
            bucc_node_t* value;
        } return_stmt;
        
        struct {
            bucc_exit_kind_t exit_kind;
        } exit_stmt;
        
        struct {
            bucc_node_t* expr;
        } throw_stmt;
        
        struct {
            char*        target;
            bucc_node_t* args;
        } chain_stmt;
        
        struct {
            bucc_node_t*     selector;
            bucc_node_list_t targets;
        } on_call;
        
        struct {
            char* name;
        } ident;
        
        struct {
            bucc_value_t value;
        } literal;
        
        struct {
            bucc_unary_op_t op;
            bucc_node_t*    operand;
        } unary;
        
        struct {
            bucc_binary_op_t op;
            bucc_node_t*     left;
            bucc_node_t*     right;
        } binary;
        
        struct {
            bucc_node_t*     callee;
            bucc_node_list_t args;
        } call;
        
        struct {
            bucc_node_t* object;
            char*        member;
        } member;
        
        struct {
            bucc_node_t* object;
            bucc_node_t* index;
        } index_expr;
        
        struct {
            bucc_node_list_t elements;
        } array_lit;
        
        struct {
            bucc_node_list_t pairs;
        } map_lit;
        
        struct {
            char*        key;
            bucc_node_t* value;
        } map_pair;
        
        struct {
            char* ns;
            char* name;
        } qualified;
    } data;
};

bucc_node_t* bucc_node_new(bucc_node_kind_t kind, bucc_source_span_t span);
void bucc_node_free(bucc_node_t* node);

void bucc_node_list_init(bucc_node_list_t* list);
void bucc_node_list_free(bucc_node_list_t* list);
bool bucc_node_list_push(bucc_node_list_t* list, bucc_node_t* node);

bucc_node_t* bucc_ast_module_new(bucc_source_span_t span);

bucc_node_t* bucc_ast_meta_program(bucc_source_span_t span, const char* name);
bucc_node_t* bucc_ast_meta_version(bucc_source_span_t span, const char* version);
bucc_node_t* bucc_ast_meta_author(bucc_source_span_t span, const char* author);
bucc_node_t* bucc_ast_meta_description(bucc_source_span_t span, const char* desc);
bucc_node_t* bucc_ast_meta_capability(bucc_source_span_t span, const char* cap);
bucc_node_t* bucc_ast_meta_option(bucc_source_span_t span, const char* name, const char* value);
bucc_node_t* bucc_ast_meta_dataset(bucc_source_span_t span, const char* name);

bucc_node_t* bucc_ast_var_decl(bucc_source_span_t span, const char* name,
                               bucc_node_t* type_ref, bucc_node_t* init, bool is_global);
bucc_node_t* bucc_ast_proc_decl(bucc_source_span_t span, const char* name,
                                bucc_node_t* body, bool is_handler);
bucc_node_t* bucc_ast_func_decl(bucc_source_span_t span, const char* name,
                                bucc_node_t* return_type, bucc_node_t* body);
bucc_node_t* bucc_ast_param_decl(bucc_source_span_t span, const char* name,
                                 bucc_node_t* type_ref);

bucc_node_t* bucc_ast_type_scalar(bucc_source_span_t span, bucc_scalar_type_t scalar);
bucc_node_t* bucc_ast_type_array(bucc_source_span_t span, bucc_node_t* element_type);
bucc_node_t* bucc_ast_type_map(bucc_source_span_t span, bucc_node_t* value_type);

bucc_node_t* bucc_ast_block(bucc_source_span_t span);
bucc_node_t* bucc_ast_stmt_var_decl(bucc_source_span_t span, bucc_node_t* decl);
bucc_node_t* bucc_ast_stmt_assign(bucc_source_span_t span, bucc_node_t* target, bucc_node_t* value);
bucc_node_t* bucc_ast_stmt_expr(bucc_source_span_t span, bucc_node_t* expr);
bucc_node_t* bucc_ast_stmt_if(bucc_source_span_t span, bucc_node_t* cond, bucc_node_t* then_block);
bucc_node_t* bucc_ast_stmt_select(bucc_source_span_t span, bucc_node_t* expr);
bucc_node_t* bucc_ast_stmt_case(bucc_source_span_t span, bucc_node_t* body, bool is_else);
bucc_node_t* bucc_ast_stmt_while(bucc_source_span_t span, bucc_node_t* cond, bucc_node_t* body);
bucc_node_t* bucc_ast_stmt_do_loop(bucc_source_span_t span, bucc_node_t* body,
                                   bucc_node_t* cond, bucc_loop_kind_t kind);
bucc_node_t* bucc_ast_stmt_for(bucc_source_span_t span, const char* var,
                               bucc_node_t* start, bucc_node_t* end, bucc_node_t* step,
                               bucc_node_t* body);
bucc_node_t* bucc_ast_stmt_try_catch(bucc_source_span_t span, bucc_node_t* try_block,
                                     const char* error_var, bucc_node_t* catch_block);
bucc_node_t* bucc_ast_stmt_return(bucc_source_span_t span, bucc_node_t* value);
bucc_node_t* bucc_ast_stmt_exit(bucc_source_span_t span, bucc_exit_kind_t kind);
bucc_node_t* bucc_ast_stmt_halt(bucc_source_span_t span);
bucc_node_t* bucc_ast_stmt_throw(bucc_source_span_t span, bucc_node_t* expr);
bucc_node_t* bucc_ast_stmt_chain(bucc_source_span_t span, const char* target, bucc_node_t* args);
bucc_node_t* bucc_ast_stmt_on_call(bucc_source_span_t span, bucc_node_t* selector);

bucc_node_t* bucc_ast_expr_ident(bucc_source_span_t span, const char* name);
bucc_node_t* bucc_ast_expr_literal_null(bucc_source_span_t span);
bucc_node_t* bucc_ast_expr_literal_bool(bucc_source_span_t span, bool value);
bucc_node_t* bucc_ast_expr_literal_i64(bucc_source_span_t span, int64_t value);
bucc_node_t* bucc_ast_expr_literal_f64(bucc_source_span_t span, double value);
bucc_node_t* bucc_ast_expr_literal_string(bucc_source_span_t span, const char* value, size_t len);
bucc_node_t* bucc_ast_expr_literal_date(bucc_source_span_t span, bucc_date_t date);
bucc_node_t* bucc_ast_expr_literal_datetime(bucc_source_span_t span, bucc_datetime_t dt);
bucc_node_t* bucc_ast_expr_unary(bucc_source_span_t span, bucc_unary_op_t op, bucc_node_t* operand);
bucc_node_t* bucc_ast_expr_binary(bucc_source_span_t span, bucc_binary_op_t op,
                                  bucc_node_t* left, bucc_node_t* right);
bucc_node_t* bucc_ast_expr_call(bucc_source_span_t span, bucc_node_t* callee);
bucc_node_t* bucc_ast_expr_member(bucc_source_span_t span, bucc_node_t* object, const char* member);
bucc_node_t* bucc_ast_expr_index(bucc_source_span_t span, bucc_node_t* object, bucc_node_t* index);
bucc_node_t* bucc_ast_expr_array_lit(bucc_source_span_t span);
bucc_node_t* bucc_ast_expr_map_lit(bucc_source_span_t span);
bucc_node_t* bucc_ast_expr_map_pair(bucc_source_span_t span, const char* key, bucc_node_t* value);
bucc_node_t* bucc_ast_expr_qualified(bucc_source_span_t span, const char* ns, const char* name);

const char* bucc_node_kind_name(bucc_node_kind_t kind);
const char* bucc_scalar_type_name(bucc_scalar_type_t type);
const char* bucc_unary_op_name(bucc_unary_op_t op);
const char* bucc_binary_op_name(bucc_binary_op_t op);

void bucc_ast_print(bucc_node_t* node, FILE* out, int indent);

#ifdef __cplusplus
}
#endif

#endif /* BUCC_AST_H */
