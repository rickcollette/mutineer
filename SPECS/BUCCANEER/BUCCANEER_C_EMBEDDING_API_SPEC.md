# Buccaneer C Embedding API Specification

Version: 0.3-draft  
Status: Proposed

This document defines the host bridge between a C-based BBS and the Buccaneer runtime.

## 1. Core principles

1. The VM never reaches directly into BBS structs.
2. The BBS passes opaque context pointers back to itself through host callbacks.
3. Host function resolution is table-driven and versioned.
4. The VM returns structured results; the host owns scheduling.
5. The host enforces sensitive runtime policy such as throttling and filesystem root restrictions.

## 2. Public object model

Recommended opaque types:

```c
typedef struct bucc_module bucc_module_t;
typedef struct bucc_vm bucc_vm_t;
typedef struct bucc_host bucc_host_t;
typedef struct bucc_diag bucc_diag_t;
typedef struct bucc_value bucc_value_t;
typedef struct bucc_run_result bucc_run_result_t;
```

## 3. Versioning

```c
typedef struct {
    uint16_t runtime_abi_major;
    uint16_t runtime_abi_minor;
    uint16_t bytecode_major;
    uint16_t bytecode_minor;
    uint16_t host_api_major;
    uint16_t host_api_minor;
} bucc_runtime_version_t;

bucc_runtime_version_t bucc_runtime_version(void);
```

## 4. Value API

```c
typedef enum {
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
```

## 5. Module loading

```c
typedef struct {
    int require_signature;
    int enable_debug_map;
    int verify_capabilities;
    int reserved;
} bucc_load_options_t;

bucc_module_t *bucc_module_load_file(
    const char *path,
    const bucc_load_options_t *opts,
    bucc_diag_t *diag
);

void bucc_module_retain(bucc_module_t *m);
void bucc_module_release(bucc_module_t *m);
```

## 6. Host import resolution

```c
typedef struct {
    uint32_t import_id;
    const char *ns_name;
    const char *fn_name;
    const char *sig_id;
    const char *required_capability;
} bucc_import_desc_t;

typedef int (*bucc_resolve_import_fn)(
    void *host_ctx,
    const bucc_import_desc_t *desc,
    uint32_t *resolved_host_fn_id,
    bucc_error_t *err
);
```

## 7. Host call interface

```c
typedef int (*bucc_host_call_fn)(
    void *host_ctx,
    void *bbs_ctx,
    void *session_ctx,
    uint32_t resolved_host_fn_id,
    const bucc_value_t *args,
    uint32_t arg_count,
    bucc_value_t *ret_out,
    bucc_error_t *err_out
);
```

## 8. Policy and capability hooks

```c
typedef struct {
    const char *module_program;
    const char *module_version;
    const char *requested_program_id;
    const char *caller_name;
    int caller_security_level;
} bucc_policy_query_t;

typedef int (*bucc_policy_check_fn)(
    void *host_ctx,
    const bucc_policy_query_t *query,
    const bucc_module_t *module,
    bucc_error_t *err_out
);
```

## 9. VM creation

```c
typedef struct {
    uint32_t max_ticks_per_run;
    uint32_t max_stack_depth;
    uint32_t max_heap_bytes;
    uint32_t max_string_bytes;
    uint32_t max_timeslice_ms;
    uint32_t max_host_calls_per_slice;
    uint32_t max_users_lookups_per_window;
} bucc_limits_t;

typedef struct {
    void *host_ctx;
    void *bbs_ctx;
    void *session_ctx;
    const bucc_host_t *host;
    bucc_limits_t limits;
    const char *program_id;
} bucc_vm_config_t;

bucc_vm_t *bucc_vm_create(
    const bucc_module_t *module,
    const bucc_vm_config_t *cfg,
    bucc_diag_t *diag
);

void bucc_vm_destroy(bucc_vm_t *vm);
```

## 10. Run and dispatch API

```c
typedef enum {
    BUCC_RUN_OK = 0,
    BUCC_RUN_EXIT,
    BUCC_RUN_YIELD,
    BUCC_RUN_CHAIN,
    BUCC_RUN_ERROR,
    BUCC_RUN_DISCONNECT
} bucc_run_status_t;

typedef struct {
    bucc_run_status_t status;
    int exit_code;
    const char *message;
    const char *chain_target;
    bucc_value_t chain_args;
} bucc_run_result_t;

bucc_run_result_t bucc_vm_run_main(bucc_vm_t *vm);
bucc_run_result_t bucc_vm_dispatch_event(
    bucc_vm_t *vm,
    const char *event_name,
    const bucc_value_t *arg
);
```

## 11. Required host responsibilities

The BBS embedding layer must:

- provide stable import resolution
- enforce capability policy
- enforce filesystem root restrictions
- throttle sensitive user lookups
- map logical datasets to generated physical storage identities
- scope `APP` to session+application
- scope `SHARED` to installed application
- keep module cache immutable
- destroy VM instances promptly on disconnect or fatal error
