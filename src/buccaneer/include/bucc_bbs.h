/*
 * bucc_bbs.h - Buccaneer BBS integration API
 *
 * Part of the Buccaneer language implementation for Mutineer BBS.
 * Provides the C embedding API for running doors within Mutineer.
 */

#ifndef BUCC_BBS_H
#define BUCC_BBS_H

#include "bucc_vm.h"
#include "bucc_module.h"
#include "bucc_host.h"
#include "bucc_package.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum bucc_door_status {
    DOOR_OK = 0,
    DOOR_ERROR_LOAD,
    DOOR_ERROR_MANIFEST,
    DOOR_ERROR_SECURITY,
    DOOR_ERROR_CAPABILITY,
    DOOR_ERROR_RUNTIME,
    DOOR_ERROR_TIMEOUT,
    DOOR_CHAIN_REQUESTED
} bucc_door_status_t;

typedef struct bucc_door_result {
    bucc_door_status_t  status;
    char*               error_message;
    char*               chain_target;
    bucc_value_t        chain_args;
    int                 exit_code;
    uint32_t            instructions_executed;
    uint32_t            host_calls_made;
} bucc_door_result_t;

typedef struct bucc_session_info {
    int64_t     user_id;
    const char* user_name;
    const char* user_alias;
    int         user_security;
    int         time_remaining;
    int         node_number;
    bool        ansi_enabled;
    int         term_width;
    int         term_height;
} bucc_session_info_t;

typedef struct bucc_door_runner {
    bucc_door_manifest_t*   manifest;
    bucc_module_t*          module;
    bucc_vm_t*              vm;
    bucc_host_context_t*    host_ctx;
    bucc_session_info_t     session;
    
    bucc_limits_t           limits;
    uint64_t                allowed_capabilities;
    
    bool                    running;
    bool                    debug;
} bucc_door_runner_t;

bucc_door_runner_t* bucc_door_runner_new(void);
void bucc_door_runner_free(bucc_door_runner_t* runner);

bucc_door_status_t bucc_door_load(bucc_door_runner_t* runner, 
                                  const char* manifest_path);

bucc_door_status_t bucc_door_load_module(bucc_door_runner_t* runner,
                                         const char* module_path);

void bucc_door_set_session(bucc_door_runner_t* runner,
                          const bucc_session_info_t* session);

void bucc_door_set_term_api(bucc_door_runner_t* runner, bucc_term_api_t* api);
void bucc_door_set_user_api(bucc_door_runner_t* runner, bucc_user_api_t* api);
void bucc_door_set_data_api(bucc_door_runner_t* runner, bucc_data_api_t* api);
void bucc_door_set_kv_api(bucc_door_runner_t* runner, bucc_kv_api_t* api);
void bucc_door_set_text_api(bucc_door_runner_t* runner, bucc_text_api_t* api);
void bucc_door_set_bbs_api(bucc_door_runner_t* runner, bucc_bbs_api_t* api);
void bucc_door_set_term_api_ex(bucc_door_runner_t* runner, bucc_term_api_t* api, void* ctx);
void bucc_door_set_user_api_ex(bucc_door_runner_t* runner, bucc_user_api_t* api, void* ctx);
void bucc_door_set_data_api_ex(bucc_door_runner_t* runner, bucc_data_api_t* api, void* ctx);
void bucc_door_set_kv_api_ex(bucc_door_runner_t* runner, bucc_kv_api_t* api, void* ctx);
void bucc_door_set_text_api_ex(bucc_door_runner_t* runner, bucc_text_api_t* api, void* ctx);
void bucc_door_set_bbs_api_ex(bucc_door_runner_t* runner, bucc_bbs_api_t* api, void* ctx);

void bucc_door_set_limits(bucc_door_runner_t* runner, const bucc_limits_t* limits);

void bucc_door_set_capability(bucc_door_runner_t* runner, const char* cap, bool allowed);

bucc_door_result_t bucc_door_run(bucc_door_runner_t* runner);

void bucc_door_stop(bucc_door_runner_t* runner);

bucc_door_result_t bucc_door_run_simple(const char* path,
                                        bucc_term_api_t* term,
                                        void* term_ctx,
                                        const bucc_session_info_t* session);

const char* bucc_door_status_string(bucc_door_status_t status);

#ifdef __cplusplus
}
#endif

#endif /* BUCC_BBS_H */
