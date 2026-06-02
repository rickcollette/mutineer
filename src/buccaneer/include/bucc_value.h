/*
 * bucc_value.h - Buccaneer runtime value representation
 *
 * Part of the Buccaneer language implementation for Mutineer BBS.
 * Values are tagged unions representing all Buccaneer types.
 */

#ifndef BUCC_VALUE_H
#define BUCC_VALUE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum bucc_value_kind {
    BUCC_VAL_NULL = 0,
    BUCC_VAL_BOOL,
    BUCC_VAL_I64,
    BUCC_VAL_F64,
    BUCC_VAL_STRING,
    BUCC_VAL_DATE,
    BUCC_VAL_DATETIME,
    BUCC_VAL_ARRAY,
    BUCC_VAL_MAP,
    BUCC_VAL_ERROR
} bucc_value_kind_t;

typedef struct bucc_string {
    char*    data;
    size_t   len;
    uint32_t refcount;
    uint32_t hash;
} bucc_string_t;

typedef struct bucc_date {
    int16_t year;
    uint8_t month;
    uint8_t day;
} bucc_date_t;

typedef struct bucc_datetime {
    int16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t _pad;
} bucc_datetime_t;

typedef struct bucc_array {
    struct bucc_value* items;
    size_t   len;
    size_t   cap;
    uint32_t refcount;
} bucc_array_t;

struct bucc_value;

typedef struct bucc_map_entry {
    bucc_string_t*      key;
    struct bucc_value*  value;
} bucc_map_entry_t;

typedef struct bucc_map {
    bucc_map_entry_t* entries;
    size_t   len;
    size_t   cap;
    uint32_t refcount;
} bucc_map_t;

typedef struct bucc_error {
    char*    message;
    char*    code;
    int32_t  line;
    uint32_t refcount;
} bucc_error_t;

typedef struct bucc_value {
    bucc_value_kind_t kind;
    union {
        bool            b;
        int64_t         i64;
        double          f64;
        bucc_string_t*  str;
        bucc_date_t     date;
        bucc_datetime_t datetime;
        bucc_array_t*   array;
        bucc_map_t*     map;
        bucc_error_t*   error;
    } as;
} bucc_value_t;

#define BUCC_NULL_VAL       ((bucc_value_t){.kind = BUCC_VAL_NULL})
#define BUCC_BOOL_VAL(v)    ((bucc_value_t){.kind = BUCC_VAL_BOOL, .as.b = (v)})
#define BUCC_I64_VAL(v)     ((bucc_value_t){.kind = BUCC_VAL_I64, .as.i64 = (v)})
#define BUCC_F64_VAL(v)     ((bucc_value_t){.kind = BUCC_VAL_F64, .as.f64 = (v)})
#define BUCC_STRING_VAL(v)  ((bucc_value_t){.kind = BUCC_VAL_STRING, .as.str = (v)})
#define BUCC_DATE_VAL(v)    ((bucc_value_t){.kind = BUCC_VAL_DATE, .as.date = (v)})
#define BUCC_DATETIME_VAL(v) ((bucc_value_t){.kind = BUCC_VAL_DATETIME, .as.datetime = (v)})
#define BUCC_ARRAY_VAL(v)   ((bucc_value_t){.kind = BUCC_VAL_ARRAY, .as.array = (v)})
#define BUCC_MAP_VAL(v)     ((bucc_value_t){.kind = BUCC_VAL_MAP, .as.map = (v)})
#define BUCC_ERROR_VAL(v)   ((bucc_value_t){.kind = BUCC_VAL_ERROR, .as.error = (v)})

#define BUCC_IS_NULL(v)     ((v).kind == BUCC_VAL_NULL)
#define BUCC_IS_BOOL(v)     ((v).kind == BUCC_VAL_BOOL)
#define BUCC_IS_I64(v)      ((v).kind == BUCC_VAL_I64)
#define BUCC_IS_F64(v)      ((v).kind == BUCC_VAL_F64)
#define BUCC_IS_STRING(v)   ((v).kind == BUCC_VAL_STRING)
#define BUCC_IS_DATE(v)     ((v).kind == BUCC_VAL_DATE)
#define BUCC_IS_DATETIME(v) ((v).kind == BUCC_VAL_DATETIME)
#define BUCC_IS_ARRAY(v)    ((v).kind == BUCC_VAL_ARRAY)
#define BUCC_IS_MAP(v)      ((v).kind == BUCC_VAL_MAP)
#define BUCC_IS_ERROR(v)    ((v).kind == BUCC_VAL_ERROR)
#define BUCC_IS_NUMERIC(v)  ((v).kind == BUCC_VAL_I64 || (v).kind == BUCC_VAL_F64)

