#pragma once
#include "bbs_config.h"
#include "bbs_db.h"
#include <stdint.h>

/* WFC statistics for dashboard display */
typedef struct WfcStats {
  /* Today's stats */
  int calls_today;
  int posts_today;
  int emails_today;
  int newusers_today;
  int feedback_today;
  int uploads_today;
  int downloads_today;
  int64_t ul_kb_today;
  int64_t dl_kb_today;
  int minutes_today;
  
  /* System totals */
  int total_calls;
  int total_posts;
  int total_uploads;
  int total_downloads;
  int days_online;
  int total_users;
  
  /* Other info */
  int node_num;
  int errors;
  int mail_waiting;
  int64_t disk_free_kb;
} WfcStats;

/* WFC status message */
typedef enum WfcStatus {
  WFC_STATUS_WAITING,
  WFC_STATUS_INIT_MODEM,
  WFC_STATUS_OFF_HOOK,
  WFC_STATUS_ANSWERING,
  WFC_STATUS_LOCAL_LOGON,
  WFC_STATUS_EVENT_PENDING,
  WFC_STATUS_SHUTDOWN
} WfcStatus;

/* Start the local WFC (Waiting for Call) console UI in a detached thread. */
void wfc_start(const BbsConfig* cfg, BbsDb* db);

/* Stop the WFC thread gracefully */
void wfc_stop(void);

/* Set WFC status message */
void wfc_set_status(WfcStatus status, const char* extra);

/* Force a screen refresh */
void wfc_refresh(void);

/* Check if WFC is running */
int wfc_is_running(void);
