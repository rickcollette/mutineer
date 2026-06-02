/*
 * diag.c - Buccaneer diagnostic and error reporting implementation
 *
 * Part of the Buccaneer language implementation for Mutineer BBS.
 */

#define _POSIX_C_SOURCE 200809L
#include "include/bucc_diag.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

bucc_diag_t* bucc_diag_new(void) {
    bucc_diag_t* d = calloc(1, sizeof(bucc_diag_t));
    if (!d) return NULL;
    
    d->file_cap = 4;
    d->files = calloc(d->file_cap, sizeof(bucc_source_file_t*));
    if (!d->files) {
        free(d);
        return NULL;
    }
    
    d->colors_enabled = false;
    return d;
}

static void free_entry(bucc_diag_entry_t* e) {
    if (!e) return;
    free(e->code);
    free(e->message);
    free(e);
}

static void free_file(bucc_source_file_t* f) {
    if (!f) return;
    free(f->path);
    free(f->content);
    free(f->line_offsets);
    free(f);
}

void bucc_diag_free(bucc_diag_t* d) {
    if (!d) return;
    
    bucc_diag_entry_t* e = d->head;
    while (e) {
        bucc_diag_entry_t* next = e->next;
        free_entry(e);
        e = next;
    }
    
    for (uint32_t i = 0; i < d->file_count; i++) {
        free_file(d->files[i]);
    }
    free(d->files);
    free(d);
}

static void compute_line_offsets(bucc_source_file_t* f) {
    if (!f || !f->content) return;
    
    uint32_t cap = 256;
    f->line_offsets = malloc(cap * sizeof(uint32_t));
    if (!f->line_offsets) return;
    
    f->line_offsets[0] = 0;
    f->line_count = 1;
    
    for (size_t i = 0; i < f->content_len; i++) {
        if (f->content[i] == '\n') {
            if (f->line_count >= cap) {
                cap *= 2;
                uint32_t* new_offsets = realloc(f->line_offsets, cap * sizeof(uint32_t));
                if (!new_offsets) return;
                f->line_offsets = new_offsets;
            }
            f->line_offsets[f->line_count++] = (uint32_t)(i + 1);
        }
    }
}

uint32_t bucc_diag_add_file(bucc_diag_t* d, const char* path, const char* content, size_t len) {
    if (!d) return 0;
    
    if (d->file_count >= d->file_cap) {
        uint32_t new_cap = d->file_cap * 2;
        bucc_source_file_t** new_files = realloc(d->files, new_cap * sizeof(bucc_source_file_t*));
        if (!new_files) return 0;
        d->files = new_files;
        d->file_cap = new_cap;
    }
    
    bucc_source_file_t* f = calloc(1, sizeof(bucc_source_file_t));
    if (!f) return 0;
    
    f->id = d->file_count + 1;
    f->path = path ? strdup(path) : NULL;
    
    if (content && len > 0) {
        f->content = malloc(len + 1);
        if (f->content) {
            memcpy(f->content, content, len);
            f->content[len] = '\0';
            f->content_len = len;
        }
    }
    
    compute_line_offsets(f);
    
    d->files[d->file_count++] = f;
    return f->id;
}

bucc_source_file_t* bucc_diag_get_file(bucc_diag_t* d, uint32_t file_id) {
    if (!d || file_id == 0 || file_id > d->file_count) return NULL;
    return d->files[file_id - 1];
}

void bucc_diag_emit_v(bucc_diag_t* d, bucc_diag_level_t level,
                      bucc_source_span_t span, const char* code,
                      const char* fmt, va_list args) {
    if (!d) return;
    
    bucc_diag_entry_t* e = calloc(1, sizeof(bucc_diag_entry_t));
    if (!e) return;
    
    e->level = level;
    e->span = span;
    e->code = code ? strdup(code) : NULL;
    
    if (fmt) {
        va_list args_copy;
        va_copy(args_copy, args);
        int len = vsnprintf(NULL, 0, fmt, args_copy);
        va_end(args_copy);
        
        if (len > 0) {
            e->message = malloc(len + 1);
            if (e->message) {
                vsnprintf(e->message, len + 1, fmt, args);
            }
        }
    }
    
    if (d->tail) {
        d->tail->next = e;
        d->tail = e;
    } else {
        d->head = d->tail = e;
    }
    
    switch (level) {
        case BUCC_DIAG_NOTE:    d->note_count++; break;
        case BUCC_DIAG_WARNING: d->warning_count++; break;
        case BUCC_DIAG_ERROR:
        case BUCC_DIAG_FATAL:   d->error_count++; break;
    }
}

void bucc_diag_emit(bucc_diag_t* d, bucc_diag_level_t level,
                    bucc_source_span_t span, const char* code,
                    const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    bucc_diag_emit_v(d, level, span, code, fmt, args);
    va_end(args);
}

void bucc_diag_note(bucc_diag_t* d, bucc_source_span_t span,
                    const char* code, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    bucc_diag_emit_v(d, BUCC_DIAG_NOTE, span, code, fmt, args);
    va_end(args);
}

void bucc_diag_warning(bucc_diag_t* d, bucc_source_span_t span,
                       const char* code, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    bucc_diag_emit_v(d, BUCC_DIAG_WARNING, span, code, fmt, args);
    va_end(args);
}

void bucc_diag_error(bucc_diag_t* d, bucc_source_span_t span,
                     const char* code, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    bucc_diag_emit_v(d, BUCC_DIAG_ERROR, span, code, fmt, args);
    va_end(args);
}

