/*
 * parser.c - Buccaneer recursive descent parser implementation
 *
 * Part of the Buccaneer language implementation for Mutineer BBS.
 */

#define _POSIX_C_SOURCE 200809L
#include "include/bucc_parser.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

void bucc_parser_init(bucc_parser_t* parser, bucc_lexer_t* lexer, bucc_diag_t* diag) {
    parser->lexer = lexer;
    parser->diag = diag;
    parser->had_error = false;
    parser->panic_mode = false;
    bucc_lexer_next(lexer);
}

static bucc_token_t* current(bucc_parser_t* p) {
    return &p->lexer->current;
}

static bucc_token_t advance(bucc_parser_t* p) {
    return bucc_lexer_next(p->lexer);
}

static bool check(bucc_parser_t* p, bucc_token_kind_t kind) {
    return current(p)->kind == kind;
}

static bool match(bucc_parser_t* p, bucc_token_kind_t kind) {
    if (check(p, kind)) {
        advance(p);
        return true;
    }
    return false;
}

static void error(bucc_parser_t* p, const char* msg) {
    if (p->panic_mode) return;
    p->panic_mode = true;
    p->had_error = true;
    bucc_diag_error(p->diag, current(p)->span, BUCC_ERR_SYNTAX, "%s", msg);
}

__attribute__((unused))
static void error_at(bucc_parser_t* p, bucc_source_span_t span, const char* msg) {
    if (p->panic_mode) return;
    p->panic_mode = true;
    p->had_error = true;
    bucc_diag_error(p->diag, span, BUCC_ERR_SYNTAX, "%s", msg);
}

static bool expect(bucc_parser_t* p, bucc_token_kind_t kind, const char* msg) {
    if (check(p, kind)) {
        advance(p);
        return true;
    }
    error(p, msg);
    return false;
}

static void skip_newlines(bucc_parser_t* p) {
    while (check(p, TOK_NEWLINE) || check(p, TOK_COMMENT)) {
        advance(p);
    }
}

static bool expect_newline(bucc_parser_t* p) {
    if (check(p, TOK_NEWLINE) || check(p, TOK_EOF)) {
        if (check(p, TOK_NEWLINE)) advance(p);
        return true;
    }
    if (check(p, TOK_COMMENT)) {
        advance(p);
        if (check(p, TOK_NEWLINE)) advance(p);
        return true;
    }
    error(p, "expected newline");
    return false;
}

static void synchronize(bucc_parser_t* p) {
    p->panic_mode = false;
    
    while (!check(p, TOK_EOF)) {
        if (check(p, TOK_NEWLINE)) {
            advance(p);
            return;
        }
        
        switch (current(p)->kind) {
            case TOK_KW_SUB:
            case TOK_KW_FUNCTION:
            case TOK_KW_DIM:
            case TOK_KW_IF:
            case TOK_KW_FOR:
            case TOK_KW_WHILE:
            case TOK_KW_DO:
            case TOK_KW_SELECT:
            case TOK_KW_TRY:
            case TOK_KW_RETURN:
            case TOK_KW_END:
                return;
            default:
                advance(p);
        }
    }
}

static char* extract_string_content(const char* str, size_t len) {
    if (len < 2) return strdup("");
    
    size_t content_len = len - 2;
    char* result = malloc(content_len + 1);
    if (!result) return NULL;
    
    size_t j = 0;
    for (size_t i = 1; i < len - 1; i++) {
        if (str[i] == '\\' && i + 1 < len - 1) {
            i++;
            switch (str[i]) {
                case 'n': result[j++] = '\n'; break;
                case 'r': result[j++] = '\r'; break;
                case 't': result[j++] = '\t'; break;
                case '\\': result[j++] = '\\'; break;
                case '"': result[j++] = '"'; break;
                default: result[j++] = str[i]; break;
            }
        } else {
            result[j++] = str[i];
        }
    }
    result[j] = '\0';
    
    return result;
}

static bucc_date_t parse_date_literal(const char* str, size_t len) {
    bucc_date_t date = {0};
    if (len < 12) return date;
    
    date.year = (int16_t)(
        (str[1] - '0') * 1000 +
        (str[2] - '0') * 100 +
        (str[3] - '0') * 10 +
        (str[4] - '0')
    );
    date.month = (uint8_t)((str[6] - '0') * 10 + (str[7] - '0'));
    date.day = (uint8_t)((str[9] - '0') * 10 + (str[10] - '0'));
    
    return date;
}

static bucc_datetime_t parse_datetime_literal(const char* str, size_t len) {
    bucc_datetime_t dt = {0};
    if (len < 21) return dt;
    
    dt.year = (int16_t)(
        (str[1] - '0') * 1000 +
        (str[2] - '0') * 100 +
        (str[3] - '0') * 10 +
        (str[4] - '0')
    );
    dt.month = (uint8_t)((str[6] - '0') * 10 + (str[7] - '0'));
    dt.day = (uint8_t)((str[9] - '0') * 10 + (str[10] - '0'));
    dt.hour = (uint8_t)((str[12] - '0') * 10 + (str[13] - '0'));
    dt.minute = (uint8_t)((str[15] - '0') * 10 + (str[16] - '0'));
    dt.second = (uint8_t)((str[18] - '0') * 10 + (str[19] - '0'));
    
    return dt;
}

