#pragma once
#include "bbs_config.h"
#include "bbs_db.h"

/* Start event scheduler thread (nightly maintenance, cron-like). */
void scheduler_start(const BbsConfig* cfg, BbsDb* db);

/* Run logon events for a user (called from session.c after login) */
void scheduler_run_logon_events(BbsDb* db, int user_id, const char* handle);
