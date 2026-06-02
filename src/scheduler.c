#define _XOPEN_SOURCE 700
#include "bbs_scheduler.h"
#include "bbs_session.h"
#include "bbs_log.h"
#include <pthread.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <strings.h>

extern volatile sig_atomic_t g_stop;

typedef struct {
  BbsConfig cfg;
  BbsDb* db;
} SchedCtx;

static int parse_day_of_week(const char* day) {
  if (!day) return -1;
  if (strncasecmp(day, "Sun", 3) == 0) return 0;
  if (strncasecmp(day, "Mon", 3) == 0) return 1;
  if (strncasecmp(day, "Tue", 3) == 0) return 2;
  if (strncasecmp(day, "Wed", 3) == 0) return 3;
  if (strncasecmp(day, "Thu", 3) == 0) return 4;
  if (strncasecmp(day, "Fri", 3) == 0) return 5;
  if (strncasecmp(day, "Sat", 3) == 0) return 6;
  return -1;
}

static time_t parse_schedule(const char* sched, time_t now) {
  if (!sched || !sched[0]) return now + 3600;
  
  /* daily@HH:MM - run every day at specified time */
  if (strncmp(sched, "daily@", 6) == 0) {
    int hh = atoi(sched + 6);
    const char* colon = strchr(sched + 6, ':');
    int mm = colon ? atoi(colon + 1) : 0;
    struct tm tm; localtime_r(&now, &tm);
    tm.tm_hour = hh; tm.tm_min = mm; tm.tm_sec = 0;
    time_t target = mktime(&tm);
    if (difftime(target, now) < 0) target += 86400;
    return target;
  }
  
  /* weekly:Day@HH:MM - run on specific day of week */
  if (strncmp(sched, "weekly:", 7) == 0) {
    const char* at = strchr(sched + 7, '@');
    if (!at) return now + 3600;
    char day_str[8] = {0};
    size_t len = at - (sched + 7);
    if (len > 7) len = 7;
    memcpy(day_str, sched + 7, len);
    int target_dow = parse_day_of_week(day_str);
    if (target_dow < 0) return now + 3600;
    
    int hh = atoi(at + 1);
    const char* colon = strchr(at + 1, ':');
    int mm = colon ? atoi(colon + 1) : 0;
    
    struct tm tm; localtime_r(&now, &tm);
    int current_dow = tm.tm_wday;
    int days_until = (target_dow - current_dow + 7) % 7;
    if (days_until == 0) {
      tm.tm_hour = hh; tm.tm_min = mm; tm.tm_sec = 0;
      time_t target = mktime(&tm);
      if (difftime(target, now) <= 0) days_until = 7;
    }
    tm.tm_hour = hh; tm.tm_min = mm; tm.tm_sec = 0;
    tm.tm_mday += days_until;
    return mktime(&tm);
  }
  
  /* monthly:DD@HH:MM - run on specific day of month */
  if (strncmp(sched, "monthly:", 8) == 0) {
    const char* at = strchr(sched + 8, '@');
    if (!at) return now + 3600;
    int target_day = atoi(sched + 8);
    if (target_day < 1 || target_day > 31) return now + 3600;
    
    int hh = atoi(at + 1);
    const char* colon = strchr(at + 1, ':');
    int mm = colon ? atoi(colon + 1) : 0;
    
    struct tm tm; localtime_r(&now, &tm);
    tm.tm_mday = target_day;
    tm.tm_hour = hh; tm.tm_min = mm; tm.tm_sec = 0;
    time_t target = mktime(&tm);
    if (difftime(target, now) <= 0) {
      tm.tm_mon += 1;
      target = mktime(&tm);
    }
    return target;
  }
  
  /* every:N - run every N seconds */
  if (strncmp(sched, "every:", 6) == 0) {
    int sec = atoi(sched + 6);
    if (sec <= 0) sec = 300;
    return now + sec;
  }
  
  /* fallback: once an hour */
  return now + 3600;
}