bucc_node_t* bucc_parse_type_ref(bucc_parser_t* p) {
    bucc_source_span_t span = current(p)->span;
    
    if (match(p, TOK_KW_ARRAY)) {
        expect(p, TOK_KW_OF, "expected 'OF' after 'ARRAY'");
        bucc_node_t* elem = bucc_parse_type_ref(p);
        return bucc_ast_type_array(span, elem);
    }
    
    if (match(p, TOK_KW_MAP)) {
        expect(p, TOK_KW_OF, "expected 'OF' after 'MAP'");
        expect(p, TOK_KW_STRING, "expected 'STRING' after 'MAP OF'");
        expect(p, TOK_KW_TO, "expected 'TO' after 'MAP OF STRING'");
        bucc_node_t* val = bucc_parse_type_ref(p);
        return bucc_ast_type_map(span, val);
    }
    
    bucc_scalar_type_t scalar;
    switch (current(p)->kind) {
        case TOK_KW_INTEGER:  scalar = SCALAR_INTEGER; break;
        case TOK_KW_DOUBLE:   scalar = SCALAR_DOUBLE; break;
        case TOK_KW_BOOLEAN:  scalar = SCALAR_BOOLEAN; break;
        case TOK_KW_STRING:   scalar = SCALAR_STRING; break;
        case TOK_KW_DATE:     scalar = SCALAR_DATE; break;
        case TOK_KW_DATETIME: scalar = SCALAR_DATETIME; break;
        default:
            error(p, "expected type name");
            return NULL;
    }
    advance(p);
    return bucc_ast_type_scalar(span, scalar);
}

bucc_node_t* bucc_parse_param_list(bucc_parser_t* p, bucc_node_list_t* params) {
    if (check(p, TOK_RPAREN)) return NULL;
    
    do {
        bucc_source_span_t span = current(p)->span;
        
        if (!check(p, TOK_IDENT)) {
            error(p, "expected parameter name");
            return NULL;
        }
        char* name = bucc_token_text(current(p));
        advance(p);
        
        expect(p, TOK_KW_AS, "expected 'AS' after parameter name");
        bucc_node_t* type = bucc_parse_type_ref(p);
        
        bucc_node_t* param = bucc_ast_param_decl(span, name, type);
        free(name);
        bucc_node_list_push(params, param);
        
    } while (match(p, TOK_COMMA));
    
    return NULL;
}

bucc_node_t* bucc_parse_expression(bucc_parser_t* p);

bucc_node_t* bucc_parse_primary_expr(bucc_parser_t* p) {
    bucc_source_span_t span = current(p)->span;
    
    if (match(p, TOK_KW_NULL)) {
        return bucc_ast_expr_literal_null(span);
    }
    
    if (match(p, TOK_KW_TRUE)) {
        return bucc_ast_expr_literal_bool(span, true);
    }
    
    if (match(p, TOK_KW_FALSE)) {
        return bucc_ast_expr_literal_bool(span, false);
    }
    
    if (check(p, TOK_INTEGER) || check(p, TOK_HEX)) {
        int64_t val = current(p)->value.i64;
        advance(p);
        return bucc_ast_expr_literal_i64(span, val);
    }
    
    if (check(p, TOK_DOUBLE)) {
        double val = current(p)->value.f64;
        advance(p);
        return bucc_ast_expr_literal_f64(span, val);
    }
    
    if (check(p, TOK_STRING)) {
        char* content = extract_string_content(current(p)->start, current(p)->len);
        advance(p);
        bucc_node_t* node = bucc_ast_expr_literal_string(span, content, strlen(content));
        free(content);
        return node;
    }
    
    if (check(p, TOK_DATE)) {
        bucc_date_t date = parse_date_literal(current(p)->start, current(p)->len);
        advance(p);
        return bucc_ast_expr_literal_date(span, date);
    }
    
    if (check(p, TOK_DATETIME)) {
        bucc_datetime_t dt = parse_datetime_literal(current(p)->start, current(p)->len);
        advance(p);
        return bucc_ast_expr_literal_datetime(span, dt);
    }
    
    if (check(p, TOK_IDENT)) {
        char* name = bucc_token_text(current(p));
        advance(p);
        bucc_node_t* node = bucc_ast_expr_ident(span, name);
        free(name);
        return node;
    }
    
    if (match(p, TOK_LPAREN)) {
        bucc_node_t* expr = bucc_parse_expression(p);
        expect(p, TOK_RPAREN, "expected ')' after expression");
        return expr;
    }
    
    if (match(p, TOK_LBRACKET)) {
        bucc_node_t* arr = bucc_ast_expr_array_lit(span);
        if (!check(p, TOK_RBRACKET)) {
            do {
                bucc_node_t* elem = bucc_parse_expression(p);
                bucc_node_list_push(&arr->data.array_lit.elements, elem);
            } while (match(p, TOK_COMMA));
        }
        expect(p, TOK_RBRACKET, "expected ']' after array elements");
        return arr;
    }
    
    if (match(p, TOK_LBRACE)) {
        bucc_node_t* map = bucc_ast_expr_map_lit(span);
        if (!check(p, TOK_RBRACE)) {
            do {
                if (!check(p, TOK_STRING)) {
                    error(p, "expected string key in map literal");
                    break;
                }
                char* key = extract_string_content(current(p)->start, current(p)->len);
                bucc_source_span_t pair_span = current(p)->span;
                advance(p);
                
                expect(p, TOK_COLON, "expected ':' after map key");
                bucc_node_t* val = bucc_parse_expression(p);
                
                bucc_node_t* pair = bucc_ast_expr_map_pair(pair_span, key, val);
                free(key);
                bucc_node_list_push(&map->data.map_lit.pairs, pair);
            } while (match(p, TOK_COMMA));
        }
        expect(p, TOK_RBRACE, "expected '}' after map pairs");
        return map;
    }
    
    error(p, "expected expression");
    return NULL;
}

