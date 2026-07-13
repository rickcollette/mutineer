/*
 * emit.c - Buccaneer bytecode emitter implementation
 *
 * Part of the Buccaneer language implementation for Mutineer BBS.
 */

#define _POSIX_C_SOURCE 200809L
#include "include/bucc_emit.h"
#include "include/bucc_module.h"
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

bucc_emitter_t* bucc_emitter_new(bucc_semantic_t* sem, bucc_diag_t* diag) {
    bucc_emitter_t* emit = calloc(1, sizeof(bucc_emitter_t));
    if (!emit) return NULL;
    
    emit->diag = diag;
    emit->sem = sem;
    
    emit->code_cap = 4096;
    emit->code = malloc(emit->code_cap);
    if (!emit->code) {
        free(emit);
        return NULL;
    }
    
    emit->label_cap = 256;
    emit->labels = calloc(emit->label_cap, sizeof(bucc_label_t*));
    
    return emit;
}

void bucc_emitter_free(bucc_emitter_t* emit) {
    if (!emit) return;
    
    free(emit->code);
    
    bucc_const_entry_t* c = emit->constants;
    while (c) {
        bucc_const_entry_t* next = c->next;
        bucc_value_release(&c->value);
        free(c);
        c = next;
    }
    
    bucc_string_entry_t* s = emit->strings;
    while (s) {
        bucc_string_entry_t* next = s->next;
        free(s->str);
        free(s);
        s = next;
    }
    
    bucc_proc_entry_t* p = emit->procedures;
    while (p) {
        bucc_proc_entry_t* next = p->next;
        free(p->name);
        free(p);
        p = next;
    }
    
    bucc_host_import_t* i = emit->imports;
    while (i) {
        bucc_host_import_t* next = i->next;
        free(i->ns);
        free(i->fn);
        free(i);
        i = next;
    }
    
    for (uint32_t j = 0; j < emit->label_cap; j++) {
        bucc_label_t* l = emit->labels[j];
        while (l) {
            bucc_label_t* next = l->next;
            free(l);
            l = next;
        }
    }
    free(emit->labels);
    
    bucc_patch_t* patch = emit->patches;
    while (patch) {
        bucc_patch_t* next = patch->next;
        free(patch);
        patch = next;
    }
    
    bucc_debug_line_info_t* dbg = emit->debug_lines;
    while (dbg) {
        bucc_debug_line_info_t* next = dbg->next;
        free(dbg);
        dbg = next;
    }
    
    free((void*)emit->source_file);
    free(emit);
}

static void ensure_code_space(bucc_emitter_t* emit, size_t needed) {
    if (emit->code_len + needed <= emit->code_cap) return;
    
    size_t new_cap = emit->code_cap * 2;
    while (new_cap < emit->code_len + needed) {
        new_cap *= 2;
    }
    
    uint8_t* new_code = realloc(emit->code, new_cap);
    if (new_code) {
        emit->code = new_code;
        emit->code_cap = new_cap;
    }
}

void bucc_emit_byte(bucc_emitter_t* emit, uint8_t byte) {
    ensure_code_space(emit, 1);
    emit->code[emit->code_len++] = byte;
}

void bucc_emit_u16(bucc_emitter_t* emit, uint16_t val) {
    ensure_code_space(emit, 2);
    emit->code[emit->code_len++] = (val >> 8) & 0xFF;
    emit->code[emit->code_len++] = val & 0xFF;
}

void bucc_emit_u32(bucc_emitter_t* emit, uint32_t val) {
    ensure_code_space(emit, 4);
    emit->code[emit->code_len++] = (val >> 24) & 0xFF;
    emit->code[emit->code_len++] = (val >> 16) & 0xFF;
    emit->code[emit->code_len++] = (val >> 8) & 0xFF;
    emit->code[emit->code_len++] = val & 0xFF;
}

void bucc_emit_i32(bucc_emitter_t* emit, int32_t val) {
    bucc_emit_u32(emit, (uint32_t)val);
}

void bucc_emit_op(bucc_emitter_t* emit, bucc_opcode_t op) {
    bucc_emit_byte(emit, (uint8_t)op);
}

void bucc_emit_op_u8(bucc_emitter_t* emit, bucc_opcode_t op, uint8_t arg) {
    bucc_emit_byte(emit, (uint8_t)op);
    bucc_emit_byte(emit, arg);
}

void bucc_emit_op_u16(bucc_emitter_t* emit, bucc_opcode_t op, uint16_t arg) {
    bucc_emit_byte(emit, (uint8_t)op);
    bucc_emit_u16(emit, arg);
}

void bucc_emit_op_u32(bucc_emitter_t* emit, bucc_opcode_t op, uint32_t arg) {
    bucc_emit_byte(emit, (uint8_t)op);
    bucc_emit_u32(emit, arg);
}

uint32_t bucc_emit_string(bucc_emitter_t* emit, const char* str) {
    if (!str) str = "";
    
    for (bucc_string_entry_t* s = emit->strings; s; s = s->next) {
        if (strcmp(s->str, str) == 0) {
            return s->index;
        }
    }
    
    bucc_string_entry_t* entry = calloc(1, sizeof(bucc_string_entry_t));
    if (!entry) return 0;
    
    entry->str = strdup(str);
    entry->index = emit->string_count++;
    entry->next = emit->strings;
    emit->strings = entry;
    
    return entry->index;
}

uint32_t bucc_emit_constant_i64(bucc_emitter_t* emit, int64_t val) {
    for (bucc_const_entry_t* c = emit->constants; c; c = c->next) {
        if (c->value.kind == BUCC_VAL_I64 && c->value.as.i64 == val) {
            return c->index;
        }
    }
    
    bucc_const_entry_t* entry = calloc(1, sizeof(bucc_const_entry_t));
    if (!entry) return 0;
    
    entry->value = BUCC_I64_VAL(val);
    entry->index = emit->const_count++;
    entry->next = emit->constants;
    emit->constants = entry;
    
    return entry->index;
}

