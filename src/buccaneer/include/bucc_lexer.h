/*
 * bucc_lexer.h - Buccaneer lexical analyzer
 *
 * Part of the Buccaneer language implementation for Mutineer BBS.
 * Tokenizes .bucc source files per BUCCANEER_GRAMMAR_SPEC.md.
 */

#ifndef BUCC_LEXER_H
#define BUCC_LEXER_H

#include "bucc_diag.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum bucc_token_kind {
    TOK_EOF = 0,
    TOK_NEWLINE,
    TOK_COMMENT,
    
    TOK_IDENT,
    TOK_INTEGER,
    TOK_DOUBLE,
    TOK_HEX,
    TOK_STRING,
    TOK_DATE,
    TOK_DATETIME,
    
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_LBRACKET,
    TOK_RBRACKET,
    TOK_LBRACE,
    TOK_RBRACE,
    TOK_COMMA,
    TOK_COLON,
    TOK_DOT,
    TOK_HASH,
    
    TOK_PLUS,
    TOK_MINUS,
    TOK_STAR,
    TOK_SLASH,
    TOK_EQ,
    TOK_NE,
    TOK_LT,
    TOK_LE,
    TOK_GT,
    TOK_GE,
    
    TOK_KW_PROGRAM,
    TOK_KW_VERSION,
    TOK_KW_AUTHOR,
    TOK_KW_DESCRIPTION,
    TOK_KW_CAPABILITY,
    TOK_KW_OPTION,
    TOK_KW_DATASET,
    
    TOK_KW_DIM,
    TOK_KW_AS,
    TOK_KW_LET,
    TOK_KW_CONST,
    
    TOK_KW_SUB,
    TOK_KW_FUNCTION,
    TOK_KW_END,
    TOK_KW_CALL,
    TOK_KW_RETURN,
    
    TOK_KW_IF,
    TOK_KW_THEN,
    TOK_KW_ELSE,
    TOK_KW_ELSEIF,
    
    TOK_KW_SELECT,
    TOK_KW_CASE,
    
    TOK_KW_FOR,
    TOK_KW_TO,
    TOK_KW_STEP,
    TOK_KW_NEXT,
    
    TOK_KW_WHILE,
    TOK_KW_WEND,
    
    TOK_KW_DO,
    TOK_KW_LOOP,
    TOK_KW_UNTIL,
    
    TOK_KW_EXIT,
    TOK_KW_HALT,
    TOK_KW_CHAIN,
    
    TOK_KW_TRY,
    TOK_KW_CATCH,
    TOK_KW_THROW,
    
    TOK_KW_ON,
    
    TOK_KW_AND,
    TOK_KW_OR,
    TOK_KW_NOT,
    TOK_KW_MOD,
    
    TOK_KW_TRUE,
    TOK_KW_FALSE,
    TOK_KW_NULL,
    
    TOK_KW_INTEGER,
    TOK_KW_DOUBLE,
    TOK_KW_BOOLEAN,
    TOK_KW_STRING,
    TOK_KW_DATE,
    TOK_KW_DATETIME,
    TOK_KW_ARRAY,
    TOK_KW_MAP,
    TOK_KW_OF,
    
    TOK_KW_REM,
    
    TOK_ERROR
} bucc_token_kind_t;

typedef struct bucc_token {
    bucc_token_kind_t  kind;
    bucc_source_span_t span;
    const char*        start;
    size_t             len;
    
    union {
        int64_t i64;
        double  f64;
    } value;
} bucc_token_t;

typedef struct bucc_lexer {
    const char*     source;
    size_t          source_len;
    size_t          pos;
    uint32_t        line;
    uint32_t        col;
    uint32_t        file_id;
    bucc_diag_t*    diag;
    
    bucc_token_t    current;
    bucc_token_t    peek;
    bool            has_peek;
} bucc_lexer_t;

void bucc_lexer_init(bucc_lexer_t* lex, const char* source, size_t len,
                     uint32_t file_id, bucc_diag_t* diag);

bucc_token_t bucc_lexer_next(bucc_lexer_t* lex);
bucc_token_t bucc_lexer_peek(bucc_lexer_t* lex);

bool bucc_lexer_at_end(bucc_lexer_t* lex);

bool bucc_lexer_match(bucc_lexer_t* lex, bucc_token_kind_t kind);
bool bucc_lexer_check(bucc_lexer_t* lex, bucc_token_kind_t kind);

void bucc_lexer_skip_newlines(bucc_lexer_t* lex);
bool bucc_lexer_expect_newline(bucc_lexer_t* lex);

const char* bucc_token_kind_name(bucc_token_kind_t kind);
char* bucc_token_text(bucc_token_t* tok);

bool bucc_token_is_type_keyword(bucc_token_kind_t kind);
bool bucc_token_is_literal(bucc_token_kind_t kind);
bool bucc_token_is_comparison(bucc_token_kind_t kind);
bool bucc_token_is_additive(bucc_token_kind_t kind);
bool bucc_token_is_multiplicative(bucc_token_kind_t kind);

#ifdef __cplusplus
}
#endif

#endif /* BUCC_LEXER_H */