bucc_node_t* bucc_parse_postfix_expr(bucc_parser_t* p) {
    bucc_node_t* expr = bucc_parse_primary_expr(p);
    if (!expr) return NULL;
    
    while (true) {
        bucc_source_span_t span = current(p)->span;
        
        if (match(p, TOK_LPAREN)) {
            bucc_node_t* call = bucc_ast_expr_call(span, expr);
            if (!check(p, TOK_RPAREN)) {
                do {
                    bucc_node_t* arg = bucc_parse_expression(p);
                    bucc_node_list_push(&call->data.call.args, arg);
                } while (match(p, TOK_COMMA));
            }
            expect(p, TOK_RPAREN, "expected ')' after arguments");
            expr = call;
            continue;
        }
        
        if (match(p, TOK_LBRACKET)) {
            bucc_node_t* idx = bucc_parse_expression(p);
            expect(p, TOK_RBRACKET, "expected ']' after index");
            expr = bucc_ast_expr_index(span, expr, idx);
            continue;
        }
        
        if (match(p, TOK_DOT)) {
            if (!check(p, TOK_IDENT)) {
                error(p, "expected member name after '.'");
                return expr;
            }
            char* member = bucc_token_text(current(p));
            advance(p);
            
            if (expr->kind == NODE_EXPR_IDENT) {
                bucc_node_t* qual = bucc_ast_expr_qualified(span,
                    expr->data.ident.name, member);
                bucc_node_free(expr);
                free(member);
                expr = qual;
            } else {
                expr = bucc_ast_expr_member(span, expr, member);
                free(member);
            }
            continue;
        }
        
        break;
    }
    
    return expr;
}

bucc_node_t* bucc_parse_unary_expr(bucc_parser_t* p) {
    bucc_source_span_t span = current(p)->span;
    
    if (match(p, TOK_MINUS)) {
        bucc_node_t* operand = bucc_parse_unary_expr(p);
        return bucc_ast_expr_unary(span, UNARY_NEG, operand);
    }
    
    if (match(p, TOK_KW_NOT)) {
        bucc_node_t* operand = bucc_parse_unary_expr(p);
        return bucc_ast_expr_unary(span, UNARY_NOT, operand);
    }
    
    return bucc_parse_postfix_expr(p);
}

bucc_node_t* bucc_parse_multiplicative_expr(bucc_parser_t* p) {
    bucc_node_t* left = bucc_parse_unary_expr(p);
    
    while (check(p, TOK_STAR) || check(p, TOK_SLASH) || check(p, TOK_KW_MOD)) {
        bucc_source_span_t span = current(p)->span;
        bucc_binary_op_t op;
        
        if (match(p, TOK_STAR)) op = BINARY_MUL;
        else if (match(p, TOK_SLASH)) op = BINARY_DIV;
        else { match(p, TOK_KW_MOD); op = BINARY_MOD; }
        
        bucc_node_t* right = bucc_parse_unary_expr(p);
        left = bucc_ast_expr_binary(span, op, left, right);
    }
    
    return left;
}

bucc_node_t* bucc_parse_additive_expr(bucc_parser_t* p) {
    bucc_node_t* left = bucc_parse_multiplicative_expr(p);
    
    while (check(p, TOK_PLUS) || check(p, TOK_MINUS)) {
        bucc_source_span_t span = current(p)->span;
        bucc_binary_op_t op = match(p, TOK_PLUS) ? BINARY_ADD : BINARY_SUB;
        if (op != BINARY_ADD) advance(p);
        
        bucc_node_t* right = bucc_parse_multiplicative_expr(p);
        left = bucc_ast_expr_binary(span, op, left, right);
    }
    
    return left;
}

bucc_node_t* bucc_parse_relational_expr(bucc_parser_t* p) {
    bucc_node_t* left = bucc_parse_additive_expr(p);
    
    while (check(p, TOK_LT) || check(p, TOK_LE) || check(p, TOK_GT) || check(p, TOK_GE)) {
        bucc_source_span_t span = current(p)->span;
        bucc_binary_op_t op;
        
        if (match(p, TOK_LT)) op = BINARY_LT;
        else if (match(p, TOK_LE)) op = BINARY_LE;
        else if (match(p, TOK_GT)) op = BINARY_GT;
        else { match(p, TOK_GE); op = BINARY_GE; }
        
        bucc_node_t* right = bucc_parse_additive_expr(p);
        left = bucc_ast_expr_binary(span, op, left, right);
    }
    
    return left;
}

