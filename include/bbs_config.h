#pragma once
#include <stdbool.h>

typedef struct BbsConfig
{
  char source_path[256];
  char bind[64];
  int port;
  char db_path[256];
  char menu_main[256];
  int idle_timeout_sec;
  char motd[256];
  /* BBS identity */
  char bbs_name[64];
  char sysop_name[64];
  /* new: paths and limits */
  char data_path[256];
  char logs_path[256];
  char art_path[256];
  int session_time_limit_min;
  /* sysop / runtime */
  int wfc_enabled;       /* deprecated; local WFC thread is replaced by mutineer-console */
  int wfc_refresh_ms;    /* mutineer-console refresh interval in ms */
  int wfc_blank_sec;     /* mutineer-console blank timeout (0=disabled) */
  int wfc_node_num;      /* mutineer-console display node number */
  int wfc_fg_color;      /* foreground color (ANSI 0-15) */
  int wfc_bg_color;      /* background color (ANSI 0-7) */
  char wfc_status_idle_char;
  char wfc_status_logging_char;
  char wfc_status_online_char;
  char wfc_status_chat_char;
  int wfc_shell_enabled;
  char wfc_shell_command[256];
  int console_enabled;          /* 1 to run mutineer-console control service */
  char console_bind[64];        /* console control listener bind address */
  int console_port;             /* console control listener port */
  int console_idle_timeout_sec; /* idle timeout for console clients */
  int scheduler_enabled; /* run events thread */
  int scheduler_tick_sec;
  /* auth */
  int login_window_sec;   /* throttle window */
  int login_max_attempts; /* attempts per IP/user per window */
  int password_upgrade;   /* enable argon2/bcrypt migration */
  /* ratios / credits */
  int default_credits;
  int default_file_points;
  /* doors/protocols */
  char doors_path[256];
  char dropfile_path[256];
  char protocol_path[256];
  int protocol_timeout_sec;       /* protocol child timeout, 0=no timeout */
  int plugins_enabled;            /* 0 disables all plugin loading */
  char plugins_dir[256];          /* plugin directory */
  char plugins_allowlist[512];    /* comma-separated plugin ids, empty=all */
  char plugins_denylist[512];     /* comma-separated plugin ids, deny wins */
  /* multi-login */
  int allow_multi_login; /* 0=block duplicate logins, 1=allow */
  /* guest account */
  int guest_enabled;     /* 0=disabled, 1=enabled */
  char guest_handle[64]; /* guest account handle (e.g., "GUEST") */
  int guest_level_id;    /* security level for guests */
  /* welcome letter */
  int welcome_letter_enabled;    /* 0=disabled, 1=enabled */
  char welcome_letter_file[256]; /* path to welcome letter text file */
  char welcome_letter_from[64];  /* sender name (e.g., "Sysop") */
  /* password expiration */
  int password_expire_days; /* 0=disabled, >0=days until password expires */
  /* DOSBox / DOS door runner */
  char dosbox_path[256];          /* path to dosbox binary (default: "dosbox") */
  char door_runtime_path[256];    /* base dir for per-launch runtime trees */
  char door_copy_mode[32];        /* "copy" (default) */
  int  door_default_timeout_sec;  /* 0=no timeout */
  int  door_cleanup_on_exit;      /* 1=remove runtime tree on success */
  int  door_keep_failed_runs;     /* 1=keep runtime tree on failure for debugging */
  int  door_janitor_interval_sec; /* periodic stale door-tree scan; 0 disables */
  int  door_stale_age_sec;        /* minimum offline launch age before removal */
  char door_session_hmac_secret[128]; /* shared secret for signed native-door sessions */
  /* chat */
  int  max_page_sysop;            /* max sysop page attempts per session (0=unlimited) */
  int  max_calls_per_day;         /* max logins per user per calendar day (0=unlimited) */
  char chat_log_path[256];        /* directory for per-session chat logs (empty=disabled) */
} BbsConfig;

bool cfg_load(const char *path, BbsConfig *out);
bool cfg_save(const char *path, const BbsConfig *cfg);
