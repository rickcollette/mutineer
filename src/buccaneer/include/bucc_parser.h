/*
 * bucc_parser.h - Buccaneer recursive descent parser
 *
 * Part of the Buccaneer language implementation for Mutineer BBS.
 * Parses tokenized source into AST per BUCCANEER_GRAMMAR_SPEC.md.
 */

#ifndef BUCC_PARSER_H
#define BUCC_PARSER_H

#include "bucc_lexer.h"
#include "bucc_ast.h"
#include "bucc_diag.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bucc_parser {
    bucc_lexer_t* lexer;
    bucc_diag_t*  diag;
    bool          had_error;
    bool          panic_mode;
} bucc_parser_t;

void bucc_parser_init(bucc_parser_t* parser, bucc_lexer_t* lexer, bucc_diag_t* diag);

bucc_node_t* bucc_parse_module(bucc_parser_t* parser);

bucc_node_t* bucc_parse_metadata(bucc_parser_t* parser);
bucc_node_t* bucc_parse_global_decl(bucc_parser_t* parser);
bucc_node_t* bucc_parse_procedure(bucc_parser_t* parser);
bucc_node_t* bucc_parse_function(bucc_parser_t* parser);

bucc_node_t* bucc_parse_type_ref(bucc_parser_t* parser);
bucc_node_t* bucc_parse_param_list(bucc_parser_t* parser, bucc_node_list_t* params);

bucc_node_t* bucc_parse_block(bucc_parser_t* parser);
bucc_node_t* bucc_parse_statement(bucc_parser_t* parser);

bucc_node_t* bucc_parse_if_stmt(bucc_parser_t* parser);
bucc_node_t* bucc_parse_select_stmt(bucc_parser_t* parser);
bucc_node_t* bucc_parse_while_stmt(bucc_parser_t* parser);
bucc_node_t* bucc_parse_do_loop_stmt(bucc_parser_t* parser);
bucc_node_t* bucc_parse_for_stmt(bucc_parser_t* parser);
bucc_node_t* bucc_parse_try_catch_stmt(bucc_parser_t* parser);

bucc_node_t* bucc_parse_expression(bucc_parser_t* parser);
bucc_node_t* bucc_parse_or_expr(bucc_parser_t* parser);
bucc_node_t* bucc_parse_and_expr(bucc_parser_t* parser);
bucc_node_t* bucc_parse_equality_expr(bucc_parser_t* parser);
bucc_node_t* bucc_parse_relational_expr(bucc_parser_t* parser);
bucc_node_t* bucc_parse_additive_expr(bucc_parser_t* parser);
bucc_node_t* bucc_parse_multiplicative_expr(bucc_parser_t* parser);
bucc_node_t* bucc_parse_unary_expr(bucc_parser_t* parser);
bucc_node_t* bucc_parse_postfix_expr(bucc_parser_t* parser);
bucc_node_t* bucc_parse_primary_expr(bucc_parser_t* parser);

bool bucc_parser_had_error(bucc_parser_t* parser);

#ifdef __cplusplus
}
#endif

#endif /* BUCC_PARSER_H */
