/*
 * bucc_host.h - Buccaneer host bridge API
 *
 * Part of the Buccaneer language implementation for Mutineer BBS.
 * Defines the C host bridge per BUCCANEER_C_EMBEDDING_API_SPEC.md.
 */

#ifndef BUCC_HOST_H
#define BUCC_HOST_H

#include "bucc_vm.h"
#include "bucc_value.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bucc_host_context bucc_host_context_t;

#define BUCC_CAP_TERM_OUTPUT    (1ULL << 0)
#define BUCC_CAP_TERM_INPUT     (1ULL << 1)
#define BUCC_CAP_TERM_QUERY     (1ULL << 2)
#define BUCC_CAP_USER_READ      (1ULL << 3)
#define BUCC_CAP_DATA_READ      (1ULL << 4)
#define BUCC_CAP_DATA_WRITE     (1ULL << 5)
#define BUCC_CAP_KV_READ        (1ULL << 6)
#define BUCC_CAP_KV_WRITE       (1ULL << 7)
#define BUCC_CAP_TEXT_READ      (1ULL << 8)
#define BUCC_CAP_TEXT_WRITE     (1ULL << 9)
#define BUCC_CAP_BBS_MESSAGE    (1ULL << 10)
#define BUCC_CAP_BBS_QUERY      (1ULL << 11)
#define BUCC_CAP_SHARED_READ    (1ULL << 12)
#define BUCC_CAP_SHARED_WRITE   (1ULL << 13)
#define BUCC_CAP_SESSION_READ   (1ULL << 14)
#define BUCC_CAP_SESSION_WRITE  (1ULL << 15)
#define BUCC_CAP_DOOR_CHAIN     (1ULL << 16)

#define BUCC_CAP_ALL            0xFFFFFFFFFFFFFFFFULL
#define BUCC_CAP_SAFE           (BUCC_CAP_TERM_OUTPUT | BUCC_CAP_TERM_INPUT | \
                                 BUCC_CAP_TERM_QUERY | BUCC_CAP_USER_READ)

typedef bucc_value_t (*bucc_native_fn_t)(bucc_host_context_t* ctx,
                                         bucc_value_t* args, int argc);

typedef struct bucc_host_function {
    const char*     ns;
    const char*     name;
    const char*     capability;
    bucc_native_fn_t handler;
    int             min_args;
    int             max_args;
    bool            throttled;
} bucc_host_function_t;

typedef struct bucc_term_api {
    void (*print)(void* ctx, const char* text);
    void (*println)(void* ctx, const char* text);
    void (*cls)(void* ctx);
    void (*color)(void* ctx, int fg, int bg);
    void (*gotoxy)(void* ctx, int x, int y);
    char* (*getkey)(void* ctx);
    char* (*input)(void* ctx, const char* prompt, int maxlen);
    char* (*input_password)(void* ctx, const char* prompt);
    void (*pause)(void* ctx, const char* prompt);
    int (*get_width)(void* ctx);
    int (*get_height)(void* ctx);
    bool (*supports_ansi)(void* ctx);
} bucc_term_api_t;

typedef struct bucc_user_api {
    const char* (*get_name)(void* ctx);
    const char* (*get_alias)(void* ctx);
    int64_t (*get_id)(void* ctx);
    int (*get_security)(void* ctx);
    int (*get_time_left)(void* ctx);
    int (*get_flags)(void* ctx);
} bucc_user_api_t;

typedef struct bucc_data_api {
    int64_t (*insert)(void* ctx, const char* dataset, bucc_map_t* record);
    int (*update)(void* ctx, const char* dataset, int64_t id, bucc_map_t* fields);
    int (*delete_record)(void* ctx, const char* dataset, int64_t id);
    bucc_map_t* (*get)(void* ctx, const char* dataset, int64_t id);
    bucc_array_t* (*find)(void* ctx, const char* dataset, bucc_map_t* query,
                          const char* order, int limit, int offset);
    int64_t (*count)(void* ctx, const char* dataset, bucc_map_t* query);
    bool (*begin_tx)(void* ctx);
    bool (*commit_tx)(void* ctx);
    bool (*rollback_tx)(void* ctx);
} bucc_data_api_t;

