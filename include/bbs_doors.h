#pragma once
#include "bbs_session.h"
#include "bbs_db.h"
#include <stddef.h>
#include <stdbool.h>

/* Parsed DOSBox door manifest (from JSON file). */
typedef struct DosboxManifest {
  char runner[32];          /* "dosbox" */
  char name[64];
  char master_dir[512];     /* absolute path to master door files */
  char startup[128];        /* DOS executable within game/, e.g. "ALADDIN.EXE" */
  char dropfile[64];        /* primary dropfile type, e.g. "DOOR.SYS" */
  char dropfile_dest[128];  /* destination relative to runtime root, e.g. "game" */
  char machine[32];         /* dosbox machine type, e.g. "svga_s3" */
  int  memsize;
  char core[32];            /* auto, simple, normal, dynamic */
  char cycles[32];          /* auto or a number */
  int  serial_telnet;       /* 1=include telnet:1 in nullmodem config */
  int  usedtr;              /* 1=include usedtr:1 */
  int  timeout_sec;         /* 0=use global default */
  char copy_mode[16];       /* "copy" (default) */
  int  cleanup_on_exit;
} DosboxManifest;

/* Parse a DOSBox door JSON manifest from a file path. */
bool dosbox_manifest_parse(const char *path, DosboxManifest *out,
                           char *errbuf, size_t errcap);

/* Validate manifest required fields and path safety. */
bool dosbox_manifest_validate(const DosboxManifest *m,
                              char *errbuf, size_t errcap);

/* Create per-launch runtime tree and copy master_dir into game/.
   Writes absolute runtime root into runtime_root_out. */
bool dosbox_prepare_runtime(const DosboxManifest *m,
                            const char *runtime_base,
                            int node_num, const char *launch_id,
                            char *runtime_root_out, size_t root_cap,
                            char *errbuf, size_t errcap);

/* Generate per-run DOSBox config at conf_path. */
bool dosbox_build_conf(const DosboxManifest *m, const char *game_dir,
                       const char *conf_path,
                       char *errbuf, size_t errcap);

/* Remove a runtime tree directory. */
void dosbox_cleanup_runtime(const char *runtime_root);

/* Generate dropfiles and launch doors/protocols. */
bool door_launch(Session* s, const DbDoor* door);
bool protocol_launch(Session* s, const DbProtocol* proto, const char* filepath, const char* direction);

/* Periodically removes offline, aged native/DOS door launch trees. */
int door_janitor_run_once(const BbsConfig* cfg, BbsDb* db);
void door_janitor_start(const BbsConfig* cfg, BbsDb* db);
void door_janitor_stop(void);
