/*
 * value.c - Buccaneer runtime value implementation
 *
 * Part of the Buccaneer language implementation for Mutineer BBS.
 */

#define _POSIX_C_SOURCE 200809L
#include "include/bucc_value.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

const char* bucc_value_kind_name(bucc_value_kind_t kind) {
    switch (kind) {
        case BUCC_VAL_NULL:     return "NULL";
        case BUCC_VAL_BOOL:     return "BOOLEAN";
        case BUCC_VAL_I64:      return "INTEGER";
        case BUCC_VAL_F64:      return "DOUBLE";
        case BUCC_VAL_STRING:   return "STRING";
        case BUCC_VAL_DATE:     return "DATE";
        case BUCC_VAL_DATETIME: return "DATETIME";
        case BUCC_VAL_ARRAY:    return "ARRAY";
        case BUCC_VAL_MAP:      return "MAP";
        case BUCC_VAL_ERROR:    return "ERROR";
        default:                return "UNKNOWN";
    }
}

static uint32_t hash_bytes(const char* data, size_t len) {
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < len; i++) {
        hash ^= (uint8_t)data[i];
        hash *= 16777619u;
    }
    return hash;
}

bucc_string_t* bucc_string_new(const char* data, size_t len) {
    bucc_string_t* s = malloc(sizeof(bucc_string_t));
    if (!s) return NULL;
    
    s->data = malloc(len + 1);
    if (!s->data) {
        free(s);
        return NULL;
    }
    
    if (data && len > 0) {
        memcpy(s->data, data, len);
    }
    s->data[len] = '\0';
    s->len = len;
    s->refcount = 1;
    s->hash = hash_bytes(s->data, len);
    
    return s;
}

bucc_string_t* bucc_string_from_cstr(const char* cstr) {
    if (!cstr) return bucc_string_new("", 0);
    return bucc_string_new(cstr, strlen(cstr));
}

bucc_string_t* bucc_string_concat(bucc_string_t* a, bucc_string_t* b) {
    if (!a && !b) return bucc_string_new("", 0);
    if (!a) { bucc_string_retain(b); return b; }
    if (!b) { bucc_string_retain(a); return a; }
    
    size_t new_len = a->len + b->len;
    bucc_string_t* s = malloc(sizeof(bucc_string_t));
    if (!s) return NULL;
    
    s->data = malloc(new_len + 1);
    if (!s->data) {
        free(s);
        return NULL;
    }
    
    memcpy(s->data, a->data, a->len);
    memcpy(s->data + a->len, b->data, b->len);
    s->data[new_len] = '\0';
    s->len = new_len;
    s->refcount = 1;
    s->hash = hash_bytes(s->data, new_len);
    
    return s;
}

void bucc_string_retain(bucc_string_t* s) {
    if (s) s->refcount++;
}

void bucc_string_release(bucc_string_t* s) {
    if (!s) return;
    if (--s->refcount == 0) {
        free(s->data);
        free(s);
    }
}

int bucc_string_cmp(bucc_string_t* a, bucc_string_t* b) {
    if (!a && !b) return 0;
    if (!a) return -1;
    if (!b) return 1;
    
    size_t min_len = a->len < b->len ? a->len : b->len;
    int cmp = memcmp(a->data, b->data, min_len);
    if (cmp != 0) return cmp;
    
    if (a->len < b->len) return -1;
    if (a->len > b->len) return 1;
    return 0;
}

uint32_t bucc_string_hash(bucc_string_t* s) {
    return s ? s->hash : 0;
}

bucc_array_t* bucc_array_new(size_t initial_cap) {
    bucc_array_t* a = malloc(sizeof(bucc_array_t));
    if (!a) return NULL;
    
    if (initial_cap == 0) initial_cap = 8;
    
    a->items = calloc(initial_cap, sizeof(bucc_value_t));
    if (!a->items) {
        free(a);
        return NULL;
    }
    
    a->len = 0;
    a->cap = initial_cap;
    a->refcount = 1;
    
    return a;
}

void bucc_array_retain(bucc_array_t* a) {
    if (a) a->refcount++;
}

void bucc_array_release(bucc_array_t* a) {
    if (!a) return;
    if (--a->refcount == 0) {
        for (size_t i = 0; i < a->len; i++) {
            bucc_value_release(&a->items[i]);
        }
        free(a->items);
        free(a);
    }
}

static bool array_grow(bucc_array_t* a) {
    size_t new_cap = a->cap * 2;
    bucc_value_t* new_items = realloc(a->items, new_cap * sizeof(bucc_value_t));
    if (!new_items) return false;
    
    a->items = new_items;
    a->cap = new_cap;
    return true;
}

