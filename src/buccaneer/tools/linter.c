/*
 * linter.c - Buccaneer source code linter
 *
 * Part of the Buccaneer language implementation for Mutineer BBS.
 * Checks .bucc source files for style issues and potential bugs.
 */

#define _POSIX_C_SOURCE 200809L
#include "../include/bucc_lexer.h"
#include "../include/bucc_parser.h"
#include "../include/bucc_ast.h"
#include "../include/bucc_semantic.h"
#include "../include/bucc_diag.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef enum {
    LINT_ERROR,
    LINT_WARNING,
    LINT_INFO
} lint_severity_t;

typedef struct lint_issue {
    lint_severity_t severity;
    const char* rule;
    const char* message;
    uint32_t line;
    uint32_t column;
    struct lint_issue* next;
} lint_issue_t;

typedef struct {
    lint_issue_t* issues;
    lint_issue_t* last;
    uint32_t error_count;
    uint32_t warning_count;
    uint32_t info_count;
} lint_ctx_t;

typedef struct var_usage {
    char* name;
    uint32_t decl_line;
    uint32_t decl_col;
    bool assigned;
    bool used;
    struct var_usage* next;
} var_usage_t;

static void add_issue(lint_ctx_t* ctx, lint_severity_t severity, const char* rule,
                      uint32_t line, uint32_t col, const char* fmt, ...) {
    lint_issue_t* issue = calloc(1, sizeof(lint_issue_t));
    if (!issue) return;
    
    issue->severity = severity;
    issue->rule = rule;
    issue->line = line;
    issue->column = col;
    
    va_list args;
    va_start(args, fmt);
    char buf[512];
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    issue->message = strdup(buf);
    
    if (ctx->last) {
        ctx->last->next = issue;
    } else {
        ctx->issues = issue;
    }
    ctx->last = issue;
    
    switch (severity) {
        case LINT_ERROR: ctx->error_count++; break;
        case LINT_WARNING: ctx->warning_count++; break;
        case LINT_INFO: ctx->info_count++; break;
    }
}

static void free_issues(lint_ctx_t* ctx) {
    lint_issue_t* issue = ctx->issues;
    while (issue) {
        lint_issue_t* next = issue->next;
        free((void*)issue->message);
        free(issue);
        issue = next;
    }
}

static var_usage_t* add_var(var_usage_t** vars, const char* name, uint32_t line, uint32_t col) {
    var_usage_t* v = calloc(1, sizeof(var_usage_t));
    if (!v) return NULL;
    v->name = strdup(name);
    v->decl_line = line;
    v->decl_col = col;
    v->next = *vars;
    *vars = v;
    return v;
}

static var_usage_t* find_var(var_usage_t* vars, const char* name) {
    for (var_usage_t* v = vars; v; v = v->next) {
        if (strcasecmp(v->name, name) == 0) return v;
    }
    return NULL;
}

static void free_vars(var_usage_t* vars) {
    while (vars) {
        var_usage_t* next = vars->next;
        free(vars->name);
        free(vars);
        vars = next;
    }
}

static void mark_expr_used(var_usage_t* vars, bucc_node_t* expr);
static void check_stmt(lint_ctx_t* ctx, var_usage_t** vars, bucc_node_t* stmt, bool* has_return);
static void check_block(lint_ctx_t* ctx, var_usage_t** vars, bucc_node_t* block, bool* has_return);

static void mark_expr_used(var_usage_t* vars, bucc_node_t* expr) {
    if (!expr) return;
    
    switch (expr->kind) {
        case NODE_EXPR_IDENT: {
            var_usage_t* v = find_var(vars, expr->data.ident.name);
            if (v) v->used = true;
            break;
        }
        case NODE_EXPR_UNARY:
            mark_expr_used(vars, expr->data.unary.operand);
            break;
        case NODE_EXPR_BINARY:
            mark_expr_used(vars, expr->data.binary.left);
            mark_expr_used(vars, expr->data.binary.right);
            break;
        case NODE_EXPR_CALL:
            mark_expr_used(vars, expr->data.call.callee);
            for (size_t i = 0; i < expr->data.call.args.count; i++) {
                mark_expr_used(vars, expr->data.call.args.items[i]);
            }
            break;
        case NODE_EXPR_INDEX:
            mark_expr_used(vars, expr->data.index_expr.object);
            mark_expr_used(vars, expr->data.index_expr.index);
            break;
        case NODE_EXPR_MEMBER:
            mark_expr_used(vars, expr->data.member.object);
            break;
        case NODE_EXPR_ARRAY_LIT:
            for (size_t i = 0; i < expr->data.array_lit.elements.count; i++) {
                mark_expr_used(vars, expr->data.array_lit.elements.items[i]);
            }
            break;
        case NODE_EXPR_MAP_LIT:
            for (size_t i = 0; i < expr->data.map_lit.pairs.count; i++) {
                bucc_node_t* pair = expr->data.map_lit.pairs.items[i];
                if (pair && pair->kind == NODE_EXPR_MAP_PAIR) {
                    mark_expr_used(vars, pair->data.map_pair.value);
                }
            }
            break;
        default:
            break;
    }
}