typedef struct bucc_kv_api {
    char* (*get)(void* ctx, const char* key, const char* default_val);
    void (*set)(void* ctx, const char* key, const char* value);
    void (*delete_key)(void* ctx, const char* key);
    bool (*exists)(void* ctx, const char* key);
} bucc_kv_api_t;

typedef struct bucc_text_api {
    char* (*read_all)(void* ctx, const char* path);
    bucc_array_t* (*read_lines)(void* ctx, const char* path);
    bool (*write_all)(void* ctx, const char* path, const char* content);
    bool (*append)(void* ctx, const char* path, const char* content);
    bool (*exists)(void* ctx, const char* path);
} bucc_text_api_t;

typedef struct bucc_bbs_api {
    bool (*send_msg)(void* ctx, int node, const char* msg);
    bucc_array_t* (*get_online)(void* ctx);
    int (*get_node)(void* ctx);
} bucc_bbs_api_t;

typedef struct bucc_users_api {
    bucc_array_t* (*find)(void* ctx, bucc_map_t* query, int limit);
    bucc_map_t* (*get)(void* ctx, int64_t id);
} bucc_users_api_t;

typedef struct bucc_msg_api {
    bucc_map_t* (*read)(void* ctx, const char* area, int64_t id);
    bucc_array_t* (*list)(void* ctx, const char* area, int limit, int offset);
    int64_t (*post)(void* ctx, const char* area, bucc_map_t* msg);
} bucc_msg_api_t;

typedef struct bucc_file_api {
    bucc_array_t* (*list)(void* ctx, const char* area, int limit, int offset);
    bucc_map_t* (*info)(void* ctx, const char* area, int64_t id);
} bucc_file_api_t;

struct bucc_host_context {
    void*           session_ctx;
    void*           user_ctx;
    void*           data_ctx;
    void*           kv_ctx;
    void*           text_ctx;
    void*           bbs_ctx;
    void*           users_ctx;
    void*           msg_ctx;
    void*           file_ctx;
    
    bucc_term_api_t*   term;
    bucc_user_api_t*   user;
    bucc_data_api_t*   data;
    bucc_kv_api_t*     kv;
    bucc_text_api_t*   text;
    bucc_bbs_api_t*    bbs;
    bucc_users_api_t*  users;
    bucc_msg_api_t*    msg;
    bucc_file_api_t*   file;
    
    bucc_map_t*     app_state;
    bucc_map_t*     shared_state;
    bucc_map_t*     session_state;
    
    bucc_vm_t*      vm;

    uint64_t        allowed_capabilities;
    
    uint32_t        throttle_count;
    uint32_t        throttle_limit;
    uint64_t        throttle_window_start;
};

bucc_host_context_t* bucc_host_context_new(void);
void bucc_host_context_free(bucc_host_context_t* ctx);
void bucc_host_set_allowed_capabilities(bucc_host_context_t* ctx, uint64_t capabilities);

void bucc_host_set_term_api(bucc_host_context_t* ctx, bucc_term_api_t* api, void* session_ctx);
void bucc_host_set_user_api(bucc_host_context_t* ctx, bucc_user_api_t* api, void* user_ctx);
void bucc_host_set_data_api(bucc_host_context_t* ctx, bucc_data_api_t* api, void* data_ctx);
void bucc_host_set_kv_api(bucc_host_context_t* ctx, bucc_kv_api_t* api, void* kv_ctx);
void bucc_host_set_text_api(bucc_host_context_t* ctx, bucc_text_api_t* api, void* text_ctx);
void bucc_host_set_bbs_api(bucc_host_context_t* ctx, bucc_bbs_api_t* api, void* bbs_ctx);
void bucc_host_set_users_api(bucc_host_context_t* ctx, bucc_users_api_t* api, void* users_ctx);
void bucc_host_set_msg_api(bucc_host_context_t* ctx, bucc_msg_api_t* api, void* msg_ctx);
void bucc_host_set_file_api(bucc_host_context_t* ctx, bucc_file_api_t* api, void* file_ctx);