bool bucc_array_push(bucc_array_t* a, bucc_value_t val) {
    if (!a) return false;
    if (a->len >= a->cap && !array_grow(a)) return false;
    
    bucc_value_retain(&val);
    a->items[a->len++] = val;
    return true;
}

bucc_value_t bucc_array_pop(bucc_array_t* a) {
    if (!a || a->len == 0) return BUCC_NULL_VAL;
    return a->items[--a->len];
}

bucc_value_t bucc_array_get(bucc_array_t* a, size_t idx) {
    if (!a || idx >= a->len) return BUCC_NULL_VAL;
    bucc_value_t v = a->items[idx];
    bucc_value_retain(&v);
    return v;
}

bool bucc_array_set(bucc_array_t* a, size_t idx, bucc_value_t val) {
    if (!a || idx >= a->len) return false;
    bucc_value_release(&a->items[idx]);
    bucc_value_retain(&val);
    a->items[idx] = val;
    return true;
}

bucc_map_t* bucc_map_new(size_t initial_cap) {
    bucc_map_t* m = malloc(sizeof(bucc_map_t));
    if (!m) return NULL;
    
    if (initial_cap == 0) initial_cap = 8;
    
    m->entries = calloc(initial_cap, sizeof(bucc_map_entry_t));
    if (!m->entries) {
        free(m);
        return NULL;
    }
    
    m->len = 0;
    m->cap = initial_cap;
    m->refcount = 1;
    
    return m;
}

void bucc_map_retain(bucc_map_t* m) {
    if (m) m->refcount++;
}

void bucc_map_release(bucc_map_t* m) {
    if (!m) return;
    if (--m->refcount == 0) {
        for (size_t i = 0; i < m->len; i++) {
            bucc_string_release(m->entries[i].key);
            if (m->entries[i].value) {
                bucc_value_release(m->entries[i].value);
                free(m->entries[i].value);
            }
        }
        free(m->entries);
        free(m);
    }
}

static int64_t map_find(bucc_map_t* m, bucc_string_t* key) {
    if (!m || !key) return -1;
    for (size_t i = 0; i < m->len; i++) {
        if (m->entries[i].key->hash == key->hash &&
            bucc_string_cmp(m->entries[i].key, key) == 0) {
            return (int64_t)i;
        }
    }
    return -1;
}

bucc_value_t bucc_map_get(bucc_map_t* m, bucc_string_t* key) {
    int64_t idx = map_find(m, key);
    if (idx < 0 || !m->entries[idx].value) return BUCC_NULL_VAL;
    bucc_value_t v = *m->entries[idx].value;
    bucc_value_retain(&v);
    return v;
}

bool bucc_map_set(bucc_map_t* m, bucc_string_t* key, bucc_value_t val) {
    if (!m || !key) return false;
    
    int64_t idx = map_find(m, key);
    if (idx >= 0) {
        if (m->entries[idx].value) {
            bucc_value_release(m->entries[idx].value);
        } else {
            m->entries[idx].value = malloc(sizeof(bucc_value_t));
            if (!m->entries[idx].value) return false;
        }
        bucc_value_retain(&val);
        *m->entries[idx].value = val;
        return true;
    }
    
    if (m->len >= m->cap) {
        size_t new_cap = m->cap * 2;
        bucc_map_entry_t* new_entries = realloc(m->entries, new_cap * sizeof(bucc_map_entry_t));
        if (!new_entries) return false;
        m->entries = new_entries;
        m->cap = new_cap;
    }
    
    bucc_string_retain(key);
    bucc_value_retain(&val);
    m->entries[m->len].key = key;
    m->entries[m->len].value = malloc(sizeof(bucc_value_t));
    if (!m->entries[m->len].value) {
        bucc_string_release(key);
        return false;
    }
    *m->entries[m->len].value = val;
    m->len++;
    
    return true;
}

bool bucc_map_has(bucc_map_t* m, bucc_string_t* key) {
    return map_find(m, key) >= 0;
}

bool bucc_map_delete(bucc_map_t* m, bucc_string_t* key) {
    int64_t idx = map_find(m, key);
    if (idx < 0) return false;
    
    bucc_string_release(m->entries[idx].key);
    if (m->entries[idx].value) {
        bucc_value_release(m->entries[idx].value);
        free(m->entries[idx].value);
    }
    
    if ((size_t)idx < m->len - 1) {
        memmove(&m->entries[idx], &m->entries[idx + 1],
                (m->len - idx - 1) * sizeof(bucc_map_entry_t));
    }
    m->len--;
    
    return true;
}