bucc_node_t* bucc_parse_equality_expr(bucc_parser_t* p) {
    bucc_node_t* left = bucc_parse_relational_expr(p);
    
    while (check(p, TOK_EQ) || check(p, TOK_NE)) {
        bucc_source_span_t span = current(p)->span;
        bucc_binary_op_t op = match(p, TOK_EQ) ? BINARY_EQ : BINARY_NE;
        if (op != BINARY_EQ) advance(p);
        
        bucc_node_t* right = bucc_parse_relational_expr(p);
        left = bucc_ast_expr_binary(span, op, left, right);
    }
    
    return left;
}

bucc_node_t* bucc_parse_and_expr(bucc_parser_t* p) {
    bucc_node_t* left = bucc_parse_equality_expr(p);
    
    while (match(p, TOK_KW_AND)) {
        bucc_source_span_t span = current(p)->span;
        bucc_node_t* right = bucc_parse_equality_expr(p);
        left = bucc_ast_expr_binary(span, BINARY_AND, left, right);
    }
    
    return left;
}

bucc_node_t* bucc_parse_or_expr(bucc_parser_t* p) {
    bucc_node_t* left = bucc_parse_and_expr(p);
    
    while (match(p, TOK_KW_OR)) {
        bucc_source_span_t span = current(p)->span;
        bucc_node_t* right = bucc_parse_and_expr(p);
        left = bucc_ast_expr_binary(span, BINARY_OR, left, right);
    }
    
    return left;
}

bucc_node_t* bucc_parse_expression(bucc_parser_t* p) {
    return bucc_parse_or_expr(p);
}

bucc_node_t* bucc_parse_var_decl(bucc_parser_t* p, bool is_global) {
    bucc_source_span_t span = current(p)->span;
    
    expect(p, TOK_KW_DIM, "expected 'DIM'");
    
    if (!check(p, TOK_IDENT)) {
        error(p, "expected variable name");
        return NULL;
    }
    char* name = bucc_token_text(current(p));
    advance(p);
    
    expect(p, TOK_KW_AS, "expected 'AS' after variable name");
    bucc_node_t* type = bucc_parse_type_ref(p);
    
    bucc_node_t* init = NULL;
    if (match(p, TOK_EQ)) {
        init = bucc_parse_expression(p);
    }
    
    bucc_node_t* decl = bucc_ast_var_decl(span, name, type, init, is_global);
    free(name);
    return decl;
}

bucc_node_t* bucc_parse_if_stmt(bucc_parser_t* p) {
    bucc_source_span_t span = current(p)->span;
    
    expect(p, TOK_KW_IF, "expected 'IF'");
    bucc_node_t* cond = bucc_parse_expression(p);
    expect(p, TOK_KW_THEN, "expected 'THEN' after condition");
    expect_newline(p);
    
    bucc_node_t* then_block = bucc_ast_block(current(p)->span);
    while (!check(p, TOK_KW_ELSEIF) && !check(p, TOK_KW_ELSE) &&
           !check(p, TOK_KW_END) && !check(p, TOK_EOF)) {
        skip_newlines(p);
        if (check(p, TOK_KW_ELSEIF) || check(p, TOK_KW_ELSE) || check(p, TOK_KW_END)) break;
        bucc_node_t* stmt = bucc_parse_statement(p);
        if (stmt) bucc_node_list_push(&then_block->data.block.stmts, stmt);
    }
    
    bucc_node_t* if_node = bucc_ast_stmt_if(span, cond, then_block);
    
    while (match(p, TOK_KW_ELSEIF)) {
        bucc_node_t* elseif_cond = bucc_parse_expression(p);
        expect(p, TOK_KW_THEN, "expected 'THEN' after ELSEIF condition");
        expect_newline(p);
        
        bucc_node_t* elseif_block = bucc_ast_block(current(p)->span);
        while (!check(p, TOK_KW_ELSEIF) && !check(p, TOK_KW_ELSE) &&
               !check(p, TOK_KW_END) && !check(p, TOK_EOF)) {
            skip_newlines(p);
            if (check(p, TOK_KW_ELSEIF) || check(p, TOK_KW_ELSE) || check(p, TOK_KW_END)) break;
            bucc_node_t* stmt = bucc_parse_statement(p);
            if (stmt) bucc_node_list_push(&elseif_block->data.block.stmts, stmt);
        }
        
        bucc_node_t* clause = bucc_ast_stmt_if(current(p)->span, elseif_cond, elseif_block);
        bucc_node_list_push(&if_node->data.if_stmt.elseif_clauses, clause);
    }
    
    if (match(p, TOK_KW_ELSE)) {
        expect_newline(p);
        bucc_node_t* else_block = bucc_ast_block(current(p)->span);
        while (!check(p, TOK_KW_END) && !check(p, TOK_EOF)) {
            skip_newlines(p);
            if (check(p, TOK_KW_END)) break;
            bucc_node_t* stmt = bucc_parse_statement(p);
            if (stmt) bucc_node_list_push(&else_block->data.block.stmts, stmt);
        }
        if_node->data.if_stmt.else_block = else_block;
    }
    
    expect(p, TOK_KW_END, "expected 'END IF'");
    expect(p, TOK_KW_IF, "expected 'IF' after 'END'");
    expect_newline(p);
    
    return if_node;
}