uint32_t bucc_emit_constant_f64(bucc_emitter_t* emit, double val) {
    for (bucc_const_entry_t* c = emit->constants; c; c = c->next) {
        if (c->value.kind == BUCC_VAL_F64 && c->value.as.f64 == val) {
            return c->index;
        }
    }
    
    bucc_const_entry_t* entry = calloc(1, sizeof(bucc_const_entry_t));
    if (!entry) return 0;
    
    entry->value = BUCC_F64_VAL(val);
    entry->index = emit->const_count++;
    entry->next = emit->constants;
    emit->constants = entry;
    
    return entry->index;
}

uint32_t bucc_emit_label(bucc_emitter_t* emit) {
    if (emit->next_label >= emit->label_cap) {
        uint32_t new_cap = emit->label_cap * 2;
        bucc_label_t** new_labels = realloc(emit->labels, new_cap * sizeof(bucc_label_t*));
        if (!new_labels) return 0;
        memset(new_labels + emit->label_cap, 0, (new_cap - emit->label_cap) * sizeof(bucc_label_t*));
        emit->labels = new_labels;
        emit->label_cap = new_cap;
    }
    return emit->next_label++;
}

void bucc_emit_label_here(bucc_emitter_t* emit, uint32_t label) {
    if (label >= emit->label_cap) return;
    
    bucc_label_t* l = calloc(1, sizeof(bucc_label_t));
    if (!l) return;
    
    l->offset = (uint32_t)emit->code_len;
    l->next = emit->labels[label];
    emit->labels[label] = l;
}

void bucc_emit_jump(bucc_emitter_t* emit, bucc_opcode_t op, uint32_t label) {
    bucc_emit_byte(emit, (uint8_t)op);
    
    bucc_patch_t* patch = calloc(1, sizeof(bucc_patch_t));
    if (patch) {
        patch->offset = (uint32_t)emit->code_len;
        patch->label_id = label;
        patch->next = emit->patches;
        emit->patches = patch;
    }
    
    bucc_emit_i32(emit, 0);
}

void bucc_emit_resolve_patches(bucc_emitter_t* emit) {
    for (bucc_patch_t* patch = emit->patches; patch; patch = patch->next) {
        if (patch->label_id < emit->label_cap && emit->labels[patch->label_id]) {
            uint32_t target = emit->labels[patch->label_id]->offset;
            int32_t rel = (int32_t)target - (int32_t)(patch->offset + 4);
            
            emit->code[patch->offset + 0] = (rel >> 24) & 0xFF;
            emit->code[patch->offset + 1] = (rel >> 16) & 0xFF;
            emit->code[patch->offset + 2] = (rel >> 8) & 0xFF;
            emit->code[patch->offset + 3] = rel & 0xFF;
        }
    }
}

uint32_t bucc_emit_host_import(bucc_emitter_t* emit, const char* ns, const char* fn) {
    for (bucc_host_import_t* i = emit->imports; i; i = i->next) {
        if (strcasecmp_local(i->ns, ns) == 0 && strcasecmp_local(i->fn, fn) == 0) {
            return i->id;
        }
    }
    
    bucc_host_import_t* entry = calloc(1, sizeof(bucc_host_import_t));
    if (!entry) return 0;
    
    entry->ns = strdup(ns);
    entry->fn = strdup(fn);
    entry->id = emit->import_count++;
    entry->next = emit->imports;
    emit->imports = entry;
    
    return entry->id;
}

static void emit_expr(bucc_emitter_t* emit, bucc_node_t* expr);
static void emit_stmt(bucc_emitter_t* emit, bucc_node_t* stmt);
static void emit_block(bucc_emitter_t* emit, bucc_node_t* block);

static bucc_symbol_t* lookup_symbol(bucc_emitter_t* emit, const char* name) {
    return bucc_scope_lookup(emit->sem->current_scope, name);
}

static bool resolve_on_call_base(bucc_emitter_t* emit, bucc_node_t* stmt,
                                 uint16_t* base_out, uint8_t* count_out) {
    if (!emit || !stmt || !base_out || !count_out) return false;
    size_t count = stmt->data.on_call.targets.count;
    if (count == 0 || count > UINT8_MAX) {
        bucc_diag_error(emit->diag, stmt->span, BUCC_ERR_ARITY_MISMATCH,
                        "ON CALL requires 1 to 255 targets");
        return false;
    }

    uint32_t ids[UINT8_MAX];
    for (size_t i = 0; i < count; i++) {
        bucc_node_t* target = stmt->data.on_call.targets.items[i];
        if (!target || target->kind != NODE_EXPR_IDENT) {
            bucc_diag_error(emit->diag, stmt->span, BUCC_ERR_UNDEFINED_SYM,
                            "ON CALL target must be a procedure name");
            return false;
        }
        bucc_symbol_t* sym = lookup_symbol(emit, target->data.ident.name);
        if (!sym || (sym->kind != SYM_PROCEDURE && sym->kind != SYM_FUNCTION)) {
            bucc_diag_error(emit->diag, target->span, BUCC_ERR_UNDEFINED_SYM,
                            "ON CALL target '%s' is not a procedure",
                            target->data.ident.name ? target->data.ident.name : "");
            return false;
        }
        ids[i] = sym->slot;
    }

    uint32_t base = ids[0];
    for (size_t i = 1; i < count; i++) {
        if (ids[i] != base + i) {
            bucc_diag_error(emit->diag, stmt->span, BUCC_ERR_INVALID_HANDLER,
                            "ON CALL targets must be contiguous procedures in declaration order");
            return false;
        }
    }
    if (base > UINT16_MAX) {
        bucc_diag_error(emit->diag, stmt->span, BUCC_ERR_INVALID_HANDLER,
                        "ON CALL target base is out of bytecode range");
        return false;
    }

    *base_out = (uint16_t)base;
    *count_out = (uint8_t)count;
    return true;
}