bucc_array_t* bucc_map_keys(bucc_map_t* m) {
    if (!m) return NULL;
    
    bucc_array_t* keys = bucc_array_new(m->len > 0 ? m->len : 1);
    if (!keys) return NULL;
    
    for (size_t i = 0; i < m->len; i++) {
        bucc_string_retain(m->entries[i].key);
        bucc_array_push(keys, BUCC_STRING_VAL(m->entries[i].key));
    }
    
    return keys;
}

bucc_error_t* bucc_error_new(const char* message, const char* code, int32_t line) {
    bucc_error_t* e = malloc(sizeof(bucc_error_t));
    if (!e) return NULL;
    
    e->message = message ? strdup(message) : NULL;
    e->code = code ? strdup(code) : NULL;
    e->line = line;
    e->refcount = 1;
    
    return e;
}

void bucc_error_retain(bucc_error_t* e) {
    if (e) e->refcount++;
}

void bucc_error_release(bucc_error_t* e) {
    if (!e) return;
    if (--e->refcount == 0) {
        free(e->message);
        free(e->code);
        free(e);
    }
}

void bucc_value_retain(bucc_value_t* v) {
    if (!v) return;
    switch (v->kind) {
        case BUCC_VAL_STRING:   bucc_string_retain(v->as.str); break;
        case BUCC_VAL_ARRAY:    bucc_array_retain(v->as.array); break;
        case BUCC_VAL_MAP:      bucc_map_retain(v->as.map); break;
        case BUCC_VAL_ERROR:    bucc_error_retain(v->as.error); break;
        default: break;
    }
}

void bucc_value_release(bucc_value_t* v) {
    if (!v) return;
    switch (v->kind) {
        case BUCC_VAL_STRING:   bucc_string_release(v->as.str); break;
        case BUCC_VAL_ARRAY:    bucc_array_release(v->as.array); break;
        case BUCC_VAL_MAP:      bucc_map_release(v->as.map); break;
        case BUCC_VAL_ERROR:    bucc_error_release(v->as.error); break;
        default: break;
    }
}

bucc_value_t bucc_value_copy(bucc_value_t* v) {
    if (!v) return BUCC_NULL_VAL;
    bucc_value_retain(v);
    return *v;
}

bool bucc_value_truthy(bucc_value_t* v) {
    if (!v) return false;
    switch (v->kind) {
        case BUCC_VAL_NULL:     return false;
        case BUCC_VAL_BOOL:     return v->as.b;
        case BUCC_VAL_I64:      return v->as.i64 != 0;
        case BUCC_VAL_F64:      return v->as.f64 != 0.0;
        case BUCC_VAL_STRING:   return v->as.str && v->as.str->len > 0;
        case BUCC_VAL_ARRAY:    return v->as.array && v->as.array->len > 0;
        case BUCC_VAL_MAP:      return v->as.map && v->as.map->len > 0;
        case BUCC_VAL_ERROR:    return false;
        default:                return true;
    }
}

bool bucc_value_equal(bucc_value_t* a, bucc_value_t* b) {
    if (!a || !b) return a == b;
    if (a->kind != b->kind) {
        if (BUCC_IS_NUMERIC(*a) && BUCC_IS_NUMERIC(*b)) {
            double da = a->kind == BUCC_VAL_I64 ? (double)a->as.i64 : a->as.f64;
            double db = b->kind == BUCC_VAL_I64 ? (double)b->as.i64 : b->as.f64;
            return da == db;
        }
        return false;
    }
    
    switch (a->kind) {
        case BUCC_VAL_NULL:     return true;
        case BUCC_VAL_BOOL:     return a->as.b == b->as.b;
        case BUCC_VAL_I64:      return a->as.i64 == b->as.i64;
        case BUCC_VAL_F64:      return a->as.f64 == b->as.f64;
        case BUCC_VAL_STRING:   return bucc_string_cmp(a->as.str, b->as.str) == 0;
        case BUCC_VAL_DATE:
            return a->as.date.year == b->as.date.year &&
                   a->as.date.month == b->as.date.month &&
                   a->as.date.day == b->as.date.day;
        case BUCC_VAL_DATETIME:
            return a->as.datetime.year == b->as.datetime.year &&
                   a->as.datetime.month == b->as.datetime.month &&
                   a->as.datetime.day == b->as.datetime.day &&
                   a->as.datetime.hour == b->as.datetime.hour &&
                   a->as.datetime.minute == b->as.datetime.minute &&
                   a->as.datetime.second == b->as.datetime.second;
        case BUCC_VAL_ARRAY:
            if (a->as.array->len != b->as.array->len) return false;
            for (size_t i = 0; i < a->as.array->len; i++) {
                if (!bucc_value_equal(&a->as.array->items[i], &b->as.array->items[i]))
                    return false;
            }
            return true;
        case BUCC_VAL_MAP:
            return a->as.map == b->as.map;
        case BUCC_VAL_ERROR:
            return a->as.error == b->as.error;
        default:
            return false;
    }
}