#define BUCC_AS_BOOL(v)     ((v).as.b)
#define BUCC_AS_I64(v)      ((v).as.i64)
#define BUCC_AS_F64(v)      ((v).as.f64)
#define BUCC_AS_STRING(v)   ((v).as.str)
#define BUCC_AS_DATE(v)     ((v).as.date)
#define BUCC_AS_DATETIME(v) ((v).as.datetime)
#define BUCC_AS_ARRAY(v)    ((v).as.array)
#define BUCC_AS_MAP(v)      ((v).as.map)
#define BUCC_AS_ERROR(v)    ((v).as.error)

const char* bucc_value_kind_name(bucc_value_kind_t kind);

bucc_string_t* bucc_string_new(const char* data, size_t len);
bucc_string_t* bucc_string_from_cstr(const char* cstr);
bucc_string_t* bucc_string_concat(bucc_string_t* a, bucc_string_t* b);
void bucc_string_retain(bucc_string_t* s);
void bucc_string_release(bucc_string_t* s);
int bucc_string_cmp(bucc_string_t* a, bucc_string_t* b);
uint32_t bucc_string_hash(bucc_string_t* s);

bucc_array_t* bucc_array_new(size_t initial_cap);
void bucc_array_retain(bucc_array_t* a);
void bucc_array_release(bucc_array_t* a);
bool bucc_array_push(bucc_array_t* a, bucc_value_t val);
bucc_value_t bucc_array_pop(bucc_array_t* a);
bucc_value_t bucc_array_get(bucc_array_t* a, size_t idx);
bool bucc_array_set(bucc_array_t* a, size_t idx, bucc_value_t val);

bucc_map_t* bucc_map_new(size_t initial_cap);
void bucc_map_retain(bucc_map_t* m);
void bucc_map_release(bucc_map_t* m);
bucc_value_t bucc_map_get(bucc_map_t* m, bucc_string_t* key);
bool bucc_map_set(bucc_map_t* m, bucc_string_t* key, bucc_value_t val);
bool bucc_map_has(bucc_map_t* m, bucc_string_t* key);
bool bucc_map_delete(bucc_map_t* m, bucc_string_t* key);
bucc_array_t* bucc_map_keys(bucc_map_t* m);

bucc_value_t* bucc_map_get_cstr(bucc_map_t* m, const char* key);
bool bucc_map_set_cstr(bucc_map_t* m, const char* key, bucc_value_t val);
bool bucc_map_has_cstr(bucc_map_t* m, const char* key);
bool bucc_map_delete_cstr(bucc_map_t* m, const char* key);

bucc_error_t* bucc_error_new(const char* message, const char* code, int32_t line);
void bucc_error_retain(bucc_error_t* e);
void bucc_error_release(bucc_error_t* e);

void bucc_value_retain(bucc_value_t* v);
void bucc_value_release(bucc_value_t* v);
bucc_value_t bucc_value_copy(bucc_value_t* v);
bool bucc_value_truthy(bucc_value_t* v);
bool bucc_value_equal(bucc_value_t* a, bucc_value_t* b);
int bucc_value_compare(bucc_value_t* a, bucc_value_t* b);

bucc_string_t* bucc_value_to_string(bucc_value_t* v);
char* bucc_value_to_cstring(bucc_value_t v);
int64_t bucc_value_to_int(bucc_value_t v);
double bucc_value_to_number(bucc_value_t* v, bool* ok);
bool bucc_value_equals(bucc_value_t a, bucc_value_t b);

bucc_value_t bucc_make_string(const char* s);
bucc_value_t bucc_make_error(const char* msg);

#ifdef __cplusplus
}
#endif

#endif /* BUCC_VALUE_H */