static void emit_expr(bucc_emitter_t* emit, bucc_node_t* expr) {
    if (!emit || !expr) return;
    
    switch (expr->kind) {
        case NODE_EXPR_LITERAL: {
            switch (expr->data.literal.value.kind) {
                case BUCC_VAL_NULL:
                    bucc_emit_op(emit, OP_PUSH_NULL);
                    break;
                case BUCC_VAL_BOOL:
                    bucc_emit_op(emit, expr->data.literal.value.as.b ? OP_PUSH_TRUE : OP_PUSH_FALSE);
                    break;
                case BUCC_VAL_I64: {
                    uint32_t idx = bucc_emit_constant_i64(emit, expr->data.literal.value.as.i64);
                    bucc_emit_op_u16(emit, OP_PUSH_I64, (uint16_t)idx);
                    break;
                }
                case BUCC_VAL_F64: {
                    uint32_t idx = bucc_emit_constant_f64(emit, expr->data.literal.value.as.f64);
                    bucc_emit_op_u16(emit, OP_PUSH_F64, (uint16_t)idx);
                    break;
                }
                case BUCC_VAL_STRING: {
                    bucc_string_t* s = expr->data.literal.value.as.str;
                    uint32_t idx = bucc_emit_string(emit, s ? s->data : "");
                    bucc_emit_op_u16(emit, OP_PUSH_STR, (uint16_t)idx);
                    break;
                }
                case BUCC_VAL_DATE: {
                    uint32_t idx = emit->const_count;
                    bucc_const_entry_t* entry = calloc(1, sizeof(bucc_const_entry_t));
                    if (entry) {
                        entry->value = expr->data.literal.value;
                        entry->index = emit->const_count++;
                        entry->next = emit->constants;
                        emit->constants = entry;
                    }
                    bucc_emit_op_u16(emit, OP_PUSH_DATE, (uint16_t)idx);
                    break;
                }
                case BUCC_VAL_DATETIME: {
                    uint32_t idx = emit->const_count;
                    bucc_const_entry_t* entry = calloc(1, sizeof(bucc_const_entry_t));
                    if (entry) {
                        entry->value = expr->data.literal.value;
                        entry->index = emit->const_count++;
                        entry->next = emit->constants;
                        emit->constants = entry;
                    }
                    bucc_emit_op_u16(emit, OP_PUSH_DATETIME, (uint16_t)idx);
                    break;
                }
                default:
                    bucc_emit_op(emit, OP_PUSH_NULL);
                    break;
            }
            break;
        }
        
        case NODE_EXPR_IDENT: {
            bucc_symbol_t* sym = lookup_symbol(emit, expr->data.ident.name);
            if (sym) {
                switch (sym->kind) {
                    case SYM_GLOBAL:
                        bucc_emit_op_u16(emit, OP_LOAD_GLOBAL, (uint16_t)sym->slot);
                        break;
                    case SYM_LOCAL:
                        bucc_emit_op_u16(emit, OP_LOAD_LOCAL, (uint16_t)sym->slot);
                        break;
                    case SYM_PARAM:
                        bucc_emit_op_u16(emit, OP_LOAD_ARG, (uint16_t)sym->slot);
                        break;
                    default:
                        bucc_emit_op(emit, OP_PUSH_NULL);
                        break;
                }
            } else {
                bucc_emit_op(emit, OP_PUSH_NULL);
            }
            break;
        }
        
        case NODE_EXPR_QUALIFIED: {
            uint32_t import_id = bucc_emit_host_import(emit,
                expr->data.qualified.ns, expr->data.qualified.name);
            bucc_emit_op_u16(emit, OP_CALL_HOST, (uint16_t)import_id);
            bucc_emit_byte(emit, 0);
            break;
        }
        
        case NODE_EXPR_CALL: {
            for (size_t i = 0; i < expr->data.call.args.count; i++) {
                emit_expr(emit, expr->data.call.args.items[i]);
            }
            
            bucc_node_t* callee = expr->data.call.callee;
            if (callee->kind == NODE_EXPR_QUALIFIED) {
                uint32_t import_id = bucc_emit_host_import(emit,
                    callee->data.qualified.ns, callee->data.qualified.name);
                bucc_emit_op_u16(emit, OP_CALL_HOST, (uint16_t)import_id);
                bucc_emit_byte(emit, (uint8_t)expr->data.call.args.count);
            } else if (callee->kind == NODE_EXPR_IDENT) {
                bucc_symbol_t* sym = lookup_symbol(emit, callee->data.ident.name);
                if (sym && (sym->kind == SYM_PROCEDURE || sym->kind == SYM_FUNCTION)) {
                    bucc_emit_op_u16(emit, OP_CALL, (uint16_t)sym->slot);
                    bucc_emit_byte(emit, (uint8_t)expr->data.call.args.count);
                } else {
                    bucc_emit_op(emit, OP_PUSH_NULL);
                }
            } else {
                emit_expr(emit, callee);
            }
            break;
        }
        
        case NODE_EXPR_UNARY: {
            emit_expr(emit, expr->data.unary.operand);
            switch (expr->data.unary.op) {
                case UNARY_NEG: bucc_emit_op(emit, OP_NEG); break;
                case UNARY_NOT: bucc_emit_op(emit, OP_NOT); break;
            }
            break;
        }
        
        case NODE_EXPR_BINARY: {
            emit_expr(emit, expr->data.binary.left);
            emit_expr(emit, expr->data.binary.right);
            
            switch (expr->data.binary.op) {
                case BINARY_ADD:    bucc_emit_op(emit, OP_ADD); break;
                case BINARY_SUB:    bucc_emit_op(emit, OP_SUB); break;
                case BINARY_MUL:    bucc_emit_op(emit, OP_MUL); break;
                case BINARY_DIV:    bucc_emit_op(emit, OP_DIV); break;
                case BINARY_MOD:    bucc_emit_op(emit, OP_MOD); break;
                case BINARY_EQ:     bucc_emit_op(emit, OP_EQ); break;
                case BINARY_NE:     bucc_emit_op(emit, OP_NE); break;
                case BINARY_LT:     bucc_emit_op(emit, OP_LT); break;
                case BINARY_LE:     bucc_emit_op(emit, OP_LE); break;
                case BINARY_GT:     bucc_emit_op(emit, OP_GT); break;
                case BINARY_GE:     bucc_emit_op(emit, OP_GE); break;
                case BINARY_AND:    bucc_emit_op(emit, OP_AND); break;
                case BINARY_OR:     bucc_emit_op(emit, OP_OR); break;
                case BINARY_CONCAT: bucc_emit_op(emit, OP_CONCAT); break;
            }
            break;
        }
        
        case NODE_EXPR_INDEX: {
            emit_expr(emit, expr->data.index_expr.object);
            emit_expr(emit, expr->data.index_expr.index);
            bucc_emit_op(emit, OP_ARRAY_GET);
            break;
        }
        
        case NODE_EXPR_MEMBER: {
            emit_expr(emit, expr->data.member.object);
            uint32_t str_idx = bucc_emit_string(emit, expr->data.member.member);
            bucc_emit_op_u16(emit, OP_PUSH_STR, (uint16_t)str_idx);
            bucc_emit_op(emit, OP_MAP_GET);
            break;
        }
        
        case NODE_EXPR_ARRAY_LIT: {
            for (size_t i = 0; i < expr->data.array_lit.elements.count; i++) {
                emit_expr(emit, expr->data.array_lit.elements.items[i]);
            }
            bucc_emit_op_u16(emit, OP_ARRAY_NEW, (uint16_t)expr->data.array_lit.elements.count);
            break;
        }
        
        case NODE_EXPR_MAP_LIT: {
            for (size_t i = 0; i < expr->data.map_lit.pairs.count; i++) {
                bucc_node_t* pair = expr->data.map_lit.pairs.items[i];
                if (pair->kind == NODE_EXPR_MAP_PAIR) {
                    uint32_t key_idx = bucc_emit_string(emit, pair->data.map_pair.key);
                    bucc_emit_op_u16(emit, OP_PUSH_STR, (uint16_t)key_idx);
                    emit_expr(emit, pair->data.map_pair.value);
                }
            }
            bucc_emit_op_u16(emit, OP_MAP_NEW, (uint16_t)expr->data.map_lit.pairs.count);
            break;
        }
        
        default:
            bucc_emit_op(emit, OP_PUSH_NULL);
            break;
    }
}

