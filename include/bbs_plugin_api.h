/*
 * bbs_plugin_api.h - BBS Plugin System Public ABI
 *
 * This header defines the stable ABI contract between the BBS host and plugins.
 * Plugins are shared objects (.so) loaded via dlopen() that can provide
 * interactive "door-like" experiences for BBS users.
 *
 * Version: 1.2
 * ABI Version: 0x00010002
 */

#pragma once
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BBS_PLUGIN_ABI_VERSION_1_0 0x00010000u
#define BBS_PLUGIN_ABI_VERSION_1_1 0x00010001u
#define BBS_PLUGIN_ABI_VERSION 0x00010002u /* v1.2, append-only */
#define BBS_PLUGIN_MAGIC 0x4250534Fu       /* 'BPSO' */

/* Return codes */
typedef enum bbs_rc {
  BBS_OK = 0,
  BBS_EINVAL = -1,
  BBS_EUNSUPPORTED = -2,
  BBS_EIO = -3,
  BBS_EPERM = -4,
  BBS_EINTERNAL = -5,
  BBS_ETIMEOUT = -6
} bbs_rc_t;

/* Opaque host-owned session handle */
typedef struct bbs_session bbs_session_t;

typedef struct bbs_user_ref {
  uint32_t user_id;
  char handle[64];
} bbs_user_ref_t;

typedef enum bbs_user_resolve_result {
  BBS_USER_NOT_FOUND = 0,
  BBS_USER_EXACT = 1,
  BBS_USER_UNIQUE_PREFIX = 2,
  BBS_USER_AMBIGUOUS = 3
} bbs_user_resolve_result_t;

/* Logging levels */
typedef enum bbs_log_level {
  BBS_LOG_DEBUG = 10,
  BBS_LOG_INFO = 20,
  BBS_LOG_WARN = 30,
  BBS_LOG_ERROR = 40
} bbs_log_level_t;

/* Terminal I/O interface (host implements) */
typedef struct bbs_io {
  bbs_rc_t (*write)(bbs_session_t *s, const void *buf, size_t n);
  bbs_rc_t (*printf)(bbs_session_t *s, const char *fmt, ...);
  bbs_rc_t (*readline)(bbs_session_t *s, char *out, size_t out_sz, int echo);
  bbs_rc_t (*cls)(bbs_session_t *s);
} bbs_io_t;

/* Scheduler interface (host implements) */
typedef void (*bbs_task_fn)(void *user);

typedef struct bbs_sched {
  bbs_rc_t (*enqueue)(bbs_task_fn fn, void *user);
  bbs_rc_t (*after_ms)(uint64_t delay_ms, bbs_task_fn fn, void *user,
                       uint64_t *task_id_out);
  bbs_rc_t (*cancel)(uint64_t task_id);
} bbs_sched_t;

/* Host API passed into plugin */
typedef struct bbs_host_api {
  uint32_t abi_version;
  uint32_t size; /* sizeof(bbs_host_api) */
  uint32_t magic;

  void (*log)(bbs_log_level_t lvl, const char *subsystem, const char *msg);

  const bbs_io_t *io;
  const bbs_sched_t *sched;

  /* session info helpers */
  const char *(*session_username)(bbs_session_t *s);
  uint32_t (*session_user_id)(bbs_session_t *s);
  uint32_t (*session_user_flags)(bbs_session_t *s);
  const char *(*session_remote_addr)(bbs_session_t *s);

  /* minimal key/value store (host-defined persistence) */
  bbs_rc_t (*kv_get)(const char *ns, const char *key, char *out, size_t out_sz);
  bbs_rc_t (*kv_set)(const char *ns, const char *key, const char *val);

  /* plugin-private data directory */
  bbs_rc_t (*plugin_data_dir)(const char *plugin_id, char *out, size_t out_sz);

  void *reserved[16]; /* must be NULL */

  /* ABI 1.1 additions. Check size before dereferencing these members. */
  uint32_t (*session_restriction_flags)(bbs_session_t *s);
  int (*session_node_number)(bbs_session_t *s);
  int (*session_security_level)(bbs_session_t *s);
  int (*session_has_ansi)(bbs_session_t *s);
  int (*session_is_alive)(bbs_session_t *s);
  bbs_rc_t (*readline_timed)(bbs_session_t *s, char *out, size_t out_sz,
                             int echo, uint32_t timeout_ms);
  void (*audit)(bbs_session_t *s, const char *action, const char *detail);
  bbs_rc_t (*plugin_config_get)(const char *plugin_id, const char *key,
                                char *out, size_t out_sz);

  /* ABI 1.2 additions. */
  bbs_rc_t (*session_terminal_size)(bbs_session_t *s, uint16_t *cols,
                                    uint16_t *rows);
  bbs_user_resolve_result_t (*user_resolve)(bbs_session_t *s, const char *query,
                                            bbs_user_ref_t *out);
  bbs_rc_t (*user_lookup_id)(bbs_session_t *s, uint32_t user_id,
                             bbs_user_ref_t *out);

} bbs_host_api_t;

