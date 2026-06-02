/*
 * ast.c - Buccaneer Abstract Syntax Tree implementation
 *
 * Part of the Buccaneer language implementation for Mutineer BBS.
 */

#define _POSIX_C_SOURCE 200809L
#include "include/bucc_ast.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static uint32_t next_node_id = 1;

bucc_node_t* bucc_node_new(bucc_node_kind_t kind, bucc_source_span_t span) {
    bucc_node_t* node = calloc(1, sizeof(bucc_node_t));
    if (!node) return NULL;
    
    node->kind = kind;
    node->span = span;
    node->node_id = next_node_id++;
    
    return node;
}

static void free_node_list_contents(bucc_node_list_t* list) {
    if (!list) return;
    for (size_t i = 0; i < list->count; i++) {
        bucc_node_free(list->items[i]);
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->cap = 0;
}

void bucc_node_free(bucc_node_t* node) {
    if (!node) return;
    
    switch (node->kind) {
        case NODE_MODULE:
            free_node_list_contents(&node->data.module.metadata);
            free_node_list_contents(&node->data.module.globals);
            free_node_list_contents(&node->data.module.procedures);
            break;
            
        case NODE_META_PROGRAM:
        case NODE_META_VERSION:
        case NODE_META_AUTHOR:
        case NODE_META_DESCRIPTION:
        case NODE_META_CAPABILITY:
            free(node->data.meta_string.value);
            break;
            
        case NODE_META_OPTION:
            free(node->data.meta_option.name);
            free(node->data.meta_option.value);
            break;
            
        case NODE_META_DATASET:
            free(node->data.meta_dataset.name);
            free_node_list_contents(&node->data.meta_dataset.fields);
            break;
            
        case NODE_VAR_DECL:
            free(node->data.var_decl.name);
            bucc_node_free(node->data.var_decl.type_ref);
            bucc_node_free(node->data.var_decl.initializer);
            break;
            
        case NODE_PROC_DECL:
            free(node->data.proc_decl.name);
            free_node_list_contents(&node->data.proc_decl.params);
            bucc_node_free(node->data.proc_decl.body);
            break;
            
        case NODE_FUNC_DECL:
            free(node->data.func_decl.name);
            free_node_list_contents(&node->data.func_decl.params);
            bucc_node_free(node->data.func_decl.return_type);
            bucc_node_free(node->data.func_decl.body);
            break;
            
        case NODE_PARAM_DECL:
            free(node->data.param_decl.name);
            bucc_node_free(node->data.param_decl.type_ref);
            break;
            
        case NODE_TYPE_ARRAY:
            bucc_node_free(node->data.type_array.element_type);
            break;
            
        case NODE_TYPE_MAP:
            bucc_node_free(node->data.type_map.value_type);
            break;
            
        case NODE_STMT_BLOCK:
            free_node_list_contents(&node->data.block.stmts);
            break;
            
        case NODE_STMT_VAR_DECL:
            bucc_node_free(node->data.var_decl.type_ref);
            bucc_node_free(node->data.var_decl.initializer);
            free(node->data.var_decl.name);
            break;
            
        case NODE_STMT_ASSIGN:
            bucc_node_free(node->data.assign.target);
            bucc_node_free(node->data.assign.value);
            break;
            
        case NODE_STMT_EXPR:
            bucc_node_free(node->data.expr_stmt.expr);
            break;
            
        case NODE_STMT_IF:
            bucc_node_free(node->data.if_stmt.condition);
            bucc_node_free(node->data.if_stmt.then_block);
            free_node_list_contents(&node->data.if_stmt.elseif_clauses);
            bucc_node_free(node->data.if_stmt.else_block);
            break;
            
        case NODE_STMT_SELECT:
            bucc_node_free(node->data.select_stmt.expr);
            free_node_list_contents(&node->data.select_stmt.cases);
            bucc_node_free(node->data.select_stmt.else_case);
            break;
            
        case NODE_STMT_CASE:
            free_node_list_contents(&node->data.case_clause.values);
            bucc_node_free(node->data.case_clause.body);
            break;
            
        case NODE_STMT_WHILE:
            bucc_node_free(node->data.while_stmt.condition);
            bucc_node_free(node->data.while_stmt.body);
            break;
            
        case NODE_STMT_DO_LOOP:
            bucc_node_free(node->data.do_loop.body);
            bucc_node_free(node->data.do_loop.condition);
            break;
            
        case NODE_STMT_FOR:
            free(node->data.for_stmt.var_name);
            bucc_node_free(node->data.for_stmt.start_expr);
            bucc_node_free(node->data.for_stmt.end_expr);
            bucc_node_free(node->data.for_stmt.step_expr);
            bucc_node_free(node->data.for_stmt.body);
            break;
            
        case NODE_STMT_TRY_CATCH:
            bucc_node_free(node->data.try_catch.try_block);
            free(node->data.try_catch.error_var);
            bucc_node_free(node->data.try_catch.catch_block);
            break;
            
        case NODE_STMT_RETURN:
            bucc_node_free(node->data.return_stmt.value);
            break;
            
        case NODE_STMT_THROW:
            bucc_node_free(node->data.throw_stmt.expr);
            break;
            
        case NODE_STMT_CHAIN:
            free(node->data.chain_stmt.target);
            bucc_node_free(node->data.chain_stmt.args);
            break;
            
        case NODE_STMT_ON_CALL:
            bucc_node_free(node->data.on_call.selector);
            free_node_list_contents(&node->data.on_call.targets);
            break;
            
        case NODE_EXPR_IDENT:
            free(node->data.ident.name);
            break;
            
        case NODE_EXPR_LITERAL:
            bucc_value_release(&node->data.literal.value);
            break;
            
        case NODE_EXPR_UNARY:
            bucc_node_free(node->data.unary.operand);
            break;
            
        case NODE_EXPR_BINARY:
            bucc_node_free(node->data.binary.left);
            bucc_node_free(node->data.binary.right);
            break;
            
        case NODE_EXPR_CALL:
            bucc_node_free(node->data.call.callee);
            free_node_list_contents(&node->data.call.args);
            break;
            
        case NODE_EXPR_MEMBER:
            bucc_node_free(node->data.member.object);
            free(node->data.member.member);
            break;
            
        case NODE_EXPR_INDEX:
            bucc_node_free(node->data.index_expr.object);
            bucc_node_free(node->data.index_expr.index);
            break;
            
        case NODE_EXPR_ARRAY_LIT:
            free_node_list_contents(&node->data.array_lit.elements);
            break;
            
        case NODE_EXPR_MAP_LIT:
            free_node_list_contents(&node->data.map_lit.pairs);
            break;
            
        case NODE_EXPR_MAP_PAIR:
            free(node->data.map_pair.key);
            bucc_node_free(node->data.map_pair.value);
            break;
            
        case NODE_EXPR_QUALIFIED:
            free(node->data.qualified.ns);
            free(node->data.qualified.name);
            break;
            
        default:
            break;
    }
    
    free(node);
}

void bucc_node_list_init(bucc_node_list_t* list) {
    list->items = NULL;
    list->count = 0;
    list->cap = 0;
}

void bucc_node_list_free(bucc_node_list_t* list) {
    free_node_list_contents(list);
}

bool bucc_node_list_push(bucc_node_list_t* list, bucc_node_t* node) {
    if (!list) return false;
    
    if (list->count >= list->cap) {
        size_t new_cap = list->cap == 0 ? 8 : list->cap * 2;
        bucc_node_t** new_items = realloc(list->items, new_cap * sizeof(bucc_node_t*));
        if (!new_items) return false;
        list->items = new_items;
        list->cap = new_cap;
    }
    
    list->items[list->count++] = node;
    return true;
}

bucc_node_t* bucc_ast_module_new(bucc_source_span_t span) {
    bucc_node_t* node = bucc_node_new(NODE_MODULE, span);
    if (!node) return NULL;
    bucc_node_list_init(&node->data.module.metadata);
    bucc_node_list_init(&node->data.module.globals);
    bucc_node_list_init(&node->data.module.procedures);
    return node;
}

bucc_node_t* bucc_ast_meta_program(bucc_source_span_t span, const char* name) {
    bucc_node_t* node = bucc_node_new(NODE_META_PROGRAM, span);
    if (!node) return NULL;
    node->data.meta_string.value = name ? strdup(name) : NULL;
    return node;
}

bucc_node_t* bucc_ast_meta_version(bucc_source_span_t span, const char* version) {
    bucc_node_t* node = bucc_node_new(NODE_META_VERSION, span);
    if (!node) return NULL;
    node->data.meta_string.value = version ? strdup(version) : NULL;
    return node;
}

bucc_node_t* bucc_ast_meta_author(bucc_source_span_t span, const char* author) {
    bucc_node_t* node = bucc_node_new(NODE_META_AUTHOR, span);
    if (!node) return NULL;
    node->data.meta_string.value = author ? strdup(author) : NULL;
    return node;
}

bucc_node_t* bucc_ast_meta_description(bucc_source_span_t span, const char* desc) {
    bucc_node_t* node = bucc_node_new(NODE_META_DESCRIPTION, span);
    if (!node) return NULL;
    node->data.meta_string.value = desc ? strdup(desc) : NULL;
    return node;
}

bucc_node_t* bucc_ast_meta_capability(bucc_source_span_t span, const char* cap) {
    bucc_node_t* node = bucc_node_new(NODE_META_CAPABILITY, span);
    if (!node) return NULL;
    node->data.meta_string.value = cap ? strdup(cap) : NULL;
    return node;
}

bucc_node_t* bucc_ast_meta_option(bucc_source_span_t span, const char* name, const char* value) {
    bucc_node_t* node = bucc_node_new(NODE_META_OPTION, span);
    if (!node) return NULL;
    node->data.meta_option.name = name ? strdup(name) : NULL;
    node->data.meta_option.value = value ? strdup(value) : NULL;
    return node;
}

bucc_node_t* bucc_ast_meta_dataset(bucc_source_span_t span, const char* name) {
    bucc_node_t* node = bucc_node_new(NODE_META_DATASET, span);
    if (!node) return NULL;
    node->data.meta_dataset.name = name ? strdup(name) : NULL;
    bucc_node_list_init(&node->data.meta_dataset.fields);
    return node;
}

bucc_node_t* bucc_ast_var_decl(bucc_source_span_t span, const char* name,
                               bucc_node_t* type_ref, bucc_node_t* init, bool is_global) {
    bucc_node_t* node = bucc_node_new(NODE_VAR_DECL, span);
    if (!node) return NULL;
    node->data.var_decl.name = name ? strdup(name) : NULL;
    node->data.var_decl.type_ref = type_ref;
    node->data.var_decl.initializer = init;
    node->data.var_decl.is_global = is_global;
    return node;
}

bucc_node_t* bucc_ast_proc_decl(bucc_source_span_t span, const char* name,
                                bucc_node_t* body, bool is_handler) {
    bucc_node_t* node = bucc_node_new(NODE_PROC_DECL, span);
    if (!node) return NULL;
    node->data.proc_decl.name = name ? strdup(name) : NULL;
    bucc_node_list_init(&node->data.proc_decl.params);
    node->data.proc_decl.body = body;
    node->data.proc_decl.is_handler = is_handler;
    return node;
}

bucc_node_t* bucc_ast_func_decl(bucc_source_span_t span, const char* name,
                                bucc_node_t* return_type, bucc_node_t* body) {
    bucc_node_t* node = bucc_node_new(NODE_FUNC_DECL, span);
    if (!node) return NULL;
    node->data.func_decl.name = name ? strdup(name) : NULL;
    bucc_node_list_init(&node->data.func_decl.params);
    node->data.func_decl.return_type = return_type;
    node->data.func_decl.body = body;
    return node;
}

bucc_node_t* bucc_ast_param_decl(bucc_source_span_t span, const char* name,
                                 bucc_node_t* type_ref) {
    bucc_node_t* node = bucc_node_new(NODE_PARAM_DECL, span);
    if (!node) return NULL;
    node->data.param_decl.name = name ? strdup(name) : NULL;
    node->data.param_decl.type_ref = type_ref;
    return node;
}

bucc_node_t* bucc_ast_type_scalar(bucc_source_span_t span, bucc_scalar_type_t scalar) {
    bucc_node_t* node = bucc_node_new(NODE_TYPE_SCALAR, span);
    if (!node) return NULL;
    node->data.type_scalar.scalar_type = scalar;
    return node;
}

bucc_node_t* bucc_ast_type_array(bucc_source_span_t span, bucc_node_t* element_type) {
    bucc_node_t* node = bucc_node_new(NODE_TYPE_ARRAY, span);
    if (!node) return NULL;
    node->data.type_array.element_type = element_type;
    return node;
}

bucc_node_t* bucc_ast_type_map(bucc_source_span_t span, bucc_node_t* value_type) {
    bucc_node_t* node = bucc_node_new(NODE_TYPE_MAP, span);
    if (!node) return NULL;
    node->data.type_map.value_type = value_type;
    return node;
}

bucc_node_t* bucc_ast_block(bucc_source_span_t span) {
    bucc_node_t* node = bucc_node_new(NODE_STMT_BLOCK, span);
    if (!node) return NULL;
    bucc_node_list_init(&node->data.block.stmts);
    return node;
}

bucc_node_t* bucc_ast_stmt_var_decl(bucc_source_span_t span, bucc_node_t* decl) {
    bucc_node_t* node = bucc_node_new(NODE_STMT_VAR_DECL, span);
    if (!node) return NULL;
    if (decl && decl->kind == NODE_VAR_DECL) {
        node->data.var_decl = decl->data.var_decl;
        decl->data.var_decl.name = NULL;
        decl->data.var_decl.type_ref = NULL;
        decl->data.var_decl.initializer = NULL;
        free(decl);
    }
    return node;
}

bucc_node_t* bucc_ast_stmt_assign(bucc_source_span_t span, bucc_node_t* target, bucc_node_t* value) {
    bucc_node_t* node = bucc_node_new(NODE_STMT_ASSIGN, span);
    if (!node) return NULL;
    node->data.assign.target = target;
    node->data.assign.value = value;
    return node;
}

bucc_node_t* bucc_ast_stmt_expr(bucc_source_span_t span, bucc_node_t* expr) {
    bucc_node_t* node = bucc_node_new(NODE_STMT_EXPR, span);
    if (!node) return NULL;
    node->data.expr_stmt.expr = expr;
    return node;
}

bucc_node_t* bucc_ast_stmt_if(bucc_source_span_t span, bucc_node_t* cond, bucc_node_t* then_block) {
    bucc_node_t* node = bucc_node_new(NODE_STMT_IF, span);
    if (!node) return NULL;
    node->data.if_stmt.condition = cond;
    node->data.if_stmt.then_block = then_block;
    bucc_node_list_init(&node->data.if_stmt.elseif_clauses);
    node->data.if_stmt.else_block = NULL;
    return node;
}

bucc_node_t* bucc_ast_stmt_select(bucc_source_span_t span, bucc_node_t* expr) {
    bucc_node_t* node = bucc_node_new(NODE_STMT_SELECT, span);
    if (!node) return NULL;
    node->data.select_stmt.expr = expr;
    bucc_node_list_init(&node->data.select_stmt.cases);
    node->data.select_stmt.else_case = NULL;
    return node;
}

bucc_node_t* bucc_ast_stmt_case(bucc_source_span_t span, bucc_node_t* body, bool is_else) {
    bucc_node_t* node = bucc_node_new(NODE_STMT_CASE, span);
    if (!node) return NULL;
    bucc_node_list_init(&node->data.case_clause.values);
    node->data.case_clause.body = body;
    node->data.case_clause.is_else = is_else;
    return node;
}

bucc_node_t* bucc_ast_stmt_while(bucc_source_span_t span, bucc_node_t* cond, bucc_node_t* body) {
    bucc_node_t* node = bucc_node_new(NODE_STMT_WHILE, span);
    if (!node) return NULL;
    node->data.while_stmt.condition = cond;
    node->data.while_stmt.body = body;
    return node;
}

bucc_node_t* bucc_ast_stmt_do_loop(bucc_source_span_t span, bucc_node_t* body,
                                   bucc_node_t* cond, bucc_loop_kind_t kind) {
    bucc_node_t* node = bucc_node_new(NODE_STMT_DO_LOOP, span);
    if (!node) return NULL;
    node->data.do_loop.body = body;
    node->data.do_loop.condition = cond;
    node->data.do_loop.loop_kind = kind;
    return node;
}

bucc_node_t* bucc_ast_stmt_for(bucc_source_span_t span, const char* var,
                               bucc_node_t* start, bucc_node_t* end, bucc_node_t* step,
                               bucc_node_t* body) {
    bucc_node_t* node = bucc_node_new(NODE_STMT_FOR, span);
    if (!node) return NULL;
    node->data.for_stmt.var_name = var ? strdup(var) : NULL;
    node->data.for_stmt.start_expr = start;
    node->data.for_stmt.end_expr = end;
    node->data.for_stmt.step_expr = step;
    node->data.for_stmt.body = body;
    return node;
}

bucc_node_t* bucc_ast_stmt_try_catch(bucc_source_span_t span, bucc_node_t* try_block,
                                     const char* error_var, bucc_node_t* catch_block) {
    bucc_node_t* node = bucc_node_new(NODE_STMT_TRY_CATCH, span);
    if (!node) return NULL;
    node->data.try_catch.try_block = try_block;
    node->data.try_catch.error_var = error_var ? strdup(error_var) : NULL;
    node->data.try_catch.catch_block = catch_block;
    return node;
}

bucc_node_t* bucc_ast_stmt_return(bucc_source_span_t span, bucc_node_t* value) {
    bucc_node_t* node = bucc_node_new(NODE_STMT_RETURN, span);
    if (!node) return NULL;
    node->data.return_stmt.value = value;
    return node;
}

bucc_node_t* bucc_ast_stmt_exit(bucc_source_span_t span, bucc_exit_kind_t kind) {
    bucc_node_t* node = bucc_node_new(NODE_STMT_EXIT, span);
    if (!node) return NULL;
    node->data.exit_stmt.exit_kind = kind;
    return node;
}

bucc_node_t* bucc_ast_stmt_halt(bucc_source_span_t span) {
    return bucc_node_new(NODE_STMT_HALT, span);
}

bucc_node_t* bucc_ast_stmt_throw(bucc_source_span_t span, bucc_node_t* expr) {
    bucc_node_t* node = bucc_node_new(NODE_STMT_THROW, span);
    if (!node) return NULL;
    node->data.throw_stmt.expr = expr;
    return node;
}

bucc_node_t* bucc_ast_stmt_chain(bucc_source_span_t span, const char* target, bucc_node_t* args) {
    bucc_node_t* node = bucc_node_new(NODE_STMT_CHAIN, span);
    if (!node) return NULL;
    node->data.chain_stmt.target = target ? strdup(target) : NULL;
    node->data.chain_stmt.args = args;
    return node;
}

bucc_node_t* bucc_ast_stmt_on_call(bucc_source_span_t span, bucc_node_t* selector) {
    bucc_node_t* node = bucc_node_new(NODE_STMT_ON_CALL, span);
    if (!node) return NULL;
    node->data.on_call.selector = selector;
    bucc_node_list_init(&node->data.on_call.targets);
    return node;
}

bucc_node_t* bucc_ast_expr_ident(bucc_source_span_t span, const char* name) {
    bucc_node_t* node = bucc_node_new(NODE_EXPR_IDENT, span);
    if (!node) return NULL;
    node->data.ident.name = name ? strdup(name) : NULL;
    return node;
}

bucc_node_t* bucc_ast_expr_literal_null(bucc_source_span_t span) {
    bucc_node_t* node = bucc_node_new(NODE_EXPR_LITERAL, span);
    if (!node) return NULL;
    node->data.literal.value = BUCC_NULL_VAL;
    return node;
}

bucc_node_t* bucc_ast_expr_literal_bool(bucc_source_span_t span, bool value) {
    bucc_node_t* node = bucc_node_new(NODE_EXPR_LITERAL, span);
    if (!node) return NULL;
    node->data.literal.value = BUCC_BOOL_VAL(value);
    return node;
}

bucc_node_t* bucc_ast_expr_literal_i64(bucc_source_span_t span, int64_t value) {
    bucc_node_t* node = bucc_node_new(NODE_EXPR_LITERAL, span);
    if (!node) return NULL;
    node->data.literal.value = BUCC_I64_VAL(value);
    return node;
}

bucc_node_t* bucc_ast_expr_literal_f64(bucc_source_span_t span, double value) {
    bucc_node_t* node = bucc_node_new(NODE_EXPR_LITERAL, span);
    if (!node) return NULL;
    node->data.literal.value = BUCC_F64_VAL(value);
    return node;
}

bucc_node_t* bucc_ast_expr_literal_string(bucc_source_span_t span, const char* value, size_t len) {
    bucc_node_t* node = bucc_node_new(NODE_EXPR_LITERAL, span);
    if (!node) return NULL;
    bucc_string_t* str = bucc_string_new(value, len);
    node->data.literal.value = BUCC_STRING_VAL(str);
    return node;
}

bucc_node_t* bucc_ast_expr_literal_date(bucc_source_span_t span, bucc_date_t date) {
    bucc_node_t* node = bucc_node_new(NODE_EXPR_LITERAL, span);
    if (!node) return NULL;
    node->data.literal.value = BUCC_DATE_VAL(date);
    return node;
}

bucc_node_t* bucc_ast_expr_literal_datetime(bucc_source_span_t span, bucc_datetime_t dt) {
    bucc_node_t* node = bucc_node_new(NODE_EXPR_LITERAL, span);
    if (!node) return NULL;
    node->data.literal.value = BUCC_DATETIME_VAL(dt);
    return node;
}

bucc_node_t* bucc_ast_expr_unary(bucc_source_span_t span, bucc_unary_op_t op, bucc_node_t* operand) {
    bucc_node_t* node = bucc_node_new(NODE_EXPR_UNARY, span);
    if (!node) return NULL;
    node->data.unary.op = op;
    node->data.unary.operand = operand;
    return node;
}

bucc_node_t* bucc_ast_expr_binary(bucc_source_span_t span, bucc_binary_op_t op,
                                  bucc_node_t* left, bucc_node_t* right) {
    bucc_node_t* node = bucc_node_new(NODE_EXPR_BINARY, span);
    if (!node) return NULL;
    node->data.binary.op = op;
    node->data.binary.left = left;
    node->data.binary.right = right;
    return node;
}

bucc_node_t* bucc_ast_expr_call(bucc_source_span_t span, bucc_node_t* callee) {
    bucc_node_t* node = bucc_node_new(NODE_EXPR_CALL, span);
    if (!node) return NULL;
    node->data.call.callee = callee;
    bucc_node_list_init(&node->data.call.args);
    return node;
}

bucc_node_t* bucc_ast_expr_member(bucc_source_span_t span, bucc_node_t* object, const char* member) {
    bucc_node_t* node = bucc_node_new(NODE_EXPR_MEMBER, span);
    if (!node) return NULL;
    node->data.member.object = object;
    node->data.member.member = member ? strdup(member) : NULL;
    return node;
}

bucc_node_t* bucc_ast_expr_index(bucc_source_span_t span, bucc_node_t* object, bucc_node_t* index) {
    bucc_node_t* node = bucc_node_new(NODE_EXPR_INDEX, span);
    if (!node) return NULL;
    node->data.index_expr.object = object;
    node->data.index_expr.index = index;
    return node;
}

bucc_node_t* bucc_ast_expr_array_lit(bucc_source_span_t span) {
    bucc_node_t* node = bucc_node_new(NODE_EXPR_ARRAY_LIT, span);
    if (!node) return NULL;
    bucc_node_list_init(&node->data.array_lit.elements);
    return node;
}

bucc_node_t* bucc_ast_expr_map_lit(bucc_source_span_t span) {
    bucc_node_t* node = bucc_node_new(NODE_EXPR_MAP_LIT, span);
    if (!node) return NULL;
    bucc_node_list_init(&node->data.map_lit.pairs);
    return node;
}

bucc_node_t* bucc_ast_expr_map_pair(bucc_source_span_t span, const char* key, bucc_node_t* value) {
    bucc_node_t* node = bucc_node_new(NODE_EXPR_MAP_PAIR, span);
    if (!node) return NULL;
    node->data.map_pair.key = key ? strdup(key) : NULL;
    node->data.map_pair.value = value;
    return node;
}

bucc_node_t* bucc_ast_expr_qualified(bucc_source_span_t span, const char* ns, const char* name) {
    bucc_node_t* node = bucc_node_new(NODE_EXPR_QUALIFIED, span);
    if (!node) return NULL;
    node->data.qualified.ns = ns ? strdup(ns) : NULL;
    node->data.qualified.name = name ? strdup(name) : NULL;
    return node;
}

const char* bucc_node_kind_name(bucc_node_kind_t kind) {
    switch (kind) {
        case NODE_MODULE:           return "Module";
        case NODE_META_PROGRAM:     return "MetaProgram";
        case NODE_META_VERSION:     return "MetaVersion";
        case NODE_META_AUTHOR:      return "MetaAuthor";
        case NODE_META_DESCRIPTION: return "MetaDescription";
        case NODE_META_CAPABILITY:  return "MetaCapability";
        case NODE_META_OPTION:      return "MetaOption";
        case NODE_META_DATASET:     return "MetaDataset";
        case NODE_VAR_DECL:         return "VarDecl";
        case NODE_PROC_DECL:        return "ProcDecl";
        case NODE_FUNC_DECL:        return "FuncDecl";
        case NODE_PARAM_DECL:       return "ParamDecl";
        case NODE_TYPE_SCALAR:      return "TypeScalar";
        case NODE_TYPE_ARRAY:       return "TypeArray";
        case NODE_TYPE_MAP:         return "TypeMap";
        case NODE_STMT_BLOCK:       return "Block";
        case NODE_STMT_VAR_DECL:    return "StmtVarDecl";
        case NODE_STMT_ASSIGN:      return "Assign";
        case NODE_STMT_EXPR:        return "ExprStmt";
        case NODE_STMT_IF:          return "If";
        case NODE_STMT_SELECT:      return "Select";
        case NODE_STMT_CASE:        return "Case";
        case NODE_STMT_WHILE:       return "While";
        case NODE_STMT_DO_LOOP:     return "DoLoop";
        case NODE_STMT_FOR:         return "For";
        case NODE_STMT_TRY_CATCH:   return "TryCatch";
        case NODE_STMT_RETURN:      return "Return";
        case NODE_STMT_EXIT:        return "Exit";
        case NODE_STMT_HALT:        return "Halt";
        case NODE_STMT_THROW:       return "Throw";
        case NODE_STMT_CHAIN:       return "Chain";
        case NODE_STMT_ON_CALL:     return "OnCall";
        case NODE_EXPR_IDENT:       return "Ident";
        case NODE_EXPR_LITERAL:     return "Literal";
        case NODE_EXPR_UNARY:       return "Unary";
        case NODE_EXPR_BINARY:      return "Binary";
        case NODE_EXPR_CALL:        return "Call";
        case NODE_EXPR_MEMBER:      return "Member";
        case NODE_EXPR_INDEX:       return "Index";
        case NODE_EXPR_ARRAY_LIT:   return "ArrayLit";
        case NODE_EXPR_MAP_LIT:     return "MapLit";
        case NODE_EXPR_MAP_PAIR:    return "MapPair";
        case NODE_EXPR_QUALIFIED:   return "Qualified";
        case NODE_ERROR:            return "Error";
        default:                    return "Unknown";
    }
}

const char* bucc_scalar_type_name(bucc_scalar_type_t type) {
    switch (type) {
        case SCALAR_INTEGER:  return "INTEGER";
        case SCALAR_DOUBLE:   return "DOUBLE";
        case SCALAR_BOOLEAN:  return "BOOLEAN";
        case SCALAR_STRING:   return "STRING";
        case SCALAR_DATE:     return "DATE";
        case SCALAR_DATETIME: return "DATETIME";
        default:              return "UNKNOWN";
    }
}

const char* bucc_unary_op_name(bucc_unary_op_t op) {
    switch (op) {
        case UNARY_NEG: return "-";
        case UNARY_NOT: return "NOT";
        default:        return "?";
    }
}

const char* bucc_binary_op_name(bucc_binary_op_t op) {
    switch (op) {
        case BINARY_ADD:    return "+";
        case BINARY_SUB:    return "-";
        case BINARY_MUL:    return "*";
        case BINARY_DIV:    return "/";
        case BINARY_MOD:    return "MOD";
        case BINARY_EQ:     return "=";
        case BINARY_NE:     return "<>";
        case BINARY_LT:     return "<";
        case BINARY_LE:     return "<=";
        case BINARY_GT:     return ">";
        case BINARY_GE:     return ">=";
        case BINARY_AND:    return "AND";
        case BINARY_OR:     return "OR";
        case BINARY_CONCAT: return "+";
        default:            return "?";
    }
}

static void print_indent(FILE* out, int indent) {
    for (int i = 0; i < indent; i++) {
        fprintf(out, "  ");
    }
}

void bucc_ast_print(bucc_node_t* node, FILE* out, int indent) {
    if (!node || !out) return;
    
    print_indent(out, indent);
    fprintf(out, "%s", bucc_node_kind_name(node->kind));
    
    switch (node->kind) {
        case NODE_MODULE:
            fprintf(out, "\n");
            for (size_t i = 0; i < node->data.module.metadata.count; i++) {
                bucc_ast_print(node->data.module.metadata.items[i], out, indent + 1);
            }
            for (size_t i = 0; i < node->data.module.globals.count; i++) {
                bucc_ast_print(node->data.module.globals.items[i], out, indent + 1);
            }
            for (size_t i = 0; i < node->data.module.procedures.count; i++) {
                bucc_ast_print(node->data.module.procedures.items[i], out, indent + 1);
            }
            break;
            
        case NODE_META_PROGRAM:
        case NODE_META_VERSION:
        case NODE_META_AUTHOR:
        case NODE_META_DESCRIPTION:
        case NODE_META_CAPABILITY:
            fprintf(out, " \"%s\"\n", node->data.meta_string.value ? node->data.meta_string.value : "");
            break;
            
        case NODE_VAR_DECL:
        case NODE_STMT_VAR_DECL:
            fprintf(out, " %s\n", node->data.var_decl.name ? node->data.var_decl.name : "?");
            if (node->data.var_decl.type_ref) {
                bucc_ast_print(node->data.var_decl.type_ref, out, indent + 1);
            }
            if (node->data.var_decl.initializer) {
                bucc_ast_print(node->data.var_decl.initializer, out, indent + 1);
            }
            break;
            
        case NODE_PROC_DECL:
            fprintf(out, " %s\n", node->data.proc_decl.name ? node->data.proc_decl.name : "?");
            for (size_t i = 0; i < node->data.proc_decl.params.count; i++) {
                bucc_ast_print(node->data.proc_decl.params.items[i], out, indent + 1);
            }
            if (node->data.proc_decl.body) {
                bucc_ast_print(node->data.proc_decl.body, out, indent + 1);
            }
            break;
            
        case NODE_FUNC_DECL:
            fprintf(out, " %s\n", node->data.func_decl.name ? node->data.func_decl.name : "?");
            for (size_t i = 0; i < node->data.func_decl.params.count; i++) {
                bucc_ast_print(node->data.func_decl.params.items[i], out, indent + 1);
            }
            if (node->data.func_decl.return_type) {
                bucc_ast_print(node->data.func_decl.return_type, out, indent + 1);
            }
            if (node->data.func_decl.body) {
                bucc_ast_print(node->data.func_decl.body, out, indent + 1);
            }
            break;
            
        case NODE_PARAM_DECL:
            fprintf(out, " %s\n", node->data.param_decl.name ? node->data.param_decl.name : "?");
            if (node->data.param_decl.type_ref) {
                bucc_ast_print(node->data.param_decl.type_ref, out, indent + 1);
            }
            break;
            
        case NODE_TYPE_SCALAR:
            fprintf(out, " %s\n", bucc_scalar_type_name(node->data.type_scalar.scalar_type));
            break;
            
        case NODE_TYPE_ARRAY:
            fprintf(out, "\n");
            if (node->data.type_array.element_type) {
                bucc_ast_print(node->data.type_array.element_type, out, indent + 1);
            }
            break;
            
        case NODE_TYPE_MAP:
            fprintf(out, "\n");
            if (node->data.type_map.value_type) {
                bucc_ast_print(node->data.type_map.value_type, out, indent + 1);
            }
            break;
            
        case NODE_STMT_BLOCK:
            fprintf(out, "\n");
            for (size_t i = 0; i < node->data.block.stmts.count; i++) {
                bucc_ast_print(node->data.block.stmts.items[i], out, indent + 1);
            }
            break;
            
        case NODE_EXPR_IDENT:
            fprintf(out, " %s\n", node->data.ident.name ? node->data.ident.name : "?");
            break;
            
        case NODE_EXPR_LITERAL: {
            bucc_string_t* s = bucc_value_to_string(&node->data.literal.value);
            fprintf(out, " %s\n", s ? s->data : "?");
            bucc_string_release(s);
            break;
        }
            
        case NODE_EXPR_QUALIFIED:
            fprintf(out, " %s.%s\n",
                    node->data.qualified.ns ? node->data.qualified.ns : "?",
                    node->data.qualified.name ? node->data.qualified.name : "?");
            break;
            
        default:
            fprintf(out, "\n");
            break;
    }
}