void bucc_diag_fatal(bucc_diag_t* d, bucc_source_span_t span,
                     const char* code, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    bucc_diag_emit_v(d, BUCC_DIAG_FATAL, span, code, fmt, args);
    va_end(args);
}

bool bucc_diag_has_errors(bucc_diag_t* d) {
    return d && d->error_count > 0;
}

uint32_t bucc_diag_error_count(bucc_diag_t* d) {
    return d ? d->error_count : 0;
}

static const char* level_name(bucc_diag_level_t level) {
    switch (level) {
        case BUCC_DIAG_NOTE:    return "note";
        case BUCC_DIAG_WARNING: return "warning";
        case BUCC_DIAG_ERROR:   return "error";
        case BUCC_DIAG_FATAL:   return "fatal";
        default:                return "unknown";
    }
}

static const char* level_color(bucc_diag_level_t level) {
    switch (level) {
        case BUCC_DIAG_NOTE:    return "\033[36m";
        case BUCC_DIAG_WARNING: return "\033[33m";
        case BUCC_DIAG_ERROR:   return "\033[31m";
        case BUCC_DIAG_FATAL:   return "\033[1;31m";
        default:                return "";
    }
}

void bucc_diag_print_entry(bucc_diag_t* d, bucc_diag_entry_t* e, FILE* out) {
    if (!d || !e || !out) return;
    
    const char* reset = d->colors_enabled ? "\033[0m" : "";
    const char* bold = d->colors_enabled ? "\033[1m" : "";
    const char* color = d->colors_enabled ? level_color(e->level) : "";
    
    bucc_source_file_t* f = bucc_diag_get_file(d, e->span.file_id);
    const char* filename = f ? f->path : "<unknown>";
    
    if (e->span.start_line > 0) {
        fprintf(out, "%s%s:%u:%u:%s %s%s%s",
                bold, filename, e->span.start_line, e->span.start_col, reset,
                color, level_name(e->level), reset);
    } else {
        fprintf(out, "%s%s:%s %s%s%s",
                bold, filename, reset,
                color, level_name(e->level), reset);
    }
    
    if (e->code) {
        fprintf(out, "[%s]", e->code);
    }
    
    fprintf(out, ": %s%s%s\n", bold, e->message ? e->message : "", reset);
    
    if (f && e->span.start_line > 0 && e->span.start_line <= f->line_count) {
        const char* line_start;
        size_t line_len;
        bucc_diag_get_line(d, e->span.file_id, e->span.start_line, &line_start, &line_len);
        
        if (line_start) {
            fprintf(out, " %5u | ", e->span.start_line);
            fwrite(line_start, 1, line_len, out);
            fprintf(out, "\n");
            
            fprintf(out, "       | ");
            for (uint32_t i = 1; i < e->span.start_col; i++) {
                fputc(' ', out);
            }
            fprintf(out, "%s^%s\n", color, reset);
        }
    }
}

void bucc_diag_print(bucc_diag_t* d, FILE* out) {
    if (!d || !out) return;
    
    for (bucc_diag_entry_t* e = d->head; e; e = e->next) {
        bucc_diag_print_entry(d, e, out);
    }
    
    if (d->error_count > 0 || d->warning_count > 0) {
        fprintf(out, "\n");
        if (d->error_count > 0) {
            fprintf(out, "%u error%s", d->error_count, d->error_count == 1 ? "" : "s");
        }
        if (d->warning_count > 0) {
            if (d->error_count > 0) fprintf(out, ", ");
            fprintf(out, "%u warning%s", d->warning_count, d->warning_count == 1 ? "" : "s");
        }
        fprintf(out, " generated.\n");
    }
}

char* bucc_diag_format_span(bucc_diag_t* d, bucc_source_span_t span) {
    bucc_source_file_t* f = bucc_diag_get_file(d, span.file_id);
    const char* filename = f ? f->path : "<unknown>";
    
    char* buf = malloc(256);
    if (!buf) return NULL;
    
    if (span.start_line > 0) {
        snprintf(buf, 256, "%s:%u:%u", filename, span.start_line, span.start_col);
    } else {
        snprintf(buf, 256, "%s", filename);
    }
    
    return buf;
}

void bucc_diag_get_line(bucc_diag_t* d, uint32_t file_id, uint32_t line,
                        const char** out_start, size_t* out_len) {
    if (out_start) *out_start = NULL;
    if (out_len) *out_len = 0;
    
    bucc_source_file_t* f = bucc_diag_get_file(d, file_id);
    if (!f || !f->content || !f->line_offsets) return;
    if (line == 0 || line > f->line_count) return;
    
    uint32_t start = f->line_offsets[line - 1];
    uint32_t end;
    
    if (line < f->line_count) {
        end = f->line_offsets[line];
        if (end > 0 && f->content[end - 1] == '\n') end--;
    } else {
        end = (uint32_t)f->content_len;
    }
    
    if (out_start) *out_start = f->content + start;
    if (out_len) *out_len = end - start;
}

void bucc_diag_clear(bucc_diag_t* d) {
    if (!d) return;
    
    bucc_diag_entry_t* e = d->head;
    while (e) {
        bucc_diag_entry_t* next = e->next;
        free_entry(e);
        e = next;
    }
    
    d->head = d->tail = NULL;
    d->error_count = 0;
    d->warning_count = 0;
    d->note_count = 0;
}

void bucc_diag_set_colors(bucc_diag_t* d, bool enabled) {
    if (d) d->colors_enabled = enabled;
}