bucc_value_t bucc_host_dispatch(bucc_vm_t* vm, const char* ns, const char* fn,
                                bucc_value_t* args, int argc, void* ctx);

bucc_value_t bucc_host_term_print(bucc_host_context_t* ctx, bucc_value_t* args, int argc);
bucc_value_t bucc_host_term_println(bucc_host_context_t* ctx, bucc_value_t* args, int argc);
bucc_value_t bucc_host_term_cls(bucc_host_context_t* ctx, bucc_value_t* args, int argc);
bucc_value_t bucc_host_term_color(bucc_host_context_t* ctx, bucc_value_t* args, int argc);
bucc_value_t bucc_host_term_gotoxy(bucc_host_context_t* ctx, bucc_value_t* args, int argc);
bucc_value_t bucc_host_term_getkey(bucc_host_context_t* ctx, bucc_value_t* args, int argc);
bucc_value_t bucc_host_term_input(bucc_host_context_t* ctx, bucc_value_t* args, int argc);
bucc_value_t bucc_host_term_input_password(bucc_host_context_t* ctx, bucc_value_t* args, int argc);
bucc_value_t bucc_host_term_pause(bucc_host_context_t* ctx, bucc_value_t* args, int argc);
bucc_value_t bucc_host_term_width(bucc_host_context_t* ctx, bucc_value_t* args, int argc);
bucc_value_t bucc_host_term_height(bucc_host_context_t* ctx, bucc_value_t* args, int argc);
bucc_value_t bucc_host_term_supports_ansi(bucc_host_context_t* ctx, bucc_value_t* args, int argc);

bucc_value_t bucc_host_user_name(bucc_host_context_t* ctx, bucc_value_t* args, int argc);
bucc_value_t bucc_host_user_alias(bucc_host_context_t* ctx, bucc_value_t* args, int argc);
bucc_value_t bucc_host_user_id(bucc_host_context_t* ctx, bucc_value_t* args, int argc);
bucc_value_t bucc_host_user_security(bucc_host_context_t* ctx, bucc_value_t* args, int argc);
bucc_value_t bucc_host_user_time_left(bucc_host_context_t* ctx, bucc_value_t* args, int argc);

bucc_value_t bucc_host_data_insert(bucc_host_context_t* ctx, bucc_value_t* args, int argc);
bucc_value_t bucc_host_data_update(bucc_host_context_t* ctx, bucc_value_t* args, int argc);
bucc_value_t bucc_host_data_delete(bucc_host_context_t* ctx, bucc_value_t* args, int argc);
bucc_value_t bucc_host_data_get(bucc_host_context_t* ctx, bucc_value_t* args, int argc);
bucc_value_t bucc_host_data_find(bucc_host_context_t* ctx, bucc_value_t* args, int argc);
bucc_value_t bucc_host_data_count(bucc_host_context_t* ctx, bucc_value_t* args, int argc);
bucc_value_t bucc_host_data_begin(bucc_host_context_t* ctx, bucc_value_t* args, int argc);
bucc_value_t bucc_host_data_commit(bucc_host_context_t* ctx, bucc_value_t* args, int argc);
bucc_value_t bucc_host_data_rollback(bucc_host_context_t* ctx, bucc_value_t* args, int argc);