/* Plugin capabilities */
typedef enum bbs_plugin_caps {
  BBS_CAP_NONE = 0,
  BBS_CAP_INTERACTIVE = 1u << 0,
  BBS_CAP_COMMANDS = 1u << 1,
  BBS_CAP_BACKGROUND = 1u << 2
} bbs_plugin_caps_t;

/* Event types for plugin notification */
typedef enum bbs_event_type {
  BBS_EVT_LOGIN = 1,
  BBS_EVT_LOGOUT,
  BBS_EVT_TICK_1S,
  BBS_EVT_SHUTDOWN,
  BBS_EVT_FORCED_DISCONNECT
} bbs_event_type_t;

typedef struct bbs_event {
  bbs_event_type_t type;
  bbs_session_t *session; /* may be NULL for global events */
  uint64_t now_ms;
  void *data;
} bbs_event_t;

/* Per-session instance vtable */
typedef struct bbs_plugin_instance_vtbl {
  bbs_rc_t (*on_enter)(void *inst, bbs_session_t *s);
  bbs_rc_t (*run)(void *inst, bbs_session_t *s);
  void (*on_exit)(void *inst, bbs_session_t *s);
  void (*on_event)(void *inst, const bbs_event_t *ev);
  void (*destroy)(void *inst);
  void *reserved[8];
} bbs_plugin_instance_vtbl_t;

/* Command handler function type */
typedef bbs_rc_t (*bbs_cmd_fn)(bbs_session_t *s, const char *args);

/* Command definition for plugin-registered commands */
typedef struct bbs_command_def {
  const char *name;
  const char *help;
  uint32_t required_flags;
  bbs_cmd_fn handler;
} bbs_command_def_t;

/* Global plugin descriptor */
typedef struct bbs_plugin_desc {
  uint32_t abi_version;
  uint32_t size; /* sizeof(bbs_plugin_desc) */
  uint32_t magic;

  const char *id; /* stable unique id: "com.example.chess" */
  const char *name;
  const char *version;
  const char *author;
  const char *description;

  uint32_t caps;

  bbs_rc_t (*init)(const bbs_host_api_t *host);
  void (*shutdown)(void);

  bbs_rc_t (*create_instance)(bbs_session_t *s, void **out_inst,
                              const bbs_plugin_instance_vtbl_t **out_vtbl);

  const bbs_command_def_t *(*commands)(size_t *out_count);
  void (*on_event)(const bbs_event_t *ev);

  void *reserved[16];
} bbs_plugin_desc_t;

/* Required exported function type */
typedef bbs_rc_t (*bbs_plugin_query_fn)(uint32_t host_abi_version,
                                        const bbs_host_api_t *host,
                                        bbs_plugin_desc_t *out_desc);

/*
 * Each plugin MUST export this function.
 * Host calls it to query plugin capabilities and obtain the descriptor.
 */
#ifdef BBS_PLUGIN_IMPL
bbs_rc_t bbs_plugin_query(uint32_t host_abi_version, const bbs_host_api_t *host,
                          bbs_plugin_desc_t *out_desc);
#endif

#ifdef __cplusplus
}
#endif
