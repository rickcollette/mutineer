/*
 * formatter.c - Buccaneer source code formatter
 *
 * Part of the Buccaneer language implementation for Mutineer BBS.
 * Reformats .bucc source files to canonical style.
 */

#define _POSIX_C_SOURCE 200809L
#include "../include/bucc_lexer.h"
#include "../include/bucc_parser.h"
#include "../include/bucc_ast.h"
#include "../include/bucc_diag.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef struct {
    FILE* out;
    bool uppercase;
    int indent_size;
    int indent_level;
} format_ctx_t;

static void emit_indent(format_ctx_t* ctx) {
    for (int i = 0; i < ctx->indent_level * ctx->indent_size; i++) {
        fputc(' ', ctx->out);
    }
}

static void emit_keyword(format_ctx_t* ctx, const char* kw) {
    if (ctx->uppercase) {
        while (*kw) {
            fputc(toupper((unsigned char)*kw), ctx->out);
            kw++;
        }
    } else {
        fputs(kw, ctx->out);
    }
}

static void emit_newline(format_ctx_t* ctx) {
    fputc('\n', ctx->out);
}

static void format_type(format_ctx_t* ctx, bucc_node_t* type);
static void format_expr(format_ctx_t* ctx, bucc_node_t* expr);
static void format_stmt(format_ctx_t* ctx, bucc_node_t* stmt);
static void format_block(format_ctx_t* ctx, bucc_node_t* block);

static void format_type(format_ctx_t* ctx, bucc_node_t* type) {
    if (!type) return;
    
    switch (type->kind) {
        case NODE_TYPE_SCALAR:
            switch (type->data.type_scalar.scalar_type) {
                case SCALAR_INTEGER:  emit_keyword(ctx, "Integer"); break;
                case SCALAR_DOUBLE:   emit_keyword(ctx, "Double"); break;
                case SCALAR_BOOLEAN:  emit_keyword(ctx, "Boolean"); break;
                case SCALAR_STRING:   emit_keyword(ctx, "String"); break;
                case SCALAR_DATE:     emit_keyword(ctx, "Date"); break;
                case SCALAR_DATETIME: emit_keyword(ctx, "DateTime"); break;
            }
            break;
        case NODE_TYPE_ARRAY:
            emit_keyword(ctx, "Array");
            fputs("(", ctx->out);
            format_type(ctx, type->data.type_array.element_type);
            fputs(")", ctx->out);
            break;
        case NODE_TYPE_MAP:
            emit_keyword(ctx, "Map");
            fputs("(", ctx->out);
            format_type(ctx, type->data.type_map.value_type);
            fputs(")", ctx->out);
            break;
        default:
            break;
    }
}