static void emit_lvalue_store(bucc_emitter_t* emit, bucc_node_t* target) {
    if (!target) return;
    
    switch (target->kind) {
        case NODE_EXPR_IDENT: {
            bucc_symbol_t* sym = lookup_symbol(emit, target->data.ident.name);
            if (sym) {
                switch (sym->kind) {
                    case SYM_GLOBAL:
                        bucc_emit_op_u16(emit, OP_STORE_GLOBAL, (uint16_t)sym->slot);
                        break;
                    case SYM_LOCAL:
                    case SYM_PARAM:
                        bucc_emit_op_u16(emit, OP_STORE_LOCAL, (uint16_t)sym->slot);
                        break;
                    default:
                        break;
                }
            }
            break;
        }
        
        case NODE_EXPR_INDEX: {
            emit_expr(emit, target->data.index_expr.object);
            emit_expr(emit, target->data.index_expr.index);
            bucc_emit_op(emit, OP_ARRAY_SET);
            break;
        }
        
        case NODE_EXPR_MEMBER: {
            emit_expr(emit, target->data.member.object);
            uint32_t str_idx = bucc_emit_string(emit, target->data.member.member);
            bucc_emit_op_u16(emit, OP_PUSH_STR, (uint16_t)str_idx);
            bucc_emit_op(emit, OP_MAP_SET);
            break;
        }
        
        default:
            break;
    }
}