int bucc_value_compare(bucc_value_t* a, bucc_value_t* b) {
    if (!a && !b) return 0;
    if (!a) return -1;
    if (!b) return 1;
    
    if (BUCC_IS_NUMERIC(*a) && BUCC_IS_NUMERIC(*b)) {
        double da = a->kind == BUCC_VAL_I64 ? (double)a->as.i64 : a->as.f64;
        double db = b->kind == BUCC_VAL_I64 ? (double)b->as.i64 : b->as.f64;
        if (da < db) return -1;
        if (da > db) return 1;
        return 0;
    }
    
    if (a->kind != b->kind) return (int)a->kind - (int)b->kind;
    
    switch (a->kind) {
        case BUCC_VAL_STRING:
            return bucc_string_cmp(a->as.str, b->as.str);
        case BUCC_VAL_DATE: {
            if (a->as.date.year != b->as.date.year)
                return a->as.date.year - b->as.date.year;
            if (a->as.date.month != b->as.date.month)
                return a->as.date.month - b->as.date.month;
            return a->as.date.day - b->as.date.day;
        }
        case BUCC_VAL_DATETIME: {
            if (a->as.datetime.year != b->as.datetime.year)
                return a->as.datetime.year - b->as.datetime.year;
            if (a->as.datetime.month != b->as.datetime.month)
                return a->as.datetime.month - b->as.datetime.month;
            if (a->as.datetime.day != b->as.datetime.day)
                return a->as.datetime.day - b->as.datetime.day;
            if (a->as.datetime.hour != b->as.datetime.hour)
                return a->as.datetime.hour - b->as.datetime.hour;
            if (a->as.datetime.minute != b->as.datetime.minute)
                return a->as.datetime.minute - b->as.datetime.minute;
            return a->as.datetime.second - b->as.datetime.second;
        }
        default:
            return 0;
    }
}

bucc_string_t* bucc_value_to_string(bucc_value_t* v) {
    if (!v) return bucc_string_from_cstr("NULL");
    
    char buf[128];
    
    switch (v->kind) {
        case BUCC_VAL_NULL:
            return bucc_string_from_cstr("NULL");
        case BUCC_VAL_BOOL:
            return bucc_string_from_cstr(v->as.b ? "TRUE" : "FALSE");
        case BUCC_VAL_I64:
            snprintf(buf, sizeof(buf), "%lld", (long long)v->as.i64);
            return bucc_string_from_cstr(buf);
        case BUCC_VAL_F64:
            snprintf(buf, sizeof(buf), "%g", v->as.f64);
            return bucc_string_from_cstr(buf);
        case BUCC_VAL_STRING:
            bucc_string_retain(v->as.str);
            return v->as.str;
        case BUCC_VAL_DATE:
            snprintf(buf, sizeof(buf), "#%04d-%02d-%02d#",
                     v->as.date.year, v->as.date.month, v->as.date.day);
            return bucc_string_from_cstr(buf);
        case BUCC_VAL_DATETIME:
            snprintf(buf, sizeof(buf), "#%04d-%02d-%02dT%02d:%02d:%02d#",
                     v->as.datetime.year, v->as.datetime.month, v->as.datetime.day,
                     v->as.datetime.hour, v->as.datetime.minute, v->as.datetime.second);
            return bucc_string_from_cstr(buf);
        case BUCC_VAL_ARRAY:
            snprintf(buf, sizeof(buf), "[ARRAY len=%zu]", v->as.array ? v->as.array->len : 0);
            return bucc_string_from_cstr(buf);
        case BUCC_VAL_MAP:
            snprintf(buf, sizeof(buf), "{MAP len=%zu}", v->as.map ? v->as.map->len : 0);
            return bucc_string_from_cstr(buf);
        case BUCC_VAL_ERROR:
            if (v->as.error && v->as.error->message) {
                return bucc_string_from_cstr(v->as.error->message);
            }
            return bucc_string_from_cstr("[ERROR]");
        default:
            return bucc_string_from_cstr("[UNKNOWN]");
    }
}

