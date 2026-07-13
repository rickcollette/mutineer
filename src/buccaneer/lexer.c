/*
 * lexer.c - Buccaneer lexical analyzer implementation
 *
 * Part of the Buccaneer language implementation for Mutineer BBS.
 */

#define _POSIX_C_SOURCE 200809L
#include "include/bucc_lexer.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

typedef struct {
    const char*       name;
    size_t            len;
    bucc_token_kind_t kind;
} keyword_entry_t;

static const keyword_entry_t keywords[] = {
    {"PROGRAM",     7,  TOK_KW_PROGRAM},
    {"VERSION",     7,  TOK_KW_VERSION},
    {"AUTHOR",      6,  TOK_KW_AUTHOR},
    {"DESCRIPTION", 11, TOK_KW_DESCRIPTION},
    {"CAPABILITY",  10, TOK_KW_CAPABILITY},
    {"OPTION",      6,  TOK_KW_OPTION},
    {"DATASET",     7,  TOK_KW_DATASET},
    {"DIM",         3,  TOK_KW_DIM},
    {"AS",          2,  TOK_KW_AS},
    {"LET",         3,  TOK_KW_LET},
    {"CONST",       5,  TOK_KW_CONST},
    {"SUB",         3,  TOK_KW_SUB},
    {"FUNCTION",    8,  TOK_KW_FUNCTION},
    {"END",         3,  TOK_KW_END},
    {"CALL",        4,  TOK_KW_CALL},
    {"RETURN",      6,  TOK_KW_RETURN},
    {"IF",          2,  TOK_KW_IF},
    {"THEN",        4,  TOK_KW_THEN},
    {"ELSE",        4,  TOK_KW_ELSE},
    {"ELSEIF",      6,  TOK_KW_ELSEIF},
    {"SELECT",      6,  TOK_KW_SELECT},
    {"CASE",        4,  TOK_KW_CASE},
    {"FOR",         3,  TOK_KW_FOR},
    {"TO",          2,  TOK_KW_TO},
    {"STEP",        4,  TOK_KW_STEP},
    {"NEXT",        4,  TOK_KW_NEXT},
    {"WHILE",       5,  TOK_KW_WHILE},
    {"WEND",        4,  TOK_KW_WEND},
    {"DO",          2,  TOK_KW_DO},
    {"LOOP",        4,  TOK_KW_LOOP},
    {"UNTIL",       5,  TOK_KW_UNTIL},
    {"EXIT",        4,  TOK_KW_EXIT},
    {"HALT",        4,  TOK_KW_HALT},
    {"CHAIN",       5,  TOK_KW_CHAIN},
    {"TRY",         3,  TOK_KW_TRY},
    {"CATCH",       5,  TOK_KW_CATCH},
    {"THROW",       5,  TOK_KW_THROW},
    {"ON",          2,  TOK_KW_ON},
    {"AND",         3,  TOK_KW_AND},
    {"OR",          2,  TOK_KW_OR},
    {"NOT",         3,  TOK_KW_NOT},
    {"MOD",         3,  TOK_KW_MOD},
    {"TRUE",        4,  TOK_KW_TRUE},
    {"FALSE",       5,  TOK_KW_FALSE},
    {"NULL",        4,  TOK_KW_NULL},
    {"INTEGER",     7,  TOK_KW_INTEGER},
    {"DOUBLE",      6,  TOK_KW_DOUBLE},
    {"BOOLEAN",     7,  TOK_KW_BOOLEAN},
    {"STRING",      6,  TOK_KW_STRING},
    {"DATE",        4,  TOK_KW_DATE},
    {"DATETIME",    8,  TOK_KW_DATETIME},
    {"ARRAY",       5,  TOK_KW_ARRAY},
    {"MAP",         3,  TOK_KW_MAP},
    {"OF",          2,  TOK_KW_OF},
    {"REM",         3,  TOK_KW_REM},
    {NULL,          0,  TOK_EOF}
};

static int strcasecmp_n(const char* a, const char* b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        int ca = toupper((unsigned char)a[i]);
        int cb = toupper((unsigned char)b[i]);
        if (ca != cb) return ca - cb;
    }
    return 0;
}

static bucc_token_kind_t lookup_keyword(const char* text, size_t len) {
    for (const keyword_entry_t* kw = keywords; kw->name; kw++) {
        if (kw->len == len && strcasecmp_n(text, kw->name, len) == 0) {
            return kw->kind;
        }
    }
    return TOK_IDENT;
}