static void emit_stmt(bucc_emitter_t* emit, bucc_node_t* stmt) {
    if (!emit || !stmt) return;
    
    if (emit->emit_debug_info && stmt->span.start_line > 0) {
        bucc_emit_debug_line(emit, stmt->span.start_line, stmt->span.start_col);
    }
    
    switch (stmt->kind) {
        case NODE_STMT_VAR_DECL: {
            if (stmt->data.var_decl.initializer) {
                emit_expr(emit, stmt->data.var_decl.initializer);
            } else {
                bucc_emit_op(emit, OP_PUSH_NULL);
            }
            bucc_emit_op_u16(emit, OP_STORE_LOCAL, (uint16_t)emit->current_local_count);
            emit->current_local_count++;
            break;
        }
        
        case NODE_STMT_ASSIGN: {
            emit_expr(emit, stmt->data.assign.value);
            emit_lvalue_store(emit, stmt->data.assign.target);
            break;
        }
        
        case NODE_STMT_EXPR: {
            emit_expr(emit, stmt->data.expr_stmt.expr);
            bucc_emit_op(emit, OP_POP);
            break;
        }
        
        case NODE_STMT_IF: {
            uint32_t else_label = bucc_emit_label(emit);
            uint32_t end_label = bucc_emit_label(emit);
            
            emit_expr(emit, stmt->data.if_stmt.condition);
            bucc_emit_jump(emit, OP_JMP_FALSE, else_label);
            
            emit_block(emit, stmt->data.if_stmt.then_block);
            bucc_emit_jump(emit, OP_JMP, end_label);
            
            bucc_emit_label_here(emit, else_label);
            
            for (size_t i = 0; i < stmt->data.if_stmt.elseif_clauses.count; i++) {
                bucc_node_t* clause = stmt->data.if_stmt.elseif_clauses.items[i];
                if (clause->kind == NODE_STMT_IF) {
                    uint32_t next_label = bucc_emit_label(emit);
                    
                    emit_expr(emit, clause->data.if_stmt.condition);
                    bucc_emit_jump(emit, OP_JMP_FALSE, next_label);
                    
                    emit_block(emit, clause->data.if_stmt.then_block);
                    bucc_emit_jump(emit, OP_JMP, end_label);
                    
                    bucc_emit_label_here(emit, next_label);
                }
            }
            
            if (stmt->data.if_stmt.else_block) {
                emit_block(emit, stmt->data.if_stmt.else_block);
            }
            
            bucc_emit_label_here(emit, end_label);
            break;
        }
        
        case NODE_STMT_SELECT: {
            uint32_t end_label = bucc_emit_label(emit);
            
            emit_expr(emit, stmt->data.select_stmt.expr);
            
            for (size_t i = 0; i < stmt->data.select_stmt.cases.count; i++) {
                bucc_node_t* case_node = stmt->data.select_stmt.cases.items[i];
                if (case_node->kind == NODE_STMT_CASE && !case_node->data.case_clause.is_else) {
                    uint32_t next_case = bucc_emit_label(emit);
                    uint32_t case_body = bucc_emit_label(emit);
                    
                    for (size_t j = 0; j < case_node->data.case_clause.values.count; j++) {
                        bucc_emit_op(emit, OP_DUP);
                        emit_expr(emit, case_node->data.case_clause.values.items[j]);
                        bucc_emit_op(emit, OP_EQ);
                        bucc_emit_jump(emit, OP_JMP_TRUE, case_body);
                    }
                    for (size_t j = 0; j < case_node->data.case_clause.range_lows.count &&
                                       j < case_node->data.case_clause.range_highs.count; j++) {
                        bucc_emit_op(emit, OP_DUP);
                        emit_expr(emit, case_node->data.case_clause.range_lows.items[j]);
                        emit_expr(emit, case_node->data.case_clause.range_highs.items[j]);
                        bucc_emit_op_u16(emit, OP_RANGE_TEST, 0);
                        bucc_emit_byte(emit, 0);
                        bucc_emit_jump(emit, OP_JMP_TRUE, case_body);
                    }
                    bucc_emit_jump(emit, OP_JMP, next_case);
                    
                    bucc_emit_label_here(emit, case_body);
                    bucc_emit_op(emit, OP_POP);
                    emit_block(emit, case_node->data.case_clause.body);
                    bucc_emit_jump(emit, OP_JMP, end_label);
                    
                    bucc_emit_label_here(emit, next_case);
                }
            }
            
            if (stmt->data.select_stmt.else_case) {
                bucc_emit_op(emit, OP_POP);
                emit_block(emit, stmt->data.select_stmt.else_case->data.case_clause.body);
            } else {
                bucc_emit_op(emit, OP_POP);
            }
            
            bucc_emit_label_here(emit, end_label);
            break;
        }
        
        case NODE_STMT_WHILE: {
            uint32_t start_label = bucc_emit_label(emit);
            uint32_t end_label = bucc_emit_label(emit);
            
            uint32_t saved_start = emit->loop_start_label;
            uint32_t saved_end = emit->loop_end_label;
            emit->loop_start_label = start_label;
            emit->loop_end_label = end_label;
            
            bucc_emit_label_here(emit, start_label);
            emit_expr(emit, stmt->data.while_stmt.condition);
            bucc_emit_jump(emit, OP_JMP_FALSE, end_label);
            
            emit_block(emit, stmt->data.while_stmt.body);
            bucc_emit_jump(emit, OP_JMP, start_label);
            
            bucc_emit_label_here(emit, end_label);
            
            emit->loop_start_label = saved_start;
            emit->loop_end_label = saved_end;
            break;
        }
        
        case NODE_STMT_DO_LOOP: {
            uint32_t start_label = bucc_emit_label(emit);
            uint32_t end_label = bucc_emit_label(emit);
            
            uint32_t saved_start = emit->loop_start_label;
            uint32_t saved_end = emit->loop_end_label;
            emit->loop_start_label = start_label;
            emit->loop_end_label = end_label;
            
            bucc_emit_label_here(emit, start_label);
            emit_block(emit, stmt->data.do_loop.body);
            
            if (stmt->data.do_loop.condition) {
                emit_expr(emit, stmt->data.do_loop.condition);
                if (stmt->data.do_loop.loop_kind == LOOP_UNTIL) {
                    bucc_emit_jump(emit, OP_JMP_FALSE, start_label);
                } else {
                    bucc_emit_jump(emit, OP_JMP_TRUE, start_label);
                }
            } else {
                bucc_emit_jump(emit, OP_JMP, start_label);
            }
            
            bucc_emit_label_here(emit, end_label);
            
            emit->loop_start_label = saved_start;
            emit->loop_end_label = saved_end;
            break;
        }
        
        case NODE_STMT_FOR: {
            uint32_t start_label = bucc_emit_label(emit);
            uint32_t end_label = bucc_emit_label(emit);
            
            uint32_t saved_start = emit->loop_start_label;
            uint32_t saved_end = emit->loop_end_label;
            emit->loop_start_label = start_label;
            emit->loop_end_label = end_label;
            
            emit_expr(emit, stmt->data.for_stmt.start_expr);
            bucc_emit_op_u16(emit, OP_STORE_LOCAL, (uint16_t)emit->current_local_count);
            uint16_t var_slot = (uint16_t)emit->current_local_count++;
            
            bucc_emit_label_here(emit, start_label);
            bucc_emit_op_u16(emit, OP_LOAD_LOCAL, var_slot);
            emit_expr(emit, stmt->data.for_stmt.end_expr);
            bucc_emit_op(emit, OP_LE);
            bucc_emit_jump(emit, OP_JMP_FALSE, end_label);
            
            emit_block(emit, stmt->data.for_stmt.body);
            
            bucc_emit_op_u16(emit, OP_LOAD_LOCAL, var_slot);
            if (stmt->data.for_stmt.step_expr) {
                emit_expr(emit, stmt->data.for_stmt.step_expr);
            } else {
                uint32_t one_idx = bucc_emit_constant_i64(emit, 1);
                bucc_emit_op_u16(emit, OP_PUSH_I64, (uint16_t)one_idx);
            }
            bucc_emit_op(emit, OP_ADD);
            bucc_emit_op_u16(emit, OP_STORE_LOCAL, var_slot);
            
            bucc_emit_jump(emit, OP_JMP, start_label);
            bucc_emit_label_here(emit, end_label);
            
            emit->loop_start_label = saved_start;
            emit->loop_end_label = saved_end;
            break;
        }
        
        case NODE_STMT_TRY_CATCH: {
            uint32_t catch_label = bucc_emit_label(emit);
            uint32_t end_label = bucc_emit_label(emit);
            
            bucc_emit_jump(emit, OP_TRY_BEGIN, catch_label);
            emit_block(emit, stmt->data.try_catch.try_block);
            bucc_emit_op(emit, OP_TRY_END);
            bucc_emit_jump(emit, OP_JMP, end_label);
            
            bucc_emit_label_here(emit, catch_label);
            if (stmt->data.try_catch.error_var) {
                bucc_emit_op_u16(emit, OP_STORE_LOCAL, (uint16_t)emit->current_local_count);
                emit->current_local_count++;
            } else {
                bucc_emit_op(emit, OP_POP);
            }
            emit_block(emit, stmt->data.try_catch.catch_block);
            
            bucc_emit_label_here(emit, end_label);
            break;
        }
        
        case NODE_STMT_RETURN: {
            if (stmt->data.return_stmt.value) {
                emit_expr(emit, stmt->data.return_stmt.value);
                bucc_emit_op(emit, OP_RETURN_VALUE);
            } else {
                bucc_emit_op(emit, OP_RETURN);
            }
            break;
        }
        
        case NODE_STMT_EXIT: {
            bucc_emit_jump(emit, OP_JMP, emit->loop_end_label);
            break;
        }
        
        case NODE_STMT_HALT: {
            bucc_emit_op(emit, OP_HALT);
            break;
        }
        
        case NODE_STMT_THROW: {
            emit_expr(emit, stmt->data.throw_stmt.expr);
            bucc_emit_op(emit, OP_THROW);
            break;
        }
        
        case NODE_STMT_CHAIN: {
            uint32_t target_idx = bucc_emit_string(emit, stmt->data.chain_stmt.target);
            if (stmt->data.chain_stmt.args) {
                emit_expr(emit, stmt->data.chain_stmt.args);
                bucc_emit_op_u16(emit, OP_CHAIN, (uint16_t)target_idx);
                bucc_emit_byte(emit, 1);
            } else {
                bucc_emit_op_u16(emit, OP_CHAIN, (uint16_t)target_idx);
                bucc_emit_byte(emit, 0);
            }
            break;
        }
        
        case NODE_STMT_ON_CALL: {
            uint16_t base = 0;
            uint8_t count = 0;
            if (!resolve_on_call_base(emit, stmt, &base, &count)) {
                break;
            }
            emit_expr(emit, stmt->data.on_call.selector);
            bucc_emit_op_u16(emit, OP_DISPATCH_CALL, base);
            bucc_emit_byte(emit, count);
            break;
        }
        
        default:
            break;
    }
}