bucc_node_t* bucc_parse_select_stmt(bucc_parser_t* p) {
    bucc_source_span_t span = current(p)->span;
    
    expect(p, TOK_KW_SELECT, "expected 'SELECT'");
    expect(p, TOK_KW_CASE, "expected 'CASE' after 'SELECT'");
    bucc_node_t* expr = bucc_parse_expression(p);
    expect_newline(p);
    
    bucc_node_t* select = bucc_ast_stmt_select(span, expr);
    
    skip_newlines(p);
    while (check(p, TOK_KW_CASE)) {
        bucc_source_span_t case_span = current(p)->span;
        advance(p);
        
        if (match(p, TOK_KW_ELSE)) {
            expect_newline(p);
            bucc_node_t* body = bucc_ast_block(current(p)->span);
            while (!check(p, TOK_KW_END) && !check(p, TOK_EOF)) {
                skip_newlines(p);
                if (check(p, TOK_KW_END)) break;
                bucc_node_t* stmt = bucc_parse_statement(p);
                if (stmt) bucc_node_list_push(&body->data.block.stmts, stmt);
            }
            select->data.select_stmt.else_case = bucc_ast_stmt_case(case_span, body, true);
            break;
        }
        
        bucc_node_t* case_node = bucc_ast_stmt_case(case_span, NULL, false);
        do {
            bucc_node_t* val = bucc_parse_expression(p);
            bucc_node_list_push(&case_node->data.case_clause.values, val);
        } while (match(p, TOK_COMMA));
        
        expect_newline(p);
        
        bucc_node_t* body = bucc_ast_block(current(p)->span);
        while (!check(p, TOK_KW_CASE) && !check(p, TOK_KW_END) && !check(p, TOK_EOF)) {
            skip_newlines(p);
            if (check(p, TOK_KW_CASE) || check(p, TOK_KW_END)) break;
            bucc_node_t* stmt = bucc_parse_statement(p);
            if (stmt) bucc_node_list_push(&body->data.block.stmts, stmt);
        }
        case_node->data.case_clause.body = body;
        
        bucc_node_list_push(&select->data.select_stmt.cases, case_node);
        skip_newlines(p);
    }
    
    expect(p, TOK_KW_END, "expected 'END SELECT'");
    expect(p, TOK_KW_SELECT, "expected 'SELECT' after 'END'");
    expect_newline(p);
    
    return select;
}

bucc_node_t* bucc_parse_while_stmt(bucc_parser_t* p) {
    bucc_source_span_t span = current(p)->span;
    
    expect(p, TOK_KW_WHILE, "expected 'WHILE'");
    bucc_node_t* cond = bucc_parse_expression(p);
    expect_newline(p);
    
    bucc_node_t* body = bucc_ast_block(current(p)->span);
    while (!check(p, TOK_KW_WEND) && !check(p, TOK_EOF)) {
        skip_newlines(p);
        if (check(p, TOK_KW_WEND)) break;
        bucc_node_t* stmt = bucc_parse_statement(p);
        if (stmt) bucc_node_list_push(&body->data.block.stmts, stmt);
    }
    
    expect(p, TOK_KW_WEND, "expected 'WEND'");
    expect_newline(p);
    
    return bucc_ast_stmt_while(span, cond, body);
}

bucc_node_t* bucc_parse_do_loop_stmt(bucc_parser_t* p) {
    bucc_source_span_t span = current(p)->span;
    
    expect(p, TOK_KW_DO, "expected 'DO'");
    expect_newline(p);
    
    bucc_node_t* body = bucc_ast_block(current(p)->span);
    while (!check(p, TOK_KW_LOOP) && !check(p, TOK_EOF)) {
        skip_newlines(p);
        if (check(p, TOK_KW_LOOP)) break;
        bucc_node_t* stmt = bucc_parse_statement(p);
        if (stmt) bucc_node_list_push(&body->data.block.stmts, stmt);
    }
    
    expect(p, TOK_KW_LOOP, "expected 'LOOP'");
    
    bucc_loop_kind_t kind = LOOP_PLAIN;
    bucc_node_t* cond = NULL;
    
    if (match(p, TOK_KW_UNTIL)) {
        kind = LOOP_UNTIL;
        cond = bucc_parse_expression(p);
    } else if (match(p, TOK_KW_WHILE)) {
        kind = LOOP_WHILE;
        cond = bucc_parse_expression(p);
    }
    
    expect_newline(p);
    
    return bucc_ast_stmt_do_loop(span, body, cond, kind);
}