static void check_stmt(lint_ctx_t* ctx, var_usage_t** vars, bucc_node_t* stmt, bool* has_return) {
    if (!stmt) return;
    
    switch (stmt->kind) {
        case NODE_STMT_VAR_DECL: {
            const char* name = stmt->data.var_decl.name;
            uint32_t line = stmt->span.start_line;
            uint32_t col = stmt->span.start_col;
            
            var_usage_t* existing = find_var(*vars, name);
            if (existing) {
                add_issue(ctx, LINT_WARNING, "shadowed-variable", line, col,
                         "Variable '%s' shadows declaration at line %u", name, existing->decl_line);
            }
            
            var_usage_t* v = add_var(vars, name, line, col);
            
            if (stmt->data.var_decl.initializer) {
                mark_expr_used(*vars, stmt->data.var_decl.initializer);
                if (v) v->assigned = true;
            }
            break;
        }
        
        case NODE_STMT_ASSIGN: {
            bucc_node_t* target = stmt->data.assign.target;
            if (target && target->kind == NODE_EXPR_IDENT) {
                var_usage_t* v = find_var(*vars, target->data.ident.name);
                if (v) v->assigned = true;
            }
            mark_expr_used(*vars, stmt->data.assign.value);
            break;
        }
        
        case NODE_STMT_EXPR:
            mark_expr_used(*vars, stmt->data.expr_stmt.expr);
            break;
            
        case NODE_STMT_IF: {
            mark_expr_used(*vars, stmt->data.if_stmt.condition);
            
            bool then_returns = false;
            check_block(ctx, vars, stmt->data.if_stmt.then_block, &then_returns);
            
            bool all_branches_return = then_returns;
            
            for (size_t i = 0; i < stmt->data.if_stmt.elseif_clauses.count; i++) {
                bucc_node_t* clause = stmt->data.if_stmt.elseif_clauses.items[i];
                if (clause && clause->kind == NODE_STMT_IF) {
                    mark_expr_used(*vars, clause->data.if_stmt.condition);
                    bool branch_returns = false;
                    check_block(ctx, vars, clause->data.if_stmt.then_block, &branch_returns);
                    if (!branch_returns) all_branches_return = false;
                }
            }
            
            if (stmt->data.if_stmt.else_block) {
                bool else_returns = false;
                check_block(ctx, vars, stmt->data.if_stmt.else_block, &else_returns);
                if (!else_returns) all_branches_return = false;
            } else {
                all_branches_return = false;
            }
            
            if (all_branches_return && has_return) *has_return = true;
            break;
        }
        
        case NODE_STMT_SELECT: {
            mark_expr_used(*vars, stmt->data.select_stmt.expr);
            
            for (size_t i = 0; i < stmt->data.select_stmt.cases.count; i++) {
                bucc_node_t* case_node = stmt->data.select_stmt.cases.items[i];
                if (case_node && case_node->kind == NODE_STMT_CASE) {
                    for (size_t j = 0; j < case_node->data.case_clause.values.count; j++) {
                        mark_expr_used(*vars, case_node->data.case_clause.values.items[j]);
                    }
                    bool dummy = false;
                    check_block(ctx, vars, case_node->data.case_clause.body, &dummy);
                }
            }
            
            if (stmt->data.select_stmt.else_case) {
                bool dummy = false;
                check_block(ctx, vars, stmt->data.select_stmt.else_case->data.case_clause.body, &dummy);
            }
            break;
        }
        
        case NODE_STMT_WHILE:
            mark_expr_used(*vars, stmt->data.while_stmt.condition);
            {
                bool dummy = false;
                check_block(ctx, vars, stmt->data.while_stmt.body, &dummy);
            }
            break;
            
        case NODE_STMT_DO_LOOP:
            {
                bool dummy = false;
                check_block(ctx, vars, stmt->data.do_loop.body, &dummy);
            }
            if (stmt->data.do_loop.condition) {
                mark_expr_used(*vars, stmt->data.do_loop.condition);
            }
            break;
            
        case NODE_STMT_FOR: {
            const char* var_name = stmt->data.for_stmt.var_name;
            uint32_t line = stmt->span.start_line;
            uint32_t col = stmt->span.start_col;
            
            var_usage_t* v = add_var(vars, var_name, line, col);
            if (v) {
                v->assigned = true;
                v->used = true;
            }
            
            mark_expr_used(*vars, stmt->data.for_stmt.start_expr);
            mark_expr_used(*vars, stmt->data.for_stmt.end_expr);
            if (stmt->data.for_stmt.step_expr) {
                mark_expr_used(*vars, stmt->data.for_stmt.step_expr);
            }
            
            bool dummy = false;
            check_block(ctx, vars, stmt->data.for_stmt.body, &dummy);
            break;
        }
        
        case NODE_STMT_TRY_CATCH: {
            bool dummy = false;
            check_block(ctx, vars, stmt->data.try_catch.try_block, &dummy);
            
            if (stmt->data.try_catch.error_var) {
                var_usage_t* v = add_var(vars, stmt->data.try_catch.error_var,
                                        stmt->span.start_line, stmt->span.start_col);
                if (v) v->assigned = true;
            }
            
            check_block(ctx, vars, stmt->data.try_catch.catch_block, &dummy);
            break;
        }
        
        case NODE_STMT_RETURN:
            if (stmt->data.return_stmt.value) {
                mark_expr_used(*vars, stmt->data.return_stmt.value);
            }
            if (has_return) *has_return = true;
            break;
            
        case NODE_STMT_THROW:
            mark_expr_used(*vars, stmt->data.throw_stmt.expr);
            if (has_return) *has_return = true;
            break;
            
        case NODE_STMT_HALT:
            if (has_return) *has_return = true;
            break;
            
        case NODE_STMT_CHAIN:
            if (stmt->data.chain_stmt.args) {
                mark_expr_used(*vars, stmt->data.chain_stmt.args);
            }
            if (has_return) *has_return = true;
            break;
            
        case NODE_STMT_ON_CALL:
            mark_expr_used(*vars, stmt->data.on_call.selector);
            break;
            
        default:
            break;
    }
}

