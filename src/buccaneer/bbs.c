/*
 * bbs.c - Buccaneer BBS integration implementation
 *
 * Part of the Buccaneer language implementation for Mutineer BBS.
 */

#define _POSIX_C_SOURCE 200809L
#include "include/bucc_bbs.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

const char* bucc_door_status_string(bucc_door_status_t status) {
    switch (status) {
        case DOOR_OK: return "OK";
        case DOOR_ERROR_LOAD: return "Failed to load door";
        case DOOR_ERROR_MANIFEST: return "Invalid manifest";
        case DOOR_ERROR_SECURITY: return "Security level insufficient";
        case DOOR_ERROR_CAPABILITY: return "Required capability not granted";
        case DOOR_ERROR_RUNTIME: return "Runtime error";
        case DOOR_ERROR_TIMEOUT: return "Execution timeout";
        case DOOR_CHAIN_REQUESTED: return "Chain to another door requested";
        default: return "Unknown status";
    }
}

bucc_door_runner_t* bucc_door_runner_new(void) {
    bucc_door_runner_t* runner = calloc(1, sizeof(bucc_door_runner_t));
    if (!runner) return NULL;
    
    runner->host_ctx = bucc_host_context_new();
    
    runner->limits.max_instructions = 10000000;
    runner->limits.max_stack_depth = 1024;
    runner->limits.max_heap_bytes = 16 * 1024 * 1024;
    runner->limits.max_string_bytes = 1024 * 1024;
    runner->limits.max_host_calls = 100000;
    
    runner->allowed_capabilities = BUCC_CAP_SAFE;
    
    return runner;
}

void bucc_door_runner_free(bucc_door_runner_t* runner) {
    if (!runner) return;
    
    if (runner->vm) {
        bucc_vm_free(runner->vm);
    }
    if (runner->module) {
        bucc_module_free(runner->module);
    }
    if (runner->manifest) {
        bucc_manifest_free(runner->manifest);
    }
    if (runner->host_ctx) {
        bucc_host_context_free(runner->host_ctx);
    }
    
    free(runner);
}

bucc_door_status_t bucc_door_load(bucc_door_runner_t* runner,
                                  const char* manifest_path) {
    if (!runner || !manifest_path) return DOOR_ERROR_LOAD;
    
    bucc_pkg_error_t pkg_err;
    runner->manifest = bucc_manifest_load(manifest_path, &pkg_err);
    
    if (!runner->manifest) {
        return DOOR_ERROR_MANIFEST;
    }
    
    char error_buf[256];
    if (!bucc_manifest_validate(runner->manifest, error_buf, sizeof(error_buf))) {
        return DOOR_ERROR_MANIFEST;
    }
    
    char* module_path = bucc_manifest_get_module_path(runner->manifest);
    runner->module = bucc_module_load(module_path);
    free(module_path);
    
    if (!runner->module) {
        return DOOR_ERROR_LOAD;
    }
    
    return DOOR_OK;
}

bucc_door_status_t bucc_door_load_module(bucc_door_runner_t* runner,
                                         const char* module_path) {
    if (!runner || !module_path) return DOOR_ERROR_LOAD;
    
    runner->module = bucc_module_load(module_path);
    if (!runner->module) {
        return DOOR_ERROR_LOAD;
    }
    
    return DOOR_OK;
}

void bucc_door_set_session(bucc_door_runner_t* runner,
                          const bucc_session_info_t* session) {
    if (!runner || !session) return;
    runner->session = *session;
}

void bucc_door_set_term_api(bucc_door_runner_t* runner, bucc_term_api_t* api) {
    if (!runner || !runner->host_ctx) return;
    bucc_host_set_term_api(runner->host_ctx, api, NULL);
}

void bucc_door_set_user_api(bucc_door_runner_t* runner, bucc_user_api_t* api) {
    if (!runner || !runner->host_ctx) return;
    bucc_host_set_user_api(runner->host_ctx, api, NULL);
}

void bucc_door_set_data_api(bucc_door_runner_t* runner, bucc_data_api_t* api) {
    if (!runner || !runner->host_ctx) return;
    bucc_host_set_data_api(runner->host_ctx, api, NULL);
}

void bucc_door_set_kv_api(bucc_door_runner_t* runner, bucc_kv_api_t* api) {
    if (!runner || !runner->host_ctx) return;
    bucc_host_set_kv_api(runner->host_ctx, api, NULL);
}

void bucc_door_set_text_api(bucc_door_runner_t* runner, bucc_text_api_t* api) {
    if (!runner || !runner->host_ctx) return;
    bucc_host_set_text_api(runner->host_ctx, api, NULL);
}

void bucc_door_set_bbs_api(bucc_door_runner_t* runner, bucc_bbs_api_t* api) {
    if (!runner || !runner->host_ctx) return;
    bucc_host_set_bbs_api(runner->host_ctx, api, NULL);
}

void bucc_door_set_limits(bucc_door_runner_t* runner, const bucc_limits_t* limits) {
    if (!runner || !limits) return;
    runner->limits = *limits;
}