bucc_node_t* bucc_parse_for_stmt(bucc_parser_t* p) {
    bucc_source_span_t span = current(p)->span;
    
    expect(p, TOK_KW_FOR, "expected 'FOR'");
    
    if (!check(p, TOK_IDENT)) {
        error(p, "expected loop variable");
        return NULL;
    }
    char* var = bucc_token_text(current(p));
    advance(p);
    
    expect(p, TOK_EQ, "expected '=' after loop variable");
    bucc_node_t* start = bucc_parse_expression(p);
    
    expect(p, TOK_KW_TO, "expected 'TO'");
    bucc_node_t* end = bucc_parse_expression(p);
    
    bucc_node_t* step = NULL;
    if (match(p, TOK_KW_STEP)) {
        step = bucc_parse_expression(p);
    }
    
    expect_newline(p);
    
    bucc_node_t* body = bucc_ast_block(current(p)->span);
    while (!check(p, TOK_KW_NEXT) && !check(p, TOK_EOF)) {
        skip_newlines(p);
        if (check(p, TOK_KW_NEXT)) break;
        bucc_node_t* stmt = bucc_parse_statement(p);
        if (stmt) bucc_node_list_push(&body->data.block.stmts, stmt);
    }
    
    expect(p, TOK_KW_NEXT, "expected 'NEXT'");
    if (check(p, TOK_IDENT)) {
        advance(p);
    }
    expect_newline(p);
    
    bucc_node_t* node = bucc_ast_stmt_for(span, var, start, end, step, body);
    free(var);
    return node;
}

bucc_node_t* bucc_parse_try_catch_stmt(bucc_parser_t* p) {
    bucc_source_span_t span = current(p)->span;
    
    expect(p, TOK_KW_TRY, "expected 'TRY'");
    expect_newline(p);
    
    bucc_node_t* try_block = bucc_ast_block(current(p)->span);
    while (!check(p, TOK_KW_CATCH) && !check(p, TOK_EOF)) {
        skip_newlines(p);
        if (check(p, TOK_KW_CATCH)) break;
        bucc_node_t* stmt = bucc_parse_statement(p);
        if (stmt) bucc_node_list_push(&try_block->data.block.stmts, stmt);
    }
    
    expect(p, TOK_KW_CATCH, "expected 'CATCH'");
    
    char* error_var = NULL;
    if (check(p, TOK_IDENT)) {
        error_var = bucc_token_text(current(p));
        advance(p);
    }
    expect_newline(p);
    
    bucc_node_t* catch_block = bucc_ast_block(current(p)->span);
    while (!check(p, TOK_KW_END) && !check(p, TOK_EOF)) {
        skip_newlines(p);
        if (check(p, TOK_KW_END)) break;
        bucc_node_t* stmt = bucc_parse_statement(p);
        if (stmt) bucc_node_list_push(&catch_block->data.block.stmts, stmt);
    }
    
    expect(p, TOK_KW_END, "expected 'END TRY'");
    expect(p, TOK_KW_TRY, "expected 'TRY' after 'END'");
    expect_newline(p);
    
    bucc_node_t* node = bucc_ast_stmt_try_catch(span, try_block, error_var, catch_block);
    free(error_var);
    return node;
}