static void check_block(lint_ctx_t* ctx, var_usage_t** vars, bucc_node_t* block, bool* has_return) {
    if (!block || block->kind != NODE_STMT_BLOCK) return;
    
    bool found_return = false;
    bool warned_unreachable = false;
    
    for (size_t i = 0; i < block->data.block.stmts.count; i++) {
        bucc_node_t* stmt = block->data.block.stmts.items[i];
        
        if (found_return && !warned_unreachable && stmt) {
            add_issue(ctx, LINT_WARNING, "unreachable-code",
                     stmt->span.start_line, stmt->span.start_col,
                     "Unreachable code after return/halt/throw/chain");
            warned_unreachable = true;
        }
        
        bool stmt_returns = false;
        check_stmt(ctx, vars, stmt, &stmt_returns);
        if (stmt_returns) found_return = true;
    }
    
    if (has_return) *has_return = found_return;
}

static void check_procedure(lint_ctx_t* ctx, bucc_node_t* proc) {
    if (!proc) return;
    
    var_usage_t* vars = NULL;
    bool has_return = false;
    
    if (proc->kind == NODE_PROC_DECL) {
        for (size_t i = 0; i < proc->data.proc_decl.params.count; i++) {
            bucc_node_t* param = proc->data.proc_decl.params.items[i];
            if (param && param->kind == NODE_PARAM_DECL) {
                var_usage_t* v = add_var(&vars, param->data.param_decl.name,
                                        param->span.start_line, param->span.start_col);
                if (v) v->assigned = true;
            }
        }
        
        check_block(ctx, &vars, proc->data.proc_decl.body, &has_return);
        
    } else if (proc->kind == NODE_FUNC_DECL) {
        for (size_t i = 0; i < proc->data.func_decl.params.count; i++) {
            bucc_node_t* param = proc->data.func_decl.params.items[i];
            if (param && param->kind == NODE_PARAM_DECL) {
                var_usage_t* v = add_var(&vars, param->data.param_decl.name,
                                        param->span.start_line, param->span.start_col);
                if (v) v->assigned = true;
            }
        }
        
        check_block(ctx, &vars, proc->data.func_decl.body, &has_return);
        
        if (!has_return) {
            add_issue(ctx, LINT_WARNING, "missing-return",
                     proc->span.start_line, proc->span.start_col,
                     "Function '%s' may not return a value on all paths",
                     proc->data.func_decl.name);
        }
    }
    
    for (var_usage_t* v = vars; v; v = v->next) {
        if (!v->used) {
            add_issue(ctx, LINT_INFO, "unused-variable",
                     v->decl_line, v->decl_col,
                     "Variable '%s' is declared but never used", v->name);
        } else if (!v->assigned) {
            add_issue(ctx, LINT_WARNING, "uninitialized-variable",
                     v->decl_line, v->decl_col,
                     "Variable '%s' may be used before being assigned", v->name);
        }
    }
    
    free_vars(vars);
}