static uint64_t capability_from_string(const char* cap) {
    if (!cap) return 0;
    
    if (strcmp(cap, "term.output") == 0) return BUCC_CAP_TERM_OUTPUT;
    if (strcmp(cap, "term.input") == 0) return BUCC_CAP_TERM_INPUT;
    if (strcmp(cap, "term.query") == 0) return BUCC_CAP_TERM_QUERY;
    if (strcmp(cap, "user.read") == 0) return BUCC_CAP_USER_READ;
    if (strcmp(cap, "data.read") == 0) return BUCC_CAP_DATA_READ;
    if (strcmp(cap, "data.write") == 0) return BUCC_CAP_DATA_WRITE;
    if (strcmp(cap, "kv.read") == 0) return BUCC_CAP_KV_READ;
    if (strcmp(cap, "kv.write") == 0) return BUCC_CAP_KV_WRITE;
    if (strcmp(cap, "text.read") == 0) return BUCC_CAP_TEXT_READ;
    if (strcmp(cap, "text.write") == 0) return BUCC_CAP_TEXT_WRITE;
    if (strcmp(cap, "bbs.message") == 0) return BUCC_CAP_BBS_MESSAGE;
    if (strcmp(cap, "bbs.query") == 0) return BUCC_CAP_BBS_QUERY;
    if (strcmp(cap, "shared.read") == 0) return BUCC_CAP_SHARED_READ;
    if (strcmp(cap, "shared.write") == 0) return BUCC_CAP_SHARED_WRITE;
    if (strcmp(cap, "session.read") == 0) return BUCC_CAP_SESSION_READ;
    if (strcmp(cap, "session.write") == 0) return BUCC_CAP_SESSION_WRITE;
    if (strcmp(cap, "door.chain") == 0) return BUCC_CAP_DOOR_CHAIN;
    
    return 0;
}

void bucc_door_set_capability(bucc_door_runner_t* runner, const char* cap, bool allowed) {
    if (!runner || !cap) return;
    
    uint64_t bit = capability_from_string(cap);
    if (allowed) {
        runner->allowed_capabilities |= bit;
    } else {
        runner->allowed_capabilities &= ~bit;
    }
}

bucc_door_result_t bucc_door_run(bucc_door_runner_t* runner) {
    bucc_door_result_t result = {0};
    
    if (!runner || !runner->module) {
        result.status = DOOR_ERROR_LOAD;
        result.error_message = strdup("No module loaded");
        return result;
    }
    
    if (runner->manifest) {
        if (runner->session.user_security < runner->manifest->min_security) {
            result.status = DOOR_ERROR_SECURITY;
            result.error_message = strdup("User security level too low");
            return result;
        }
        
        for (size_t i = 0; i < runner->manifest->capability_count; i++) {
            if (runner->manifest->capabilities[i].required) {
                uint64_t cap = capability_from_string(
                    runner->manifest->capabilities[i].name);
                if (!(runner->allowed_capabilities & cap)) {
                    result.status = DOOR_ERROR_CAPABILITY;
                    char msg[256];
                    snprintf(msg, sizeof(msg), "Required capability not granted: %s",
                            runner->manifest->capabilities[i].name);
                    result.error_message = strdup(msg);
                    return result;
                }
            }
        }
    }
    
    runner->vm = bucc_vm_new(runner->module);
    if (!runner->vm) {
        result.status = DOOR_ERROR_RUNTIME;
        result.error_message = strdup("Failed to create VM");
        return result;
    }
    
    bucc_vm_set_host_handler(runner->vm, bucc_host_dispatch, runner->host_ctx);
    bucc_vm_set_limits(runner->vm, &runner->limits);
    runner->vm->debug_enabled = runner->debug;
    
    runner->running = true;
    bucc_vm_status_t vm_status = bucc_vm_run(runner->vm);
    runner->running = false;
    
    result.instructions_executed = runner->vm->counters.instructions;
    result.host_calls_made = runner->vm->counters.host_calls;
    
    switch (vm_status) {
        case VM_OK:
        case VM_HALT:
            result.status = DOOR_OK;
            break;
            
        case VM_CHAIN:
            result.status = DOOR_CHAIN_REQUESTED;
            if (runner->vm->chain_target) {
                result.chain_target = strdup(runner->vm->chain_target);
            }
            result.chain_args = runner->vm->chain_args;
            break;
            
        case VM_LIMIT_EXCEEDED:
            result.status = DOOR_ERROR_TIMEOUT;
            result.error_message = strdup("Execution limit exceeded");
            break;
            
        default:
            result.status = DOOR_ERROR_RUNTIME;
            if (runner->vm->error_message) {
                result.error_message = strdup(runner->vm->error_message);
            } else {
                result.error_message = strdup("Unknown runtime error");
            }
            break;
    }
    
    return result;
}

void bucc_door_stop(bucc_door_runner_t* runner) {
    if (!runner || !runner->vm) return;
    runner->vm->running = false;
    runner->running = false;
}

bucc_door_result_t bucc_door_run_simple(const char* path,
                                        bucc_term_api_t* term,
                                        void* term_ctx,
                                        const bucc_session_info_t* session) {
    bucc_door_result_t result = {0};
    
    bucc_door_runner_t* runner = bucc_door_runner_new();
    if (!runner) {
        result.status = DOOR_ERROR_RUNTIME;
        result.error_message = strdup("Failed to create runner");
        return result;
    }
    
    size_t len = strlen(path);
    bucc_door_status_t load_status;
    
    if (len > 5 && strcmp(path + len - 5, ".json") == 0) {
        load_status = bucc_door_load(runner, path);
    } else {
        load_status = bucc_door_load_module(runner, path);
    }
    
    if (load_status != DOOR_OK) {
        result.status = load_status;
        result.error_message = strdup("Failed to load door");
        bucc_door_runner_free(runner);
        return result;
    }
    
    if (session) {
        bucc_door_set_session(runner, session);
    }
    
    if (term) {
        bucc_host_set_term_api(runner->host_ctx, term, term_ctx);
    }
    
    runner->allowed_capabilities = BUCC_CAP_ALL;
    
    result = bucc_door_run(runner);
    
    bucc_door_runner_free(runner);
    
    return result;
}