static void emit_block(bucc_emitter_t* emit, bucc_node_t* block) {
    if (!emit || !block || block->kind != NODE_STMT_BLOCK) return;
    
    for (size_t i = 0; i < block->data.block.stmts.count; i++) {
        emit_stmt(emit, block->data.block.stmts.items[i]);
    }
}

static void emit_procedure(bucc_emitter_t* emit, bucc_node_t* proc) {
    if (!emit || !proc) return;
    
    bucc_proc_entry_t* entry = calloc(1, sizeof(bucc_proc_entry_t));
    if (!entry) return;
    
    entry->name = strdup(proc->data.proc_decl.name);
    entry->id = emit->proc_count++;
    entry->code_offset = (uint32_t)emit->code_len;
    entry->param_count = (uint32_t)proc->data.proc_decl.params.count;
    entry->is_function = false;
    
    emit->current_proc = entry;
    emit->current_local_count = entry->param_count;
    
    emit_block(emit, proc->data.proc_decl.body);
    bucc_emit_op(emit, OP_RETURN);
    
    entry->code_len = (uint32_t)emit->code_len - entry->code_offset;
    entry->local_count = emit->current_local_count;
    
    entry->next = emit->procedures;
    emit->procedures = entry;
}

static void emit_function(bucc_emitter_t* emit, bucc_node_t* func) {
    if (!emit || !func) return;
    
    bucc_proc_entry_t* entry = calloc(1, sizeof(bucc_proc_entry_t));
    if (!entry) return;
    
    entry->name = strdup(func->data.func_decl.name);
    entry->id = emit->proc_count++;
    entry->code_offset = (uint32_t)emit->code_len;
    entry->param_count = (uint32_t)func->data.func_decl.params.count;
    entry->is_function = true;
    
    emit->current_proc = entry;
    emit->current_local_count = entry->param_count;
    
    emit_block(emit, func->data.func_decl.body);
    bucc_emit_op(emit, OP_PUSH_NULL);
    bucc_emit_op(emit, OP_RETURN_VALUE);
    
    entry->code_len = (uint32_t)emit->code_len - entry->code_offset;
    entry->local_count = emit->current_local_count;
    
    entry->next = emit->procedures;
    emit->procedures = entry;
}