static void check_naming_convention(lint_ctx_t* ctx, bucc_node_t* module) {
    if (!module || module->kind != NODE_MODULE) return;
    
    for (size_t i = 0; i < module->data.module.procedures.count; i++) {
        bucc_node_t* proc = module->data.module.procedures.items[i];
        if (!proc) continue;
        
        const char* name = NULL;
        if (proc->kind == NODE_PROC_DECL) {
            name = proc->data.proc_decl.name;
        } else if (proc->kind == NODE_FUNC_DECL) {
            name = proc->data.func_decl.name;
        }
        
        if (name && name[0] && !isupper((unsigned char)name[0])) {
            add_issue(ctx, LINT_INFO, "naming-convention",
                     proc->span.start_line, proc->span.start_col,
                     "Procedure/function '%s' should start with uppercase letter", name);
        }
    }
}

static void check_module(lint_ctx_t* ctx, bucc_node_t* module) {
    if (!module || module->kind != NODE_MODULE) return;
    
    bool has_main = false;
    for (size_t i = 0; i < module->data.module.procedures.count; i++) {
        bucc_node_t* proc = module->data.module.procedures.items[i];
        if (proc && proc->kind == NODE_PROC_DECL) {
            if (strcasecmp(proc->data.proc_decl.name, "Main") == 0) {
                has_main = true;
            }
        }
        check_procedure(ctx, proc);
    }
    
    if (!has_main) {
        add_issue(ctx, LINT_WARNING, "missing-main",
                 1, 1, "Module does not define a 'Main' procedure");
    }
    
    check_naming_convention(ctx, module);
}

static void output_json(lint_ctx_t* ctx, const char* filename) {
    printf("{\n");
    printf("  \"file\": \"%s\",\n", filename);
    printf("  \"issues\": [\n");
    
    bool first = true;
    for (lint_issue_t* issue = ctx->issues; issue; issue = issue->next) {
        if (!first) printf(",\n");
        first = false;
        
        const char* severity_str = "info";
        if (issue->severity == LINT_ERROR) severity_str = "error";
        else if (issue->severity == LINT_WARNING) severity_str = "warning";
        
        printf("    {\n");
        printf("      \"severity\": \"%s\",\n", severity_str);
        printf("      \"rule\": \"%s\",\n", issue->rule);
        printf("      \"line\": %u,\n", issue->line);
        printf("      \"column\": %u,\n", issue->column);
        printf("      \"message\": \"%s\"\n", issue->message);
        printf("    }");
    }
    
    printf("\n  ],\n");
    printf("  \"summary\": {\n");
    printf("    \"errors\": %u,\n", ctx->error_count);
    printf("    \"warnings\": %u,\n", ctx->warning_count);
    printf("    \"info\": %u\n", ctx->info_count);
    printf("  }\n");
    printf("}\n");
}

static void output_text(lint_ctx_t* ctx, const char* filename) {
    for (lint_issue_t* issue = ctx->issues; issue; issue = issue->next) {
        const char* severity_str = "info";
        if (issue->severity == LINT_ERROR) severity_str = "error";
        else if (issue->severity == LINT_WARNING) severity_str = "warning";
        
        printf("%s:%u:%u: %s [%s]: %s\n",
               filename, issue->line, issue->column,
               severity_str, issue->rule, issue->message);
    }
    
    printf("\n%u error(s), %u warning(s), %u info\n",
           ctx->error_count, ctx->warning_count, ctx->info_count);
}

int bucc_lint_file(const char* input_path, bool json_output) {
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
    
    lint_ctx_t ctx = {0};
    
    uint32_t parse_errors = bucc_diag_error_count(diag);
    if (parse_errors > 0) {
        add_issue(&ctx, LINT_ERROR, "parse-error", 0, 0,
                 "%u parse error(s) found", parse_errors);
        
        if (json_output) {
            output_json(&ctx, input_path);
        } else {
            bucc_diag_print(diag, stderr);
            output_text(&ctx, input_path);
        }
        
        free_issues(&ctx);
        bucc_node_free(ast);
        bucc_diag_free(diag);
        free(source);
        return 1;
    }
    
    bucc_semantic_t* sem = bucc_semantic_new(diag);
    if (sem) {
        bucc_semantic_analyze(sem, ast);
        bucc_semantic_free(sem);
    }
    
    uint32_t sem_errors = bucc_diag_error_count(diag);
    if (sem_errors > 0) {
        add_issue(&ctx, LINT_ERROR, "semantic-error", 0, 0,
                 "%u semantic error(s) found", sem_errors);
    }
    
    check_module(&ctx, ast);
    
    if (json_output) {
        output_json(&ctx, input_path);
    } else {
        if (sem_errors > 0) {
            bucc_diag_print(diag, stderr);
        }
        output_text(&ctx, input_path);
    }
    
    int result = ctx.error_count > 0 ? 1 : 0;
    
    free_issues(&ctx);
    bucc_node_free(ast);
    bucc_diag_free(diag);
    free(source);
    
    return result;
}