bucc_node_t* bucc_parse_statement(bucc_parser_t* p) {
    skip_newlines(p);
    
    if (check(p, TOK_EOF)) return NULL;
    
    bucc_source_span_t span = current(p)->span;
    
    if (check(p, TOK_KW_DIM)) {
        bucc_node_t* decl = bucc_parse_var_decl(p, false);
        expect_newline(p);
        return bucc_ast_stmt_var_decl(span, decl);
    }
    
    if (check(p, TOK_KW_IF)) {
        return bucc_parse_if_stmt(p);
    }
    
    if (check(p, TOK_KW_SELECT)) {
        return bucc_parse_select_stmt(p);
    }
    
    if (check(p, TOK_KW_WHILE)) {
        return bucc_parse_while_stmt(p);
    }
    
    if (check(p, TOK_KW_DO)) {
        return bucc_parse_do_loop_stmt(p);
    }
    
    if (check(p, TOK_KW_FOR)) {
        return bucc_parse_for_stmt(p);
    }
    
    if (check(p, TOK_KW_TRY)) {
        return bucc_parse_try_catch_stmt(p);
    }
    
    if (match(p, TOK_KW_RETURN)) {
        bucc_node_t* value = NULL;
        if (!check(p, TOK_NEWLINE) && !check(p, TOK_EOF) && !check(p, TOK_COMMENT)) {
            value = bucc_parse_expression(p);
        }
        expect_newline(p);
        return bucc_ast_stmt_return(span, value);
    }
    
    if (match(p, TOK_KW_EXIT)) {
        bucc_exit_kind_t kind;
        if (match(p, TOK_KW_FOR)) kind = EXIT_FOR;
        else if (match(p, TOK_KW_DO)) kind = EXIT_DO;
        else if (match(p, TOK_KW_WHILE)) kind = EXIT_WHILE;
        else if (match(p, TOK_KW_SELECT)) kind = EXIT_SELECT;
        else {
            error(p, "expected FOR, DO, WHILE, or SELECT after EXIT");
            return NULL;
        }
        expect_newline(p);
        return bucc_ast_stmt_exit(span, kind);
    }
    
    if (match(p, TOK_KW_HALT)) {
        expect_newline(p);
        return bucc_ast_stmt_halt(span);
    }
    
    if (match(p, TOK_KW_THROW)) {
        bucc_node_t* expr = bucc_parse_expression(p);
        expect_newline(p);
        return bucc_ast_stmt_throw(span, expr);
    }
    
    if (match(p, TOK_KW_CHAIN)) {
        if (!check(p, TOK_STRING)) {
            error(p, "expected string literal after CHAIN");
            return NULL;
        }
        char* target = extract_string_content(current(p)->start, current(p)->len);
        advance(p);
        
        bucc_node_t* args = NULL;
        if (match(p, TOK_COMMA)) {
            args = bucc_parse_expression(p);
        }
        expect_newline(p);
        
        bucc_node_t* node = bucc_ast_stmt_chain(span, target, args);
        free(target);
        return node;
    }
    
    if (match(p, TOK_KW_ON)) {
        bucc_node_t* selector = bucc_parse_expression(p);
        expect(p, TOK_KW_CALL, "expected 'CALL' after ON expression");
        
        bucc_node_t* on_call = bucc_ast_stmt_on_call(span, selector);
        do {
            if (!check(p, TOK_IDENT)) {
                error(p, "expected procedure name");
                break;
            }
            char* name = bucc_token_text(current(p));
            bucc_node_t* target = bucc_ast_expr_ident(current(p)->span, name);
            free(name);
            advance(p);
            bucc_node_list_push(&on_call->data.on_call.targets, target);
        } while (match(p, TOK_COMMA));
        
        expect_newline(p);
        return on_call;
    }
    
    if (match(p, TOK_KW_LET)) {
    }
    
    if (match(p, TOK_KW_CALL)) {
    }
    
    bucc_node_t* expr = bucc_parse_expression(p);
    if (!expr) {
        synchronize(p);
        return NULL;
    }
    
    if (match(p, TOK_EQ)) {
        bucc_node_t* value = bucc_parse_expression(p);
        expect_newline(p);
        return bucc_ast_stmt_assign(span, expr, value);
    }
    
    expect_newline(p);
    return bucc_ast_stmt_expr(span, expr);
}

bucc_node_t* bucc_parse_block(bucc_parser_t* p) {
    bucc_node_t* block = bucc_ast_block(current(p)->span);
    
    while (!check(p, TOK_KW_END) && !check(p, TOK_EOF)) {
        skip_newlines(p);
        if (check(p, TOK_KW_END)) break;
        
        bucc_node_t* stmt = bucc_parse_statement(p);
        if (stmt) {
            bucc_node_list_push(&block->data.block.stmts, stmt);
        }
        
        if (p->panic_mode) {
            synchronize(p);
        }
    }
    
    return block;
}

bucc_node_t* bucc_parse_procedure(bucc_parser_t* p) {
    bucc_source_span_t span = current(p)->span;
    
    expect(p, TOK_KW_SUB, "expected 'SUB'");
    
    if (!check(p, TOK_IDENT)) {
        error(p, "expected procedure name");
        return NULL;
    }
    char* name = bucc_token_text(current(p));
    advance(p);
    
    expect(p, TOK_LPAREN, "expected '(' after procedure name");
    
    bucc_node_t* proc = bucc_ast_proc_decl(span, name, NULL, false);
    bucc_parse_param_list(p, &proc->data.proc_decl.params);
    
    expect(p, TOK_RPAREN, "expected ')' after parameters");
    expect_newline(p);
    
    proc->data.proc_decl.body = bucc_parse_block(p);
    
    expect(p, TOK_KW_END, "expected 'END SUB'");
    expect(p, TOK_KW_SUB, "expected 'SUB' after 'END'");
    expect_newline(p);
    
    free(name);
    return proc;
}

bucc_node_t* bucc_parse_function(bucc_parser_t* p) {
    bucc_source_span_t span = current(p)->span;
    
    expect(p, TOK_KW_FUNCTION, "expected 'FUNCTION'");
    
    if (!check(p, TOK_IDENT)) {
        error(p, "expected function name");
        return NULL;
    }
    char* name = bucc_token_text(current(p));
    advance(p);
    
    expect(p, TOK_LPAREN, "expected '(' after function name");
    
    bucc_node_t* func = bucc_ast_func_decl(span, name, NULL, NULL);
    bucc_parse_param_list(p, &func->data.func_decl.params);
    
    expect(p, TOK_RPAREN, "expected ')' after parameters");
    expect(p, TOK_KW_AS, "expected 'AS' after parameters");
    
    func->data.func_decl.return_type = bucc_parse_type_ref(p);
    expect_newline(p);
    
    func->data.func_decl.body = bucc_parse_block(p);
    
    expect(p, TOK_KW_END, "expected 'END FUNCTION'");
    expect(p, TOK_KW_FUNCTION, "expected 'FUNCTION' after 'END'");
    expect_newline(p);
    
    free(name);
    return func;
}