double bucc_value_to_number(bucc_value_t* v, bool* ok) {
    if (ok) *ok = true;
    
    if (!v) {
        if (ok) *ok = false;
        return 0.0;
    }
    
    switch (v->kind) {
        case BUCC_VAL_I64:
            return (double)v->as.i64;
        case BUCC_VAL_F64:
            return v->as.f64;
        case BUCC_VAL_BOOL:
            return v->as.b ? 1.0 : 0.0;
        case BUCC_VAL_STRING:
            if (v->as.str && v->as.str->data) {
                char* end;
                double d = strtod(v->as.str->data, &end);
                if (end != v->as.str->data) return d;
            }
            if (ok) *ok = false;
            return 0.0;
        default:
            if (ok) *ok = false;
            return 0.0;
    }
}

char* bucc_value_to_cstring(bucc_value_t v) {
    bucc_string_t* s = bucc_value_to_string(&v);
    if (!s) return strdup("");
    char* result = strdup(s->data ? s->data : "");
    bucc_string_release(s);
    return result;
}

int64_t bucc_value_to_int(bucc_value_t v) {
    switch (v.kind) {
        case BUCC_VAL_I64:
            return v.as.i64;
        case BUCC_VAL_F64:
            return (int64_t)v.as.f64;
        case BUCC_VAL_BOOL:
            return v.as.b ? 1 : 0;
        case BUCC_VAL_STRING:
            if (v.as.str && v.as.str->data) {
                return strtoll(v.as.str->data, NULL, 10);
            }
            return 0;
        default:
            return 0;
    }
}

bool bucc_value_equals(bucc_value_t a, bucc_value_t b) {
    if (a.kind != b.kind) return false;
    
    switch (a.kind) {
        case BUCC_VAL_NULL:
            return true;
        case BUCC_VAL_BOOL:
            return a.as.b == b.as.b;
        case BUCC_VAL_I64:
            return a.as.i64 == b.as.i64;
        case BUCC_VAL_F64:
            return a.as.f64 == b.as.f64;
        case BUCC_VAL_STRING:
            return bucc_string_cmp(a.as.str, b.as.str) == 0;
        case BUCC_VAL_DATE:
            return a.as.date.year == b.as.date.year &&
                   a.as.date.month == b.as.date.month &&
                   a.as.date.day == b.as.date.day;
        case BUCC_VAL_DATETIME:
            return a.as.datetime.year == b.as.datetime.year &&
                   a.as.datetime.month == b.as.datetime.month &&
                   a.as.datetime.day == b.as.datetime.day &&
                   a.as.datetime.hour == b.as.datetime.hour &&
                   a.as.datetime.minute == b.as.datetime.minute &&
                   a.as.datetime.second == b.as.datetime.second;
        default:
            return false;
    }
}

bucc_value_t bucc_make_string(const char* s) {
    bucc_string_t* str = bucc_string_from_cstr(s ? s : "");
    return (bucc_value_t){.kind = BUCC_VAL_STRING, .as.str = str};
}

bucc_value_t bucc_make_error(const char* msg) {
    bucc_error_t* err = bucc_error_new(msg, NULL, 0);
    return (bucc_value_t){.kind = BUCC_VAL_ERROR, .as.error = err};
}

bucc_value_t* bucc_map_get_cstr(bucc_map_t* m, const char* key) {
    if (!m || !key) return NULL;
    bucc_string_t* k = bucc_string_from_cstr(key);
    bucc_value_t result = bucc_map_get(m, k);
    bucc_string_release(k);
    if (result.kind == BUCC_VAL_NULL) return NULL;
    
    for (size_t i = 0; i < m->len; i++) {
        if (m->entries[i].key && m->entries[i].key->data &&
            strcmp(m->entries[i].key->data, key) == 0) {
            return m->entries[i].value;
        }
    }
    return NULL;
}

bool bucc_map_set_cstr(bucc_map_t* m, const char* key, bucc_value_t val) {
    if (!m || !key) return false;
    bucc_string_t* k = bucc_string_from_cstr(key);
    bool result = bucc_map_set(m, k, val);
    bucc_string_release(k);
    return result;
}

bool bucc_map_has_cstr(bucc_map_t* m, const char* key) {
    if (!m || !key) return false;
    bucc_string_t* k = bucc_string_from_cstr(key);
    bool result = bucc_map_has(m, k);
    bucc_string_release(k);
    return result;
}

bool bucc_map_delete_cstr(bucc_map_t* m, const char* key) {
    if (!m || !key) return false;
    bucc_string_t* k = bucc_string_from_cstr(key);
    bool result = bucc_map_delete(m, k);
    bucc_string_release(k);
    return result;
}