static void format_expr(format_ctx_t* ctx, bucc_node_t* expr) {
    if (!expr) return;
    
    switch (expr->kind) {
        case NODE_EXPR_LITERAL:
            switch (expr->data.literal.value.kind) {
                case BUCC_VAL_NULL:
                    emit_keyword(ctx, "Null");
                    break;
                case BUCC_VAL_BOOL:
                    emit_keyword(ctx, expr->data.literal.value.as.b ? "True" : "False");
                    break;
                case BUCC_VAL_I64:
                    fprintf(ctx->out, "%ld", (long)expr->data.literal.value.as.i64);
                    break;
                case BUCC_VAL_F64:
                    fprintf(ctx->out, "%g", expr->data.literal.value.as.f64);
                    break;
                case BUCC_VAL_STRING: {
                    bucc_string_t* s = expr->data.literal.value.as.str;
                    fputc('"', ctx->out);
                    if (s && s->data) {
                        for (size_t i = 0; i < s->len; i++) {
                            char c = s->data[i];
                            switch (c) {
                                case '"':  fputs("\\\"", ctx->out); break;
                                case '\\': fputs("\\\\", ctx->out); break;
                                case '\n': fputs("\\n", ctx->out); break;
                                case '\r': fputs("\\r", ctx->out); break;
                                case '\t': fputs("\\t", ctx->out); break;
                                default:
                                    if (c >= 32 && c < 127) {
                                        fputc(c, ctx->out);
                                    } else {
                                        fprintf(ctx->out, "\\x%02x", (unsigned char)c);
                                    }
                                    break;
                            }
                        }
                    }
                    fputc('"', ctx->out);
                    break;
                }
                case BUCC_VAL_DATE: {
                    bucc_date_t d = expr->data.literal.value.as.date;
                    fprintf(ctx->out, "#%04d-%02d-%02d#", d.year, d.month, d.day);
                    break;
                }
                case BUCC_VAL_DATETIME: {
                    bucc_datetime_t dt = expr->data.literal.value.as.datetime;
                    fprintf(ctx->out, "#%04d-%02d-%02d %02d:%02d:%02d#",
                            dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
                    break;
                }
                default:
                    emit_keyword(ctx, "Null");
                    break;
            }
            break;
            
        case NODE_EXPR_IDENT:
            fputs(expr->data.ident.name, ctx->out);
            break;
            
        case NODE_EXPR_QUALIFIED:
            fputs(expr->data.qualified.ns, ctx->out);
            fputc('.', ctx->out);
            fputs(expr->data.qualified.name, ctx->out);
            break;
            
        case NODE_EXPR_CALL:
            format_expr(ctx, expr->data.call.callee);
            fputc('(', ctx->out);
            for (size_t i = 0; i < expr->data.call.args.count; i++) {
                if (i > 0) fputs(", ", ctx->out);
                format_expr(ctx, expr->data.call.args.items[i]);
            }
            fputc(')', ctx->out);
            break;
            
        case NODE_EXPR_UNARY:
            switch (expr->data.unary.op) {
                case UNARY_NEG: fputc('-', ctx->out); break;
                case UNARY_NOT: emit_keyword(ctx, "Not "); break;
            }
            format_expr(ctx, expr->data.unary.operand);
            break;
            
        case NODE_EXPR_BINARY:
            format_expr(ctx, expr->data.binary.left);
            switch (expr->data.binary.op) {
                case BINARY_ADD:    fputs(" + ", ctx->out); break;
                case BINARY_SUB:    fputs(" - ", ctx->out); break;
                case BINARY_MUL:    fputs(" * ", ctx->out); break;
                case BINARY_DIV:    fputs(" / ", ctx->out); break;
                case BINARY_MOD:    fputs(" ", ctx->out); emit_keyword(ctx, "Mod"); fputs(" ", ctx->out); break;
                case BINARY_EQ:     fputs(" = ", ctx->out); break;
                case BINARY_NE:     fputs(" <> ", ctx->out); break;
                case BINARY_LT:     fputs(" < ", ctx->out); break;
                case BINARY_LE:     fputs(" <= ", ctx->out); break;
                case BINARY_GT:     fputs(" > ", ctx->out); break;
                case BINARY_GE:     fputs(" >= ", ctx->out); break;
                case BINARY_AND:    fputs(" ", ctx->out); emit_keyword(ctx, "And"); fputs(" ", ctx->out); break;
                case BINARY_OR:     fputs(" ", ctx->out); emit_keyword(ctx, "Or"); fputs(" ", ctx->out); break;
                case BINARY_CONCAT: fputs(" & ", ctx->out); break;
            }
            format_expr(ctx, expr->data.binary.right);
            break;
            
        case NODE_EXPR_INDEX:
            format_expr(ctx, expr->data.index_expr.object);
            fputc('(', ctx->out);
            format_expr(ctx, expr->data.index_expr.index);
            fputc(')', ctx->out);
            break;
            
        case NODE_EXPR_MEMBER:
            format_expr(ctx, expr->data.member.object);
            fputc('.', ctx->out);
            fputs(expr->data.member.member, ctx->out);
            break;
            
        case NODE_EXPR_ARRAY_LIT:
            fputc('[', ctx->out);
            for (size_t i = 0; i < expr->data.array_lit.elements.count; i++) {
                if (i > 0) fputs(", ", ctx->out);
                format_expr(ctx, expr->data.array_lit.elements.items[i]);
            }
            fputc(']', ctx->out);
            break;
            
        case NODE_EXPR_MAP_LIT:
            fputc('{', ctx->out);
            for (size_t i = 0; i < expr->data.map_lit.pairs.count; i++) {
                if (i > 0) fputs(", ", ctx->out);
                bucc_node_t* pair = expr->data.map_lit.pairs.items[i];
                if (pair && pair->kind == NODE_EXPR_MAP_PAIR) {
                    fprintf(ctx->out, "\"%s\": ", pair->data.map_pair.key);
                    format_expr(ctx, pair->data.map_pair.value);
                }
            }
            fputc('}', ctx->out);
            break;
            
        default:
            break;
    }
}

static void format_stmt(format_ctx_t* ctx, bucc_node_t* stmt) {
    if (!stmt) return;
    
    switch (stmt->kind) {
        case NODE_STMT_VAR_DECL: {
            bucc_node_t* decl = stmt;
            emit_indent(ctx);
            emit_keyword(ctx, "Dim");
            fputs(" ", ctx->out);
            fputs(decl->data.var_decl.name, ctx->out);
            if (decl->data.var_decl.type_ref) {
                fputs(" ", ctx->out);
                emit_keyword(ctx, "As");
                fputs(" ", ctx->out);
                format_type(ctx, decl->data.var_decl.type_ref);
            }
            if (decl->data.var_decl.initializer) {
                fputs(" = ", ctx->out);
                format_expr(ctx, decl->data.var_decl.initializer);
            }
            emit_newline(ctx);
            break;
        }
        
        case NODE_STMT_ASSIGN:
            emit_indent(ctx);
            format_expr(ctx, stmt->data.assign.target);
            fputs(" = ", ctx->out);
            format_expr(ctx, stmt->data.assign.value);
            emit_newline(ctx);
            break;
            
        case NODE_STMT_EXPR:
            emit_indent(ctx);
            format_expr(ctx, stmt->data.expr_stmt.expr);
            emit_newline(ctx);
            break;
            
        case NODE_STMT_IF:
            emit_indent(ctx);
            emit_keyword(ctx, "If");
            fputs(" ", ctx->out);
            format_expr(ctx, stmt->data.if_stmt.condition);
            fputs(" ", ctx->out);
            emit_keyword(ctx, "Then");
            emit_newline(ctx);
            ctx->indent_level++;
            format_block(ctx, stmt->data.if_stmt.then_block);
            ctx->indent_level--;
            
            for (size_t i = 0; i < stmt->data.if_stmt.elseif_clauses.count; i++) {
                bucc_node_t* clause = stmt->data.if_stmt.elseif_clauses.items[i];
                if (clause && clause->kind == NODE_STMT_IF) {
                    emit_indent(ctx);
                    emit_keyword(ctx, "ElseIf");
                    fputs(" ", ctx->out);
                    format_expr(ctx, clause->data.if_stmt.condition);
                    fputs(" ", ctx->out);
                    emit_keyword(ctx, "Then");
                    emit_newline(ctx);
                    ctx->indent_level++;
                    format_block(ctx, clause->data.if_stmt.then_block);
                    ctx->indent_level--;
                }
            }
            
            if (stmt->data.if_stmt.else_block) {
                emit_indent(ctx);
                emit_keyword(ctx, "Else");
                emit_newline(ctx);
                ctx->indent_level++;
                format_block(ctx, stmt->data.if_stmt.else_block);
                ctx->indent_level--;
            }
            
            emit_indent(ctx);
            emit_keyword(ctx, "End If");
            emit_newline(ctx);
            break;
            
        case NODE_STMT_SELECT:
            emit_indent(ctx);
            emit_keyword(ctx, "Select Case");
            fputs(" ", ctx->out);
            format_expr(ctx, stmt->data.select_stmt.expr);
            emit_newline(ctx);
            ctx->indent_level++;
            
            for (size_t i = 0; i < stmt->data.select_stmt.cases.count; i++) {
                bucc_node_t* case_node = stmt->data.select_stmt.cases.items[i];
                if (case_node && case_node->kind == NODE_STMT_CASE && !case_node->data.case_clause.is_else) {
                    emit_indent(ctx);
                    emit_keyword(ctx, "Case");
                    fputs(" ", ctx->out);
                    for (size_t j = 0; j < case_node->data.case_clause.values.count; j++) {
                        if (j > 0) fputs(", ", ctx->out);
                        format_expr(ctx, case_node->data.case_clause.values.items[j]);
                    }
                    emit_newline(ctx);
                    ctx->indent_level++;
                    format_block(ctx, case_node->data.case_clause.body);
                    ctx->indent_level--;
                }
            }
            
            if (stmt->data.select_stmt.else_case) {
                emit_indent(ctx);
                emit_keyword(ctx, "Case Else");
                emit_newline(ctx);
                ctx->indent_level++;
                format_block(ctx, stmt->data.select_stmt.else_case->data.case_clause.body);
                ctx->indent_level--;
            }
            
            ctx->indent_level--;
            emit_indent(ctx);
            emit_keyword(ctx, "End Select");
            emit_newline(ctx);
            break;
            
        case NODE_STMT_WHILE:
            emit_indent(ctx);
            emit_keyword(ctx, "While");
            fputs(" ", ctx->out);
            format_expr(ctx, stmt->data.while_stmt.condition);
            emit_newline(ctx);
            ctx->indent_level++;
            format_block(ctx, stmt->data.while_stmt.body);
            ctx->indent_level--;
            emit_indent(ctx);
            emit_keyword(ctx, "Wend");
            emit_newline(ctx);
            break;
            
        case NODE_STMT_DO_LOOP:
            emit_indent(ctx);
            emit_keyword(ctx, "Do");
            emit_newline(ctx);
            ctx->indent_level++;
            format_block(ctx, stmt->data.do_loop.body);
            ctx->indent_level--;
            emit_indent(ctx);
            if (stmt->data.do_loop.condition) {
                emit_keyword(ctx, "Loop");
                fputs(" ", ctx->out);
                if (stmt->data.do_loop.loop_kind == LOOP_UNTIL) {
                    emit_keyword(ctx, "Until");
                } else {
                    emit_keyword(ctx, "While");
                }
                fputs(" ", ctx->out);
                format_expr(ctx, stmt->data.do_loop.condition);
            } else {
                emit_keyword(ctx, "Loop");
            }
            emit_newline(ctx);
            break;
            
        case NODE_STMT_FOR:
            emit_indent(ctx);
            emit_keyword(ctx, "For");
            fputs(" ", ctx->out);
            fputs(stmt->data.for_stmt.var_name, ctx->out);
            fputs(" = ", ctx->out);
            format_expr(ctx, stmt->data.for_stmt.start_expr);
            fputs(" ", ctx->out);
            emit_keyword(ctx, "To");
            fputs(" ", ctx->out);
            format_expr(ctx, stmt->data.for_stmt.end_expr);
            if (stmt->data.for_stmt.step_expr) {
                fputs(" ", ctx->out);
                emit_keyword(ctx, "Step");
                fputs(" ", ctx->out);
                format_expr(ctx, stmt->data.for_stmt.step_expr);
            }
            emit_newline(ctx);
            ctx->indent_level++;
            format_block(ctx, stmt->data.for_stmt.body);
            ctx->indent_level--;
            emit_indent(ctx);
            emit_keyword(ctx, "Next");
            fputs(" ", ctx->out);
            fputs(stmt->data.for_stmt.var_name, ctx->out);
            emit_newline(ctx);
            break;
            
        case NODE_STMT_TRY_CATCH:
            emit_indent(ctx);
            emit_keyword(ctx, "Try");
            emit_newline(ctx);
            ctx->indent_level++;
            format_block(ctx, stmt->data.try_catch.try_block);
            ctx->indent_level--;
            emit_indent(ctx);
            emit_keyword(ctx, "Catch");
            if (stmt->data.try_catch.error_var) {
                fputs(" ", ctx->out);
                fputs(stmt->data.try_catch.error_var, ctx->out);
            }
            emit_newline(ctx);
            ctx->indent_level++;
            format_block(ctx, stmt->data.try_catch.catch_block);
            ctx->indent_level--;
            emit_indent(ctx);
            emit_keyword(ctx, "End Try");
            emit_newline(ctx);
            break;
            
        case NODE_STMT_RETURN:
            emit_indent(ctx);
            emit_keyword(ctx, "Return");
            if (stmt->data.return_stmt.value) {
                fputs(" ", ctx->out);
                format_expr(ctx, stmt->data.return_stmt.value);
            }
            emit_newline(ctx);
            break;
            
        case NODE_STMT_EXIT:
            emit_indent(ctx);
            emit_keyword(ctx, "Exit");
            fputs(" ", ctx->out);
            switch (stmt->data.exit_stmt.exit_kind) {
                case EXIT_FOR:    emit_keyword(ctx, "For"); break;
                case EXIT_DO:     emit_keyword(ctx, "Do"); break;
                case EXIT_WHILE:  emit_keyword(ctx, "While"); break;
                case EXIT_SELECT: emit_keyword(ctx, "Select"); break;
            }
            emit_newline(ctx);
            break;
            
        case NODE_STMT_HALT:
            emit_indent(ctx);
            emit_keyword(ctx, "Halt");
            emit_newline(ctx);
            break;
            
        case NODE_STMT_THROW:
            emit_indent(ctx);
            emit_keyword(ctx, "Throw");
            fputs(" ", ctx->out);
            format_expr(ctx, stmt->data.throw_stmt.expr);
            emit_newline(ctx);
            break;
            
        case NODE_STMT_CHAIN:
            emit_indent(ctx);
            emit_keyword(ctx, "Chain");
            fputs(" \"", ctx->out);
            fputs(stmt->data.chain_stmt.target, ctx->out);
            fputs("\"", ctx->out);
            if (stmt->data.chain_stmt.args) {
                fputs(", ", ctx->out);
                format_expr(ctx, stmt->data.chain_stmt.args);
            }
            emit_newline(ctx);
            break;
            
        case NODE_STMT_ON_CALL:
            emit_indent(ctx);
            emit_keyword(ctx, "On");
            fputs(" ", ctx->out);
            format_expr(ctx, stmt->data.on_call.selector);
            fputs(" ", ctx->out);
            emit_keyword(ctx, "Call");
            fputs(" ", ctx->out);
            for (size_t i = 0; i < stmt->data.on_call.targets.count; i++) {
                if (i > 0) fputs(", ", ctx->out);
                bucc_node_t* target = stmt->data.on_call.targets.items[i];
                if (target && target->kind == NODE_EXPR_IDENT) {
                    fputs(target->data.ident.name, ctx->out);
                }
            }
            emit_newline(ctx);
            break;
            
        default:
            break;
    }
}

static void format_block(format_ctx_t* ctx, bucc_node_t* block) {
    if (!block || block->kind != NODE_STMT_BLOCK) return;
    
    for (size_t i = 0; i < block->data.block.stmts.count; i++) {
        format_stmt(ctx, block->data.block.stmts.items[i]);
    }
}

static void format_metadata(format_ctx_t* ctx, bucc_node_t* meta) {
    if (!meta) return;
    
    emit_indent(ctx);
    switch (meta->kind) {
        case NODE_META_PROGRAM:
            emit_keyword(ctx, "'$Program");
            fputs(" ", ctx->out);
            fputs(meta->data.meta_string.value, ctx->out);
            break;
        case NODE_META_VERSION:
            emit_keyword(ctx, "'$Version");
            fputs(" ", ctx->out);
            fputs(meta->data.meta_string.value, ctx->out);
            break;
        case NODE_META_AUTHOR:
            emit_keyword(ctx, "'$Author");
            fputs(" ", ctx->out);
            fputs(meta->data.meta_string.value, ctx->out);
            break;
        case NODE_META_DESCRIPTION:
            emit_keyword(ctx, "'$Description");
            fputs(" ", ctx->out);
            fputs(meta->data.meta_string.value, ctx->out);
            break;
        case NODE_META_CAPABILITY:
            emit_keyword(ctx, "'$Capability");
            fputs(" ", ctx->out);
            fputs(meta->data.meta_string.value, ctx->out);
            break;
        case NODE_META_OPTION:
            emit_keyword(ctx, "'$Option");
            fputs(" ", ctx->out);
            fputs(meta->data.meta_option.name, ctx->out);
            fputs(" ", ctx->out);
            fputs(meta->data.meta_option.value, ctx->out);
            break;
        case NODE_META_DATASET:
            emit_keyword(ctx, "'$Dataset");
            fputs(" ", ctx->out);
            fputs(meta->data.meta_dataset.name, ctx->out);
            break;
        default:
            break;
    }
    emit_newline(ctx);
}

static void format_global_var(format_ctx_t* ctx, bucc_node_t* var) {
    if (!var || var->kind != NODE_VAR_DECL) return;
    
    emit_indent(ctx);
    emit_keyword(ctx, "Dim");
    fputs(" ", ctx->out);
    emit_keyword(ctx, "Shared");
    fputs(" ", ctx->out);
    fputs(var->data.var_decl.name, ctx->out);
    if (var->data.var_decl.type_ref) {
        fputs(" ", ctx->out);
        emit_keyword(ctx, "As");
        fputs(" ", ctx->out);
        format_type(ctx, var->data.var_decl.type_ref);
    }
    if (var->data.var_decl.initializer) {
        fputs(" = ", ctx->out);
        format_expr(ctx, var->data.var_decl.initializer);
    }
    emit_newline(ctx);
}

static void format_procedure(format_ctx_t* ctx, bucc_node_t* proc) {
    if (!proc) return;
    
    if (proc->kind == NODE_PROC_DECL) {
        emit_indent(ctx);
        emit_keyword(ctx, "Sub");
        fputs(" ", ctx->out);
        fputs(proc->data.proc_decl.name, ctx->out);
        fputs("(", ctx->out);
        for (size_t i = 0; i < proc->data.proc_decl.params.count; i++) {
            if (i > 0) fputs(", ", ctx->out);
            bucc_node_t* param = proc->data.proc_decl.params.items[i];
            if (param && param->kind == NODE_PARAM_DECL) {
                fputs(param->data.param_decl.name, ctx->out);
                if (param->data.param_decl.type_ref) {
                    fputs(" ", ctx->out);
                    emit_keyword(ctx, "As");
                    fputs(" ", ctx->out);
                    format_type(ctx, param->data.param_decl.type_ref);
                }
            }
        }
        fputs(")", ctx->out);
        emit_newline(ctx);
        ctx->indent_level++;
        format_block(ctx, proc->data.proc_decl.body);
        ctx->indent_level--;
        emit_indent(ctx);
        emit_keyword(ctx, "End Sub");
        emit_newline(ctx);
    } else if (proc->kind == NODE_FUNC_DECL) {
        emit_indent(ctx);
        emit_keyword(ctx, "Function");
        fputs(" ", ctx->out);
        fputs(proc->data.func_decl.name, ctx->out);
        fputs("(", ctx->out);
        for (size_t i = 0; i < proc->data.func_decl.params.count; i++) {
            if (i > 0) fputs(", ", ctx->out);
            bucc_node_t* param = proc->data.func_decl.params.items[i];
            if (param && param->kind == NODE_PARAM_DECL) {
                fputs(param->data.param_decl.name, ctx->out);
                if (param->data.param_decl.type_ref) {
                    fputs(" ", ctx->out);
                    emit_keyword(ctx, "As");
                    fputs(" ", ctx->out);
                    format_type(ctx, param->data.param_decl.type_ref);
                }
            }
        }
        fputs(")", ctx->out);
        if (proc->data.func_decl.return_type) {
            fputs(" ", ctx->out);
            emit_keyword(ctx, "As");
            fputs(" ", ctx->out);
            format_type(ctx, proc->data.func_decl.return_type);
        }
        emit_newline(ctx);
        ctx->indent_level++;
        format_block(ctx, proc->data.func_decl.body);
        ctx->indent_level--;
        emit_indent(ctx);
        emit_keyword(ctx, "End Function");
        emit_newline(ctx);
    }
}

static void format_module(format_ctx_t* ctx, bucc_node_t* module) {
    if (!module || module->kind != NODE_MODULE) return;
    
    for (size_t i = 0; i < module->data.module.metadata.count; i++) {
        format_metadata(ctx, module->data.module.metadata.items[i]);
    }
    
    if (module->data.module.metadata.count > 0) {
        emit_newline(ctx);
    }
    
    for (size_t i = 0; i < module->data.module.globals.count; i++) {
        format_global_var(ctx, module->data.module.globals.items[i]);
    }
    
    if (module->data.module.globals.count > 0) {
        emit_newline(ctx);
    }
    
    for (size_t i = 0; i < module->data.module.procedures.count; i++) {
        if (i > 0) emit_newline(ctx);
        format_procedure(ctx, module->data.module.procedures.items[i]);
    }
}

int bucc_format_file(const char* input_path, const char* output_path,
                     bool uppercase, int indent_size) {
    FILE* in = fopen(input_path, "r");
    if (!in) {
        fprintf(stderr, "Error: Cannot open input file: %s\n", input_path);
        return 1;
    }
    
    fseek(in, 0, SEEK_END);
    long size = ftell(in);
    fseek(in, 0, SEEK_SET);
    
    char* source = malloc(size + 1);
    if (!source) {
        fclose(in);
        return 1;
    }
    
    size_t len = fread(source, 1, size, in);
    source[len] = '\0';
    fclose(in);
    
    bucc_diag_t* diag = bucc_diag_new();
    if (!diag) {
        free(source);
        return 1;
    }
    
    uint32_t file_id = bucc_diag_add_file(diag, input_path, source, len);
    
    bucc_lexer_t lexer;
    bucc_lexer_init(&lexer, source, len, file_id, diag);
    
    bucc_parser_t parser;
    bucc_parser_init(&parser, &lexer, diag);
    
    bucc_node_t* ast = bucc_parse_module(&parser);
    
    if (bucc_diag_has_errors(diag)) {
        bucc_diag_print(diag, stderr);
        bucc_node_free(ast);
        bucc_diag_free(diag);
        free(source);
        return 1;
    }
    
    FILE* out = stdout;
    if (output_path && strcmp(output_path, "-") != 0) {
        out = fopen(output_path, "w");
        if (!out) {
            fprintf(stderr, "Error: Cannot open output file: %s\n", output_path);
            bucc_node_free(ast);
            bucc_diag_free(diag);
            free(source);
            return 1;
        }
    }
    
    format_ctx_t ctx = {
        .out = out,
        .uppercase = uppercase,
        .indent_size = indent_size > 0 ? indent_size : 4,
        .indent_level = 0
    };
    
    format_module(&ctx, ast);
    
    if (out != stdout) {
        fclose(out);
    }
    
    bucc_node_free(ast);
    bucc_diag_free(diag);
    free(source);
    
    return 0;
}