void bucc_lexer_init(bucc_lexer_t* lex, const char* source, size_t len,
                     uint32_t file_id, bucc_diag_t* diag) {
    memset(lex, 0, sizeof(*lex));
    lex->source = source;
    lex->source_len = len;
    lex->pos = 0;
    lex->line = 1;
    lex->col = 1;
    lex->file_id = file_id;
    lex->diag = diag;
    lex->has_peek = false;
}

static char peek_char(bucc_lexer_t* lex) {
    if (lex->pos >= lex->source_len) return '\0';
    return lex->source[lex->pos];
}

static char peek_char_n(bucc_lexer_t* lex, size_t n) {
    if (lex->pos + n >= lex->source_len) return '\0';
    return lex->source[lex->pos + n];
}

static char advance(bucc_lexer_t* lex) {
    if (lex->pos >= lex->source_len) return '\0';
    char c = lex->source[lex->pos++];
    if (c == '\n') {
        lex->line++;
        lex->col = 1;
    } else {
        lex->col++;
    }
    return c;
}

static void skip_whitespace(bucc_lexer_t* lex) {
    while (lex->pos < lex->source_len) {
        char c = peek_char(lex);
        if (c == ' ' || c == '\t' || c == '\r') {
            advance(lex);
        } else if (c == ':') {
            advance(lex);
        } else {
            break;
        }
    }
}

static bucc_token_t make_token(bucc_lexer_t* lex, bucc_token_kind_t kind,
                               const char* start, size_t len,
                               uint32_t start_line, uint32_t start_col) {
    bucc_token_t tok = {0};
    tok.kind = kind;
    tok.start = start;
    tok.len = len;
    tok.span.file_id = lex->file_id;
    tok.span.start_line = start_line;
    tok.span.start_col = start_col;
    tok.span.end_line = lex->line;
    tok.span.end_col = lex->col;
    return tok;
}

static bucc_token_t make_error(bucc_lexer_t* lex, const char* msg,
                               uint32_t start_line, uint32_t start_col) {
    bucc_source_span_t span = {
        .file_id = lex->file_id,
        .start_line = start_line,
        .start_col = start_col,
        .end_line = lex->line,
        .end_col = lex->col
    };
    bucc_diag_error(lex->diag, span, BUCC_ERR_SYNTAX, "%s", msg);
    
    bucc_token_t tok = {0};
    tok.kind = TOK_ERROR;
    tok.span = span;
    return tok;
}

static bool is_ident_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool is_ident_char(char c) {
    return is_ident_start(c) || (c >= '0' && c <= '9') || c == '$';
}

static bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