bucc_value_t bucc_host_kv_get(bucc_host_context_t* ctx, bucc_value_t* args, int argc);
bucc_value_t bucc_host_kv_set(bucc_host_context_t* ctx, bucc_value_t* args, int argc);
bucc_value_t bucc_host_kv_delete(bucc_host_context_t* ctx, bucc_value_t* args, int argc);
bucc_value_t bucc_host_kv_exists(bucc_host_context_t* ctx, bucc_value_t* args, int argc);

bucc_value_t bucc_host_app_get(bucc_host_context_t* ctx, bucc_value_t* args, int argc);
bucc_value_t bucc_host_app_set(bucc_host_context_t* ctx, bucc_value_t* args, int argc);

bucc_value_t bucc_host_shared_get(bucc_host_context_t* ctx, bucc_value_t* args, int argc);
bucc_value_t bucc_host_shared_set(bucc_host_context_t* ctx, bucc_value_t* args, int argc);
bucc_value_t bucc_host_shared_cas(bucc_host_context_t* ctx, bucc_value_t* args, int argc);

bucc_value_t bucc_host_text_read_all(bucc_host_context_t* ctx, bucc_value_t* args, int argc);
bucc_value_t bucc_host_text_read_lines(bucc_host_context_t* ctx, bucc_value_t* args, int argc);
bucc_value_t bucc_host_text_write_all(bucc_host_context_t* ctx, bucc_value_t* args, int argc);
bucc_value_t bucc_host_text_append(bucc_host_context_t* ctx, bucc_value_t* args, int argc);
bucc_value_t bucc_host_text_exists(bucc_host_context_t* ctx, bucc_value_t* args, int argc);

bucc_value_t bucc_host_sys_now(bucc_host_context_t* ctx, bucc_value_t* args, int argc);
bucc_value_t bucc_host_sys_today(bucc_host_context_t* ctx, bucc_value_t* args, int argc);
bucc_value_t bucc_host_sys_sleep(bucc_host_context_t* ctx, bucc_value_t* args, int argc);

bucc_value_t bucc_host_session_get(bucc_host_context_t* ctx, bucc_value_t* args, int argc);
bucc_value_t bucc_host_session_set(bucc_host_context_t* ctx, bucc_value_t* args, int argc);
bucc_value_t bucc_host_session_node(bucc_host_context_t* ctx, bucc_value_t* args, int argc);
bucc_value_t bucc_host_session_elapsed_ms(bucc_host_context_t* ctx, bucc_value_t* args, int argc);

bucc_value_t bucc_host_bbs_send_msg(bucc_host_context_t* ctx, bucc_value_t* args, int argc);
bucc_value_t bucc_host_bbs_online(bucc_host_context_t* ctx, bucc_value_t* args, int argc);
bucc_value_t bucc_host_bbs_node(bucc_host_context_t* ctx, bucc_value_t* args, int argc);

bucc_value_t bucc_host_users_find(bucc_host_context_t* ctx, bucc_value_t* args, int argc);
bucc_value_t bucc_host_users_get(bucc_host_context_t* ctx, bucc_value_t* args, int argc);

bucc_value_t bucc_host_msg_read(bucc_host_context_t* ctx, bucc_value_t* args, int argc);
bucc_value_t bucc_host_msg_list(bucc_host_context_t* ctx, bucc_value_t* args, int argc);
bucc_value_t bucc_host_msg_post(bucc_host_context_t* ctx, bucc_value_t* args, int argc);

bucc_value_t bucc_host_file_list(bucc_host_context_t* ctx, bucc_value_t* args, int argc);
bucc_value_t bucc_host_file_info(bucc_host_context_t* ctx, bucc_value_t* args, int argc);

bucc_value_t bucc_host_door_exit(bucc_host_context_t* ctx, bucc_value_t* args, int argc);
bucc_value_t bucc_host_door_chain(bucc_host_context_t* ctx, bucc_value_t* args, int argc);

#ifdef __cplusplus
}
#endif

#endif /* BUCC_HOST_H */
