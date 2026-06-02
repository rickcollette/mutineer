# Buccaneer Grammar Specification

Version: 0.3-draft  
Status: Proposed

This document defines Buccaneer lexical and syntactic grammar. Semantic rules such as name resolution, type checking, capability enforcement, and control-flow legality are defined separately in the semantic analysis spec.

## 1. Notation

The grammar uses EBNF-like notation:

- `A ::= B C` means sequence
- `A ::= B | C` means choice
- `[A]` means optional
- `{A}` means zero or more
- terminals appear in quotes or token names
- comments and newlines are lexically recognized

Keywords are case-insensitive in source.

## 2. Lexical grammar

### 2.1 Character set

- source files are UTF-8
- identifiers and keywords are ASCII-only in v1
- string literals may contain UTF-8 text

### 2.2 Tokens

```ebnf
identifier      ::= letter { letter | digit | "_" }
integer_lit     ::= ["-"] digit { digit }
double_lit      ::= ["-"] digit { digit } "." digit { digit }
hex_lit         ::= "&H" hex_digit { hex_digit }
string_lit      ::= '"' { string_char } '"'
date_lit        ::= "#" yyyy "-" mm "-" dd "#"
datetime_lit    ::= "#" yyyy "-" mm "-" dd "T" hh ":" mi ":" ss "#"
boolean_lit     ::= "TRUE" | "FALSE"
null_lit        ::= "NULL"
comment         ::= "'" { any_char_except_newline }
                 |  "REM" ws { any_char_except_newline }
```

## 3. Compilation unit grammar

```ebnf
module              ::= metadata_block body eof
metadata_block      ::= { metadata_stmt }
body                ::= { top_level_decl }

metadata_stmt       ::= program_stmt
                      | version_stmt
                      | author_stmt
                      | description_stmt
                      | capability_stmt
                      | option_stmt
                      | dataset_decl_stmt

program_stmt        ::= "PROGRAM" string_lit newline
version_stmt        ::= "VERSION" string_lit newline
author_stmt         ::= "AUTHOR" string_lit newline
description_stmt    ::= "DESCRIPTION" string_lit newline
capability_stmt     ::= "CAPABILITY" string_lit newline
option_stmt         ::= "OPTION" option_name [ option_arg ] newline
dataset_decl_stmt   ::= "DATASET" identifier dataset_schema_spec newline
```

A module must contain exactly one `PROGRAM` and exactly one `VERSION`.

## 4. Top-level declarations

```ebnf
top_level_decl      ::= global_var_decl
                      | procedure_decl
                      | function_decl

global_var_decl     ::= "DIM" identifier "AS" type_ref [ "=" expr ] newline
```

## 5. Type grammar

```ebnf
type_ref            ::= scalar_type
                      | array_type
                      | map_type

scalar_type         ::= "INTEGER"
                      | "DOUBLE"
                      | "BOOLEAN"
                      | "STRING"
                      | "DATE"
                      | "DATETIME"

array_type          ::= "ARRAY" "OF" type_ref
map_type            ::= "MAP" "OF" "STRING" "TO" type_ref
```

## 6. Procedures and functions

```ebnf
procedure_decl      ::= "SUB" identifier "(" [ param_list ] ")" newline
                        statement_block
                        "END" "SUB" newline

function_decl       ::= "FUNCTION" identifier "(" [ param_list ] ")" "AS" type_ref newline
                        statement_block
                        "END" "FUNCTION" newline

param_list          ::= param { "," param }
param               ::= identifier "AS" type_ref
```

## 7. Statements

```ebnf
statement_block     ::= { statement }

statement           ::= simple_stmt newline
                      | if_stmt
                      | select_stmt
                      | while_stmt
                      | do_loop_stmt
                      | for_stmt
                      | try_catch_stmt

simple_stmt         ::= var_decl_stmt
                      | assignment_stmt
                      | expr_stmt
                      | return_stmt
                      | exit_stmt
                      | halt_stmt
                      | throw_stmt
                      | chain_stmt
                      | on_call_stmt
```

### 7.1 Assignments and calls

```ebnf
var_decl_stmt       ::= "DIM" identifier "AS" type_ref [ "=" expr ]
assignment_stmt     ::= [ "LET" ] lvalue "=" expr
expr_stmt           ::= expr
return_stmt         ::= "RETURN" [ expr ]
exit_stmt           ::= "EXIT" ( "FOR" | "DO" | "WHILE" | "SELECT" )
halt_stmt           ::= "HALT"
throw_stmt          ::= "THROW" expr
chain_stmt          ::= "CHAIN" string_lit [ "," expr ]
on_call_stmt        ::= "ON" expr "CALL" callee_list
callee_list         ::= callee { "," callee }
```

## 8. Conditionals and selection

```ebnf
if_stmt             ::= "IF" expr "THEN" newline
                        statement_block
                        { "ELSEIF" expr "THEN" newline statement_block }
                        [ "ELSE" newline statement_block ]
                        "END" "IF" newline

select_stmt         ::= "SELECT" "CASE" expr newline
                        { case_clause }
                        [ case_else_clause ]
                        "END" "SELECT" newline
```

## 9. Loops

```ebnf
while_stmt          ::= "WHILE" expr newline
                        statement_block
                        "WEND" newline

do_loop_stmt        ::= "DO" newline
                        statement_block
                        "LOOP" [ "UNTIL" expr | "WHILE" expr ] newline

for_stmt            ::= "FOR" identifier "=" expr "TO" expr [ "STEP" expr ] newline
                        statement_block
                        "NEXT" [ identifier ] newline
```

## 10. Try-catch

```ebnf
try_catch_stmt      ::= "TRY" newline
                        statement_block
                        "CATCH" identifier newline
                        statement_block
                        "END" "TRY" newline
```

## 11. Expressions

```ebnf
expr                ::= logical_or_expr
logical_or_expr     ::= logical_and_expr { "OR" logical_and_expr }
logical_and_expr    ::= equality_expr { "AND" equality_expr }
equality_expr       ::= relational_expr { ( "=" | "<>" ) relational_expr }
relational_expr     ::= additive_expr { ( "<" | "<=" | ">" | ">=" ) additive_expr }
additive_expr       ::= multiplicative_expr { ( "+" | "-" ) multiplicative_expr }
multiplicative_expr ::= unary_expr { ( "*" | "/" | "MOD" ) unary_expr }
unary_expr          ::= [ "-" | "NOT" ] postfix_expr
postfix_expr        ::= primary_expr { postfix_op }
postfix_op          ::= "(" [ arg_list ] ")"
                      | "[" expr "]"
                      | "." identifier
```

```ebnf
primary_expr        ::= identifier
                      | integer_lit
                      | double_lit
                      | hex_lit
                      | string_lit
                      | date_lit
                      | datetime_lit
                      | boolean_lit
                      | null_lit
                      | "(" expr ")"
                      | array_literal
                      | map_literal

arg_list            ::= expr { "," expr }
array_literal       ::= "[" [ expr { "," expr } ] "]"
map_literal         ::= "{" [ map_pair { "," map_pair } ] "}"
map_pair            ::= string_lit ":" expr
```

## 12. Grammar notes

- no line numbers
- no unrestricted `GOTO`
- `CHAIN` targets are string literals in v1
- `ON expr CALL` is structured dispatch, not a computed jump
- `TRY/CATCH` is the only language-level error trap in v1