bucc_node_t* bucc_parse_metadata(bucc_parser_t* p) {
    bucc_source_span_t span = current(p)->span;
    
    if (match(p, TOK_KW_PROGRAM)) {
        if (!check(p, TOK_STRING)) {
            error(p, "expected string after PROGRAM");
            return NULL;
        }
        char* name = extract_string_content(current(p)->start, current(p)->len);
        advance(p);
        expect_newline(p);
        bucc_node_t* node = bucc_ast_meta_program(span, name);
        free(name);
        return node;
    }
    
    if (match(p, TOK_KW_VERSION)) {
        if (!check(p, TOK_STRING)) {
            error(p, "expected string after VERSION");
            return NULL;
        }
        char* ver = extract_string_content(current(p)->start, current(p)->len);
        advance(p);
        expect_newline(p);
        bucc_node_t* node = bucc_ast_meta_version(span, ver);
        free(ver);
        return node;
    }
    
    if (match(p, TOK_KW_AUTHOR)) {
        if (!check(p, TOK_STRING)) {
            error(p, "expected string after AUTHOR");
            return NULL;
        }
        char* author = extract_string_content(current(p)->start, current(p)->len);
        advance(p);
        expect_newline(p);
        bucc_node_t* node = bucc_ast_meta_author(span, author);
        free(author);
        return node;
    }
    
    if (match(p, TOK_KW_DESCRIPTION)) {
        if (!check(p, TOK_STRING)) {
            error(p, "expected string after DESCRIPTION");
            return NULL;
        }
        char* desc = extract_string_content(current(p)->start, current(p)->len);
        advance(p);
        expect_newline(p);
        bucc_node_t* node = bucc_ast_meta_description(span, desc);
        free(desc);
        return node;
    }
    
    if (match(p, TOK_KW_CAPABILITY)) {
        if (!check(p, TOK_STRING)) {
            error(p, "expected string after CAPABILITY");
            return NULL;
        }
        char* cap = extract_string_content(current(p)->start, current(p)->len);
        advance(p);
        expect_newline(p);
        bucc_node_t* node = bucc_ast_meta_capability(span, cap);
        free(cap);
        return node;
    }
    
    if (match(p, TOK_KW_OPTION)) {
        if (!check(p, TOK_IDENT)) {
            error(p, "expected option name");
            return NULL;
        }
        char* name = bucc_token_text(current(p));
        advance(p);
        
        char* value = NULL;
        if (check(p, TOK_STRING)) {
            value = extract_string_content(current(p)->start, current(p)->len);
            advance(p);
        }
        expect_newline(p);
        
        bucc_node_t* node = bucc_ast_meta_option(span, name, value);
        free(name);
        free(value);
        return node;
    }
    
    if (match(p, TOK_KW_DATASET)) {
        if (!check(p, TOK_IDENT)) {
            error(p, "expected dataset name");
            return NULL;
        }
        char* name = bucc_token_text(current(p));
        advance(p);
        expect_newline(p);
        
        bucc_node_t* node = bucc_ast_meta_dataset(span, name);
        free(name);
        return node;
    }
    
    return NULL;
}

bucc_node_t* bucc_parse_global_decl(bucc_parser_t* p) {
    return bucc_parse_var_decl(p, true);
}

bucc_node_t* bucc_parse_module(bucc_parser_t* p) {
    bucc_node_t* module = bucc_ast_module_new(current(p)->span);
    
    skip_newlines(p);
    
    while (check(p, TOK_KW_PROGRAM) || check(p, TOK_KW_VERSION) ||
           check(p, TOK_KW_AUTHOR) || check(p, TOK_KW_DESCRIPTION) ||
           check(p, TOK_KW_CAPABILITY) || check(p, TOK_KW_OPTION) ||
           check(p, TOK_KW_DATASET)) {
        bucc_node_t* meta = bucc_parse_metadata(p);
        if (meta) {
            bucc_node_list_push(&module->data.module.metadata, meta);
        }
        skip_newlines(p);
    }
    
    while (check(p, TOK_KW_DIM)) {
        bucc_node_t* global = bucc_parse_global_decl(p);
        if (global) {
            bucc_node_list_push(&module->data.module.globals, global);
        }
        expect_newline(p);
        skip_newlines(p);
    }
    
    while (!check(p, TOK_EOF)) {
        skip_newlines(p);
        if (check(p, TOK_EOF)) break;
        
        if (check(p, TOK_KW_SUB)) {
            bucc_node_t* proc = bucc_parse_procedure(p);
            if (proc) {
                bucc_node_list_push(&module->data.module.procedures, proc);
            }
        } else if (check(p, TOK_KW_FUNCTION)) {
            bucc_node_t* func = bucc_parse_function(p);
            if (func) {
                bucc_node_list_push(&module->data.module.procedures, func);
            }
        } else {
            error(p, "expected SUB or FUNCTION declaration");
            synchronize(p);
        }
    }
    
    return module;
}

bool bucc_parser_had_error(bucc_parser_t* p) {
    return p->had_error;
}