bool bucc_emit_module(bucc_emitter_t* emit, bucc_node_t* module) {
    if (!emit || !module || module->kind != NODE_MODULE) return false;
    
    for (size_t i = 0; i < module->data.module.procedures.count; i++) {
        bucc_node_t* proc = module->data.module.procedures.items[i];
        if (proc->kind == NODE_PROC_DECL) {
            emit_procedure(emit, proc);
        } else if (proc->kind == NODE_FUNC_DECL) {
            emit_function(emit, proc);
        }
    }
    
    bucc_emit_resolve_patches(emit);
    
    return !bucc_diag_has_errors(emit->diag);
}

bucc_module_t* bucc_emitter_to_module(bucc_emitter_t* emit) {
    if (!emit) return NULL;
    
    bucc_module_t* mod = calloc(1, sizeof(bucc_module_t));
    if (!mod) return NULL;
    
    mod->refcount = 1;
    
    memcpy(mod->header.magic, BUCC_MAGIC, 4);
    mod->header.format_major = BUCC_FORMAT_MAJOR;
    mod->header.format_minor = BUCC_FORMAT_MINOR;
    mod->header.flags = 0;
    mod->header.header_size = sizeof(bucc_module_header_t);
    
    uint32_t string_cap = emit->string_count + emit->import_count * 2 + emit->proc_count + 16;
    mod->strings = calloc(string_cap, sizeof(char*));
    mod->string_count = 0;
    
    if (mod->strings) {
        bucc_string_entry_t** sorted = calloc(emit->string_count > 0 ? emit->string_count : 1, sizeof(bucc_string_entry_t*));
        if (sorted) {
            for (bucc_string_entry_t* s = emit->strings; s; s = s->next) {
                if (s->index < emit->string_count) {
                    sorted[s->index] = s;
                }
            }
            for (uint32_t i = 0; i < emit->string_count; i++) {
                if (sorted[i] && sorted[i]->str) {
                    mod->strings[mod->string_count++] = strdup(sorted[i]->str);
                } else {
                    mod->strings[mod->string_count++] = strdup("");
                }
            }
            free(sorted);
        }
    }
    
    mod->const_count = emit->const_count;
    if (mod->const_count > 0) {
        mod->constants = calloc(mod->const_count, sizeof(bucc_value_t));
        if (mod->constants) {
            bucc_const_entry_t** sorted = calloc(mod->const_count, sizeof(bucc_const_entry_t*));
            if (sorted) {
                for (bucc_const_entry_t* c = emit->constants; c; c = c->next) {
                    if (c->index < mod->const_count) {
                        sorted[c->index] = c;
                    }
                }
                for (uint32_t i = 0; i < mod->const_count; i++) {
                    if (sorted[i]) {
                        mod->constants[i] = sorted[i]->value;
                        bucc_value_retain(&mod->constants[i]);
                    } else {
                        mod->constants[i] = BUCC_NULL_VAL;
                    }
                }
                free(sorted);
            }
        }
    }
    
    mod->proc_count = emit->proc_count;
    if (mod->proc_count > 0) {
        mod->procedures = calloc(mod->proc_count, sizeof(bucc_module_proc_t));
        if (mod->procedures) {
            bucc_proc_entry_t** sorted = calloc(mod->proc_count, sizeof(bucc_proc_entry_t*));
            if (sorted) {
                for (bucc_proc_entry_t* p = emit->procedures; p; p = p->next) {
                    if (p->id < mod->proc_count) {
                        sorted[p->id] = p;
                    }
                }
                for (uint32_t i = 0; i < mod->proc_count; i++) {
                    if (sorted[i]) {
                        mod->procedures[i].name = sorted[i]->name ? strdup(sorted[i]->name) : NULL;
                        mod->procedures[i].id = sorted[i]->id;
                        mod->procedures[i].code_offset = sorted[i]->code_offset;
                        mod->procedures[i].code_len = sorted[i]->code_len;
                        mod->procedures[i].param_count = sorted[i]->param_count;
                        mod->procedures[i].local_count = sorted[i]->local_count;
                        mod->procedures[i].is_function = sorted[i]->is_function;
                    }
                }
                free(sorted);
            }
        }
    }
    
    mod->import_count = emit->import_count;
    if (mod->import_count > 0) {
        mod->imports = calloc(mod->import_count, sizeof(bucc_module_import_t));
        if (mod->imports) {
            bucc_host_import_t** sorted = calloc(mod->import_count, sizeof(bucc_host_import_t*));
            if (sorted) {
                for (bucc_host_import_t* imp = emit->imports; imp; imp = imp->next) {
                    if (imp->id < mod->import_count) {
                        sorted[imp->id] = imp;
                    }
                }
                for (uint32_t i = 0; i < mod->import_count; i++) {
                    if (sorted[i]) {
                        mod->imports[i].ns = sorted[i]->ns ? strdup(sorted[i]->ns) : NULL;
                        mod->imports[i].fn = sorted[i]->fn ? strdup(sorted[i]->fn) : NULL;
                        mod->imports[i].id = sorted[i]->id;
                        mod->imports[i].capability = NULL;
                    }
                }
                free(sorted);
            }
        }
    }
    
    mod->bytecode_len = (uint32_t)emit->code_len;
    if (mod->bytecode_len > 0) {
        mod->bytecode = malloc(mod->bytecode_len);
        if (mod->bytecode) {
            memcpy(mod->bytecode, emit->code, mod->bytecode_len);
        }
    }
    
    return mod;
}