static bool is_hex_digit(char c) {
    return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static bucc_token_t scan_identifier(bucc_lexer_t* lex) {
    uint32_t start_line = lex->line;
    uint32_t start_col = lex->col;
    const char* start = lex->source + lex->pos;
    
    while (is_ident_char(peek_char(lex))) {
        advance(lex);
    }
    
    size_t len = (lex->source + lex->pos) - start;
    bucc_token_kind_t kind = lookup_keyword(start, len);
    
    return make_token(lex, kind, start, len, start_line, start_col);
}

static bucc_token_t scan_number(bucc_lexer_t* lex) {
    uint32_t start_line = lex->line;
    uint32_t start_col = lex->col;
    const char* start = lex->source + lex->pos;
    
    if (peek_char(lex) == '-') {
        advance(lex);
    }
    
    while (is_digit(peek_char(lex))) {
        advance(lex);
    }
    
    bool is_double = false;
    if (peek_char(lex) == '.' && is_digit(peek_char_n(lex, 1))) {
        is_double = true;
        advance(lex);
        while (is_digit(peek_char(lex))) {
            advance(lex);
        }
    }
    
    if (peek_char(lex) == 'e' || peek_char(lex) == 'E') {
        is_double = true;
        advance(lex);
        if (peek_char(lex) == '+' || peek_char(lex) == '-') {
            advance(lex);
        }
        if (!is_digit(peek_char(lex))) {
            return make_error(lex, "invalid number: expected exponent digits", start_line, start_col);
        }
        while (is_digit(peek_char(lex))) {
            advance(lex);
        }
    }
    
    size_t len = (lex->source + lex->pos) - start;
    bucc_token_t tok = make_token(lex, is_double ? TOK_DOUBLE : TOK_INTEGER,
                                  start, len, start_line, start_col);
    
    char* end;
    if (is_double) {
        tok.value.f64 = strtod(start, &end);
    } else {
        tok.value.i64 = strtoll(start, &end, 10);
    }
    
    return tok;
}

static bucc_token_t scan_hex(bucc_lexer_t* lex) {
    uint32_t start_line = lex->line;
    uint32_t start_col = lex->col;
    const char* start = lex->source + lex->pos;
    
    advance(lex);
    advance(lex);
    
    if (!is_hex_digit(peek_char(lex))) {
        return make_error(lex, "invalid hex literal: expected hex digits", start_line, start_col);
    }
    
    while (is_hex_digit(peek_char(lex))) {
        advance(lex);
    }
    
    size_t len = (lex->source + lex->pos) - start;
    bucc_token_t tok = make_token(lex, TOK_HEX, start, len, start_line, start_col);
    
    tok.value.i64 = strtoll(start + 2, NULL, 16);
    
    return tok;
}

static bucc_token_t scan_string(bucc_lexer_t* lex) {
    uint32_t start_line = lex->line;
    uint32_t start_col = lex->col;
    const char* start = lex->source + lex->pos;
    
    advance(lex);
    
    while (peek_char(lex) != '"' && peek_char(lex) != '\0' && peek_char(lex) != '\n') {
        if (peek_char(lex) == '\\' && peek_char_n(lex, 1) != '\0') {
            advance(lex);
        }
        advance(lex);
    }
    
    if (peek_char(lex) != '"') {
        return make_error(lex, "unterminated string literal", start_line, start_col);
    }
    
    advance(lex);
    
    size_t len = (lex->source + lex->pos) - start;
    return make_token(lex, TOK_STRING, start, len, start_line, start_col);
}

static bucc_token_t scan_date_or_datetime(bucc_lexer_t* lex) {
    uint32_t start_line = lex->line;
    uint32_t start_col = lex->col;
    const char* start = lex->source + lex->pos;
    
    advance(lex);
    
    for (int i = 0; i < 4; i++) {
        if (!is_digit(peek_char(lex))) {
            return make_error(lex, "invalid date literal: expected year", start_line, start_col);
        }
        advance(lex);
    }
    
    if (peek_char(lex) != '-') {
        return make_error(lex, "invalid date literal: expected '-'", start_line, start_col);
    }
    advance(lex);
    
    for (int i = 0; i < 2; i++) {
        if (!is_digit(peek_char(lex))) {
            return make_error(lex, "invalid date literal: expected month", start_line, start_col);
        }
        advance(lex);
    }
    
    if (peek_char(lex) != '-') {
        return make_error(lex, "invalid date literal: expected '-'", start_line, start_col);
    }
    advance(lex);
    
    for (int i = 0; i < 2; i++) {
        if (!is_digit(peek_char(lex))) {
            return make_error(lex, "invalid date literal: expected day", start_line, start_col);
        }
        advance(lex);
    }
    
    bool is_datetime = false;
    if (peek_char(lex) == 'T' || peek_char(lex) == 't') {
        is_datetime = true;
        advance(lex);
        
        for (int i = 0; i < 2; i++) {
            if (!is_digit(peek_char(lex))) {
                return make_error(lex, "invalid datetime literal: expected hour", start_line, start_col);
            }
            advance(lex);
        }
        
        if (peek_char(lex) != ':') {
            return make_error(lex, "invalid datetime literal: expected ':'", start_line, start_col);
        }
        advance(lex);
        
        for (int i = 0; i < 2; i++) {
            if (!is_digit(peek_char(lex))) {
                return make_error(lex, "invalid datetime literal: expected minute", start_line, start_col);
            }
            advance(lex);
        }
        
        if (peek_char(lex) != ':') {
            return make_error(lex, "invalid datetime literal: expected ':'", start_line, start_col);
        }
        advance(lex);
        
        for (int i = 0; i < 2; i++) {
            if (!is_digit(peek_char(lex))) {
                return make_error(lex, "invalid datetime literal: expected second", start_line, start_col);
            }
            advance(lex);
        }
    }
    
    if (peek_char(lex) != '#') {
        return make_error(lex, "invalid date literal: expected closing '#'", start_line, start_col);
    }
    advance(lex);
    
    size_t len = (lex->source + lex->pos) - start;
    return make_token(lex, is_datetime ? TOK_DATETIME : TOK_DATE,
                      start, len, start_line, start_col);
}

static bucc_token_t scan_comment(bucc_lexer_t* lex) {
    uint32_t start_line = lex->line;
    uint32_t start_col = lex->col;
    const char* start = lex->source + lex->pos;
    
    advance(lex);
    
    while (peek_char(lex) != '\n' && peek_char(lex) != '\0') {
        advance(lex);
    }
    
    size_t len = (lex->source + lex->pos) - start;
    return make_token(lex, TOK_COMMENT, start, len, start_line, start_col);
}

static bucc_token_t scan_rem_comment(bucc_lexer_t* lex) {
    uint32_t start_line = lex->line;
    uint32_t start_col = lex->col;
    const char* start = lex->source + lex->pos;
    
    advance(lex);
    advance(lex);
    advance(lex);
    
    while (peek_char(lex) != '\n' && peek_char(lex) != '\0') {
        advance(lex);
    }
    
    size_t len = (lex->source + lex->pos) - start;
    return make_token(lex, TOK_COMMENT, start, len, start_line, start_col);
}

bucc_token_t bucc_lexer_next(bucc_lexer_t* lex) {
    if (lex->has_peek) {
        lex->has_peek = false;
        lex->current = lex->peek;
        return lex->current;
    }
    
    skip_whitespace(lex);
    
    if (lex->pos >= lex->source_len) {
        lex->current = make_token(lex, TOK_EOF, NULL, 0, lex->line, lex->col);
        return lex->current;
    }
    
    char c = peek_char(lex);
    uint32_t start_line = lex->line;
    uint32_t start_col = lex->col;
    
    if (c == '\n') {
        advance(lex);
        lex->current = make_token(lex, TOK_NEWLINE, lex->source + lex->pos - 1, 1,
                                  start_line, start_col);
        return lex->current;
    }
    
    if (c == '\'') {
        lex->current = scan_comment(lex);
        return lex->current;
    }
    
    if (is_ident_start(c)) {
        if ((c == 'R' || c == 'r') &&
            (peek_char_n(lex, 1) == 'E' || peek_char_n(lex, 1) == 'e') &&
            (peek_char_n(lex, 2) == 'M' || peek_char_n(lex, 2) == 'm') &&
            !is_ident_char(peek_char_n(lex, 3))) {
            char next = peek_char_n(lex, 3);
            if (next == ' ' || next == '\t' || next == '\n' || next == '\0') {
                lex->current = scan_rem_comment(lex);
                return lex->current;
            }
        }
        lex->current = scan_identifier(lex);
        return lex->current;
    }
    
    if (is_digit(c)) {
        lex->current = scan_number(lex);
        return lex->current;
    }
    
    if (c == '&' && (peek_char_n(lex, 1) == 'H' || peek_char_n(lex, 1) == 'h')) {
        lex->current = scan_hex(lex);
        return lex->current;
    }
    
    if (c == '"') {
        lex->current = scan_string(lex);
        return lex->current;
    }
    
    if (c == '#') {
        if (is_digit(peek_char_n(lex, 1))) {
            lex->current = scan_date_or_datetime(lex);
            return lex->current;
        }
        advance(lex);
        lex->current = make_token(lex, TOK_HASH, lex->source + lex->pos - 1, 1,
                                  start_line, start_col);
        return lex->current;
    }
    
    bucc_token_kind_t kind = TOK_ERROR;
    size_t tok_len = 1;
    
    switch (c) {
        case '(': kind = TOK_LPAREN; break;
        case ')': kind = TOK_RPAREN; break;
        case '[': kind = TOK_LBRACKET; break;
        case ']': kind = TOK_RBRACKET; break;
        case '{': kind = TOK_LBRACE; break;
        case '}': kind = TOK_RBRACE; break;
        case ',': kind = TOK_COMMA; break;
        case '.': kind = TOK_DOT; break;
        case '+': kind = TOK_PLUS; break;
        case '-':
            if (is_digit(peek_char_n(lex, 1))) {
                lex->current = scan_number(lex);
                return lex->current;
            }
            kind = TOK_MINUS;
            break;
        case '*': kind = TOK_STAR; break;
        case '/': kind = TOK_SLASH; break;
        case '=': kind = TOK_EQ; break;
        case '<':
            if (peek_char_n(lex, 1) == '>') {
                kind = TOK_NE;
                tok_len = 2;
            } else if (peek_char_n(lex, 1) == '=') {
                kind = TOK_LE;
                tok_len = 2;
            } else {
                kind = TOK_LT;
            }
            break;
        case '>':
            if (peek_char_n(lex, 1) == '=') {
                kind = TOK_GE;
                tok_len = 2;
            } else {
                kind = TOK_GT;
            }
            break;
        default:
            advance(lex);
            lex->current = make_error(lex, "unexpected character", start_line, start_col);
            return lex->current;
    }
    
    const char* start = lex->source + lex->pos;
    for (size_t i = 0; i < tok_len; i++) {
        advance(lex);
    }
    
    lex->current = make_token(lex, kind, start, tok_len, start_line, start_col);
    return lex->current;
}

bucc_token_t bucc_lexer_peek(bucc_lexer_t* lex) {
    if (lex->has_peek) {
        return lex->peek;
    }
    
    bucc_token_t saved = lex->current;
    lex->peek = bucc_lexer_next(lex);
    lex->current = saved;
    lex->has_peek = true;
    
    return lex->peek;
}

bool bucc_lexer_at_end(bucc_lexer_t* lex) {
    return lex->current.kind == TOK_EOF;
}

bool bucc_lexer_match(bucc_lexer_t* lex, bucc_token_kind_t kind) {
    if (lex->current.kind == kind) {
        bucc_lexer_next(lex);
        return true;
    }
    return false;
}

bool bucc_lexer_check(bucc_lexer_t* lex, bucc_token_kind_t kind) {
    return lex->current.kind == kind;
}

void bucc_lexer_skip_newlines(bucc_lexer_t* lex) {
    while (lex->current.kind == TOK_NEWLINE || lex->current.kind == TOK_COMMENT) {
        bucc_lexer_next(lex);
    }
}

bool bucc_lexer_expect_newline(bucc_lexer_t* lex) {
    if (lex->current.kind == TOK_NEWLINE || lex->current.kind == TOK_EOF) {
        if (lex->current.kind == TOK_NEWLINE) {
            bucc_lexer_next(lex);
        }
        return true;
    }
    
    if (lex->current.kind == TOK_COMMENT) {
        bucc_lexer_next(lex);
        if (lex->current.kind == TOK_NEWLINE) {
            bucc_lexer_next(lex);
        }
        return true;
    }
    
    bucc_diag_error(lex->diag, lex->current.span, BUCC_ERR_SYNTAX,
                    "expected newline, got %s", bucc_token_kind_name(lex->current.kind));
    return false;
}

const char* bucc_token_kind_name(bucc_token_kind_t kind) {
    switch (kind) {
        case TOK_EOF:           return "EOF";
        case TOK_NEWLINE:       return "NEWLINE";
        case TOK_COMMENT:       return "COMMENT";
        case TOK_IDENT:         return "IDENTIFIER";
        case TOK_INTEGER:       return "INTEGER";
        case TOK_DOUBLE:        return "DOUBLE";
        case TOK_HEX:           return "HEX";
        case TOK_STRING:        return "STRING";
        case TOK_DATE:          return "DATE";
        case TOK_DATETIME:      return "DATETIME";
        case TOK_LPAREN:        return "(";
        case TOK_RPAREN:        return ")";
        case TOK_LBRACKET:      return "[";
        case TOK_RBRACKET:      return "]";
        case TOK_LBRACE:        return "{";
        case TOK_RBRACE:        return "}";
        case TOK_COMMA:         return ",";
        case TOK_COLON:         return ":";
        case TOK_DOT:           return ".";
        case TOK_HASH:          return "#";
        case TOK_PLUS:          return "+";
        case TOK_MINUS:         return "-";
        case TOK_STAR:          return "*";
        case TOK_SLASH:         return "/";
        case TOK_EQ:            return "=";
        case TOK_NE:            return "<>";
        case TOK_LT:            return "<";
        case TOK_LE:            return "<=";
        case TOK_GT:            return ">";
        case TOK_GE:            return ">=";
        case TOK_KW_PROGRAM:    return "PROGRAM";
        case TOK_KW_VERSION:    return "VERSION";
        case TOK_KW_AUTHOR:     return "AUTHOR";
        case TOK_KW_DESCRIPTION: return "DESCRIPTION";
        case TOK_KW_CAPABILITY: return "CAPABILITY";
        case TOK_KW_OPTION:     return "OPTION";
        case TOK_KW_DATASET:    return "DATASET";
        case TOK_KW_DIM:        return "DIM";
        case TOK_KW_AS:         return "AS";
        case TOK_KW_LET:        return "LET";
        case TOK_KW_CONST:      return "CONST";
        case TOK_KW_SUB:        return "SUB";
        case TOK_KW_FUNCTION:   return "FUNCTION";
        case TOK_KW_END:        return "END";
        case TOK_KW_CALL:       return "CALL";
        case TOK_KW_RETURN:     return "RETURN";
        case TOK_KW_IF:         return "IF";
        case TOK_KW_THEN:       return "THEN";
        case TOK_KW_ELSE:       return "ELSE";
        case TOK_KW_ELSEIF:     return "ELSEIF";
        case TOK_KW_SELECT:     return "SELECT";
        case TOK_KW_CASE:       return "CASE";
        case TOK_KW_FOR:        return "FOR";
        case TOK_KW_TO:         return "TO";
        case TOK_KW_STEP:       return "STEP";
        case TOK_KW_NEXT:       return "NEXT";
        case TOK_KW_WHILE:      return "WHILE";
        case TOK_KW_WEND:       return "WEND";
        case TOK_KW_DO:         return "DO";
        case TOK_KW_LOOP:       return "LOOP";
        case TOK_KW_UNTIL:      return "UNTIL";
        case TOK_KW_EXIT:       return "EXIT";
        case TOK_KW_HALT:       return "HALT";
        case TOK_KW_CHAIN:      return "CHAIN";
        case TOK_KW_TRY:        return "TRY";
        case TOK_KW_CATCH:      return "CATCH";
        case TOK_KW_THROW:      return "THROW";
        case TOK_KW_ON:         return "ON";
        case TOK_KW_AND:        return "AND";
        case TOK_KW_OR:         return "OR";
        case TOK_KW_NOT:        return "NOT";
        case TOK_KW_MOD:        return "MOD";
        case TOK_KW_TRUE:       return "TRUE";
        case TOK_KW_FALSE:      return "FALSE";
        case TOK_KW_NULL:       return "NULL";
        case TOK_KW_INTEGER:    return "INTEGER";
        case TOK_KW_DOUBLE:     return "DOUBLE";
        case TOK_KW_BOOLEAN:    return "BOOLEAN";
        case TOK_KW_STRING:     return "STRING";
        case TOK_KW_DATE:       return "DATE";
        case TOK_KW_DATETIME:   return "DATETIME";
        case TOK_KW_ARRAY:      return "ARRAY";
        case TOK_KW_MAP:        return "MAP";
        case TOK_KW_OF:         return "OF";
        case TOK_KW_REM:        return "REM";
        case TOK_ERROR:         return "ERROR";
        default:                return "UNKNOWN";
    }
}

char* bucc_token_text(bucc_token_t* tok) {
    if (!tok || !tok->start || tok->len == 0) return strdup("");
    
    char* text = malloc(tok->len + 1);
    if (!text) return NULL;
    
    memcpy(text, tok->start, tok->len);
    text[tok->len] = '\0';
    
    return text;
}

bool bucc_token_is_type_keyword(bucc_token_kind_t kind) {
    switch (kind) {
        case TOK_KW_INTEGER:
        case TOK_KW_DOUBLE:
        case TOK_KW_BOOLEAN:
        case TOK_KW_STRING:
        case TOK_KW_DATE:
        case TOK_KW_DATETIME:
        case TOK_KW_ARRAY:
        case TOK_KW_MAP:
            return true;
        default:
            return false;
    }
}

bool bucc_token_is_literal(bucc_token_kind_t kind) {
    switch (kind) {
        case TOK_INTEGER:
        case TOK_DOUBLE:
        case TOK_HEX:
        case TOK_STRING:
        case TOK_DATE:
        case TOK_DATETIME:
        case TOK_KW_TRUE:
        case TOK_KW_FALSE:
        case TOK_KW_NULL:
            return true;
        default:
            return false;
    }
}

bool bucc_token_is_comparison(bucc_token_kind_t kind) {
    switch (kind) {
        case TOK_EQ:
        case TOK_NE:
        case TOK_LT:
        case TOK_LE:
        case TOK_GT:
        case TOK_GE:
            return true;
        default:
            return false;
    }
}

bool bucc_token_is_additive(bucc_token_kind_t kind) {
    return kind == TOK_PLUS || kind == TOK_MINUS;
}

bool bucc_token_is_multiplicative(bucc_token_kind_t kind) {
    return kind == TOK_STAR || kind == TOK_SLASH || kind == TOK_KW_MOD;
}