static void run_event(BbsDb* db, int id, const char* command, const char* name) {
  log_info("scheduler: running event %s", name ? name : "");
  if (command && command[0]) {
    int rc = system(command);
    if (rc != 0) log_warn("event %s exited with %d", name ? name : "", rc);
  }
  /* update last_run/next_run */
  char sql[256];
  snprintf(sql, sizeof(sql), "UPDATE events SET last_run=datetime('now'), next_run=NULL WHERE id=%d", id);
  db_exec(db, sql);
}

static void* sched_thread(void* arg) {
  SchedCtx* ctx = (SchedCtx*)arg;
  while (!g_stop) {
    time_t now = time(NULL);
    DbEvent evs[32];
    int count = db_events_list(ctx->db, evs, 32);
    for (int i = 0; i < count; i++) {
      /* Skip disabled events and non-scheduled event types */
      if (!evs[i].enabled) continue;
      if (strcmp(evs[i].event_type, "scheduled") != 0) continue;
      
      time_t next_ts = 0;
      if (evs[i].next_run[0]) {
        struct tm tm = {0};
        strptime(evs[i].next_run, "%Y-%m-%d %H:%M:%S", &tm);
        next_ts = mktime(&tm);
      }
      if (next_ts == 0) {
        next_ts = parse_schedule(evs[i].schedule, now);
        char buf[64]; struct tm tm; localtime_r(&next_ts, &tm);
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
        db_event_update_next(ctx->db, evs[i].id, buf);
      }
      
      /* Check for pre-event warning */
      if (evs[i].warning_min > 0) {
        double diff = difftime(next_ts, now);
        if (diff > 0 && diff <= evs[i].warning_min * 60) {
          char warn_msg[256];
          snprintf(warn_msg, sizeof(warn_msg), 
                   "\r\n*** Event '%s' will run in %d minute(s) ***\r\n",
                   evs[i].name, (int)(diff / 60));
          online_broadcast(warn_msg);
        }
      }
      
      if (difftime(next_ts, now) <= 0) {
        run_event(ctx->db, evs[i].id, evs[i].command, evs[i].name);
      }
    }
    sleep(ctx->cfg.scheduler_tick_sec > 0 ? ctx->cfg.scheduler_tick_sec : 30);
  }
  free(ctx);
  return NULL;
}

void scheduler_start(const BbsConfig* cfg, BbsDb* db) {
  if (!cfg || !db || !cfg->scheduler_enabled) return;
  SchedCtx* ctx = (SchedCtx*)calloc(1, sizeof(SchedCtx));
  ctx->cfg = *cfg;
  ctx->db = db;
  pthread_t th;
  if (pthread_create(&th, NULL, sched_thread, ctx) == 0) {
    pthread_detach(th);
  } else {
    free(ctx);
  }
}

void scheduler_run_logon_events(BbsDb* db, int user_id, const char* handle) {
  if (!db) return;
  DbEvent evs[32];
  int count = db_events_list(db, evs, 32);
  for (int i = 0; i < count; i++) {
    if (!evs[i].enabled) continue;
    if (strcmp(evs[i].event_type, "logon") != 0) continue;
    
    log_info("scheduler: running logon event %s for user %s", evs[i].name, handle ? handle : "");
    if (evs[i].command[0]) {
      /* Set environment variables for the command */
      char user_id_str[16], handle_str[64];
      snprintf(user_id_str, sizeof(user_id_str), "%d", user_id);
      snprintf(handle_str, sizeof(handle_str), "%s", handle ? handle : "");
      setenv("BBS_USER_ID", user_id_str, 1);
      setenv("BBS_USER_HANDLE", handle_str, 1);
      
      int rc = system(evs[i].command);
      if (rc != 0) log_warn("logon event %s exited with %d", evs[i].name, rc);
      
      unsetenv("BBS_USER_ID");
      unsetenv("BBS_USER_HANDLE");
    }
  }
}