const char* bucc_opcode_name(bucc_opcode_t op) {
    switch (op) {
        case OP_NOP:          return "NOP";
        case OP_HALT:         return "HALT";
        case OP_PUSH_NULL:    return "PUSH_NULL";
        case OP_PUSH_TRUE:    return "PUSH_TRUE";
        case OP_PUSH_FALSE:   return "PUSH_FALSE";
        case OP_PUSH_I64:     return "PUSH_I64";
        case OP_PUSH_F64:     return "PUSH_F64";
        case OP_PUSH_STR:     return "PUSH_STR";
        case OP_PUSH_DATE:    return "PUSH_DATE";
        case OP_PUSH_DATETIME:return "PUSH_DATETIME";
        case OP_POP:          return "POP";
        case OP_DUP:          return "DUP";
        case OP_SWAP:         return "SWAP";
        case OP_LOAD_GLOBAL:  return "LOAD_GLOBAL";
        case OP_STORE_GLOBAL: return "STORE_GLOBAL";
        case OP_LOAD_LOCAL:   return "LOAD_LOCAL";
        case OP_STORE_LOCAL:  return "STORE_LOCAL";
        case OP_LOAD_ARG:     return "LOAD_ARG";
        case OP_ADD:          return "ADD";
        case OP_SUB:          return "SUB";
        case OP_MUL:          return "MUL";
        case OP_DIV:          return "DIV";
        case OP_MOD:          return "MOD";
        case OP_NEG:          return "NEG";
        case OP_EQ:           return "EQ";
        case OP_NE:           return "NE";
        case OP_LT:           return "LT";
        case OP_LE:           return "LE";
        case OP_GT:           return "GT";
        case OP_GE:           return "GE";
        case OP_AND:          return "AND";
        case OP_OR:           return "OR";
        case OP_NOT:          return "NOT";
        case OP_CONCAT:       return "CONCAT";
        case OP_JMP:          return "JMP";
        case OP_JMP_FALSE:    return "JMP_FALSE";
        case OP_JMP_TRUE:     return "JMP_TRUE";
        case OP_CALL:         return "CALL";
        case OP_CALL_HOST:    return "CALL_HOST";
        case OP_RETURN:       return "RETURN";
        case OP_RETURN_VALUE: return "RETURN_VALUE";
        case OP_CHAIN:        return "CHAIN";
        case OP_YIELD:        return "YIELD";
        case OP_ARRAY_NEW:    return "ARRAY_NEW";
        case OP_ARRAY_GET:    return "ARRAY_GET";
        case OP_ARRAY_SET:    return "ARRAY_SET";
        case OP_ARRAY_PUSH:   return "ARRAY_PUSH";
        case OP_ARRAY_POP:    return "ARRAY_POP";
        case OP_MAP_NEW:      return "MAP_NEW";
        case OP_MAP_GET:      return "MAP_GET";
        case OP_MAP_SET:      return "MAP_SET";
        case OP_MAP_HAS:      return "MAP_HAS";
        case OP_MAP_KEYS:     return "MAP_KEYS";
        case OP_DISPATCH_CALL:return "DISPATCH_CALL";
        case OP_RANGE_TEST:   return "RANGE_TEST";
        case OP_TRY_BEGIN:    return "TRY_BEGIN";
        case OP_TRY_END:      return "TRY_END";
        case OP_THROW:        return "THROW";
        case OP_RE_THROW:     return "RE_THROW";
        case OP_CAST_I64:     return "CAST_I64";
        case OP_CAST_F64:     return "CAST_F64";
        case OP_CAST_STRING:  return "CAST_STRING";
        case OP_CAST_BOOL:    return "CAST_BOOL";
        case OP_CAST_DATE:    return "CAST_DATE";
        case OP_CAST_DATETIME:return "CAST_DATETIME";
        case OP_TYPEOF:       return "TYPEOF";
        case OP_DEBUG_LINE:   return "DEBUG_LINE";
        case OP_PROF_TICK:    return "PROF_TICK";
        default:              return "UNKNOWN";
    }
}

void bucc_emit_debug_line(bucc_emitter_t* emit, uint32_t line, uint32_t col) {
    if (!emit || !emit->emit_debug_info) return;
    
    if (line == emit->last_debug_line) return;
    emit->last_debug_line = line;
    
    bucc_debug_line_info_t* entry = malloc(sizeof(bucc_debug_line_info_t));
    if (!entry) return;
    
    entry->bytecode_offset = (uint32_t)emit->code_len;
    entry->source_line = line;
    entry->source_column = col;
    entry->next = NULL;
    
    if (!emit->debug_lines) {
        emit->debug_lines = entry;
    } else {
        bucc_debug_line_info_t* last = emit->debug_lines;
        while (last->next) last = last->next;
        last->next = entry;
    }
    emit->debug_line_count++;
    
    bucc_emit_op_u16(emit, OP_DEBUG_LINE, (uint16_t)line);
}

void bucc_emitter_set_debug(bucc_emitter_t* emit, bool enable, const char* source_file) {
    if (!emit) return;
    
    emit->emit_debug_info = enable;
    free((void*)emit->source_file);
    emit->source_file = source_file ? strdup(source_file) : NULL;
}

bucc_debug_map_t* bucc_emitter_get_debug_map(bucc_emitter_t* emit) {
    if (!emit || !emit->emit_debug_info || !emit->debug_lines) return NULL;
    
    bucc_debug_map_t* map = bucc_debug_map_new(emit->source_file);
    if (!map) return NULL;
    
    for (bucc_debug_line_info_t* entry = emit->debug_lines; entry; entry = entry->next) {
        bucc_debug_map_add_entry(map, entry->bytecode_offset, entry->source_line, entry->source_column);
    }
    
    return map;
}
