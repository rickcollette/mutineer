/*
 * bucc_diag.h - Buccaneer diagnostic and error reporting
 *
 * Part of the Buccaneer language implementation for Mutineer BBS.
 * Provides structured error/warning reporting with source locations.
 */

#ifndef BUCC_DIAG_H
#define BUCC_DIAG_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum bucc_diag_level {
    BUCC_DIAG_NOTE = 0,
    BUCC_DIAG_WARNING,
    BUCC_DIAG_ERROR,
    BUCC_DIAG_FATAL
} bucc_diag_level_t;

typedef struct bucc_source_span {
    uint32_t file_id;
    uint32_t start_line;
    uint32_t start_col;
    uint32_t end_line;
    uint32_t end_col;
} bucc_source_span_t;

#define BUCC_SPAN_NONE ((bucc_source_span_t){0, 0, 0, 0, 0})

typedef struct bucc_diag_entry {
    bucc_diag_level_t   level;
    bucc_source_span_t  span;
    char*               code;
    char*               message;
    struct bucc_diag_entry* next;
} bucc_diag_entry_t;

typedef struct bucc_source_file {
    uint32_t    id;
    char*       path;
    char*       content;
    size_t      content_len;
    uint32_t*   line_offsets;
    uint32_t    line_count;
} bucc_source_file_t;

typedef struct bucc_diag {
    bucc_diag_entry_t*  head;
    bucc_diag_entry_t*  tail;
    uint32_t            error_count;
    uint32_t            warning_count;
    uint32_t            note_count;
    bucc_source_file_t** files;
    uint32_t            file_count;
    uint32_t            file_cap;
    bool                colors_enabled;
} bucc_diag_t;

bucc_diag_t* bucc_diag_new(void);
void bucc_diag_free(bucc_diag_t* d);

uint32_t bucc_diag_add_file(bucc_diag_t* d, const char* path, const char* content, size_t len);
bucc_source_file_t* bucc_diag_get_file(bucc_diag_t* d, uint32_t file_id);

void bucc_diag_emit(bucc_diag_t* d, bucc_diag_level_t level,
                    bucc_source_span_t span, const char* code,
                    const char* fmt, ...);
void bucc_diag_emit_v(bucc_diag_t* d, bucc_diag_level_t level,
                      bucc_source_span_t span, const char* code,
                      const char* fmt, va_list args);

void bucc_diag_note(bucc_diag_t* d, bucc_source_span_t span,
                    const char* code, const char* fmt, ...);
void bucc_diag_warning(bucc_diag_t* d, bucc_source_span_t span,
                       const char* code, const char* fmt, ...);
void bucc_diag_error(bucc_diag_t* d, bucc_source_span_t span,
                     const char* code, const char* fmt, ...);
void bucc_diag_fatal(bucc_diag_t* d, bucc_source_span_t span,
                     const char* code, const char* fmt, ...);

bool bucc_diag_has_errors(bucc_diag_t* d);
uint32_t bucc_diag_error_count(bucc_diag_t* d);

void bucc_diag_print(bucc_diag_t* d, FILE* out);
void bucc_diag_print_entry(bucc_diag_t* d, bucc_diag_entry_t* e, FILE* out);

char* bucc_diag_format_span(bucc_diag_t* d, bucc_source_span_t span);
void bucc_diag_get_line(bucc_diag_t* d, uint32_t file_id, uint32_t line,
                        const char** out_start, size_t* out_len);

void bucc_diag_clear(bucc_diag_t* d);
void bucc_diag_set_colors(bucc_diag_t* d, bool enabled);

#define BUCC_ERR_SYNTAX         "E0001"
#define BUCC_ERR_UNEXPECTED_TOK "E0002"
#define BUCC_ERR_UNTERMINATED   "E0003"
#define BUCC_ERR_INVALID_CHAR   "E0004"
#define BUCC_ERR_INVALID_NUMBER "E0005"
#define BUCC_ERR_INVALID_DATE   "E0006"
#define BUCC_ERR_DUPLICATE_DECL "E0010"
#define BUCC_ERR_UNDEFINED_SYM  "E0011"
#define BUCC_ERR_TYPE_MISMATCH  "E0020"
#define BUCC_ERR_INVALID_LVALUE "E0021"
#define BUCC_ERR_ARITY_MISMATCH "E0022"
#define BUCC_ERR_NO_RETURN      "E0030"
#define BUCC_ERR_UNREACHABLE    "E0031"
#define BUCC_ERR_INVALID_EXIT   "E0032"
#define BUCC_ERR_MISSING_MAIN   "E0040"
#define BUCC_ERR_INVALID_HANDLER "E0041"
#define BUCC_ERR_CAPABILITY     "E0050"
#define BUCC_ERR_DATASET        "E0051"
#define BUCC_ERR_CHAIN_TARGET   "E0052"
#define BUCC_ERR_PATH_TRAVERSAL "E0053"

#define BUCC_WARN_UNUSED_VAR    "W0001"
#define BUCC_WARN_UNUSED_PROC   "W0002"
#define BUCC_WARN_SHADOW        "W0003"
#define BUCC_WARN_LOOP_LOOKUP   "W0010"
#define BUCC_WARN_IMPLICIT_CONV "W0020"

#ifdef __cplusplus
}
#endif

#endif /* BUCC_DIAG_H */
