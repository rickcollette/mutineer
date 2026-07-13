#define _XOPEN_SOURCE 700
#include "bbs_scheduler.h"
#include "bbs_session.h"
#include "bbs_acs.h"
#include "bbs_log.h"
#include "bbs_process.h"
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
  volatile sig_atomic_t stop;
  int warned_ids[64];
  char warned_next[64][32];
} SchedCtx;

static pthread_t g_sched_thread;
static SchedCtx* g_sched_ctx = NULL;
static pthread_mutex_t g_sched_mu = PTHREAD_MUTEX_INITIALIZER;

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
    char errbuf[256] = {0};
    char** argv = NULL;
    if (!bbs_argv_parse_template(command, NULL, &argv, errbuf, sizeof(errbuf))) {
      log_warn("event %s command rejected: %s", name ? name : "", errbuf);
    } else {
      BbsProcessResult result;
      if (!bbs_exec_argv(argv, name ? name : "event", NULL, -1, -1, -1,
                         0, &result, errbuf, sizeof(errbuf)) && errbuf[0]) {
        log_warn("event %s failed: %s", name ? name : "", errbuf);
      }
      bbs_argv_free(argv);
    }
  }
  db_event_mark_ran(db, id);
}

static bool warning_already_sent(SchedCtx* ctx, const DbEvent* ev) {
  for (size_t i = 0; i < 64; i++) {
    if (ctx->warned_ids[i] == ev->id && strcmp(ctx->warned_next[i], ev->next_run) == 0) return true;
  }
  return false;
}

static void warning_mark_sent(SchedCtx* ctx, const DbEvent* ev) {
  size_t slot = 0;
  for (size_t i = 0; i < 64; i++) {
    if (ctx->warned_ids[i] == 0 || ctx->warned_ids[i] == ev->id) {
      slot = i;
      break;
    }
  }
  ctx->warned_ids[slot] = ev->id;
  snprintf(ctx->warned_next[slot], sizeof(ctx->warned_next[slot]), "%s", ev->next_run);
}

static void* sched_thread(void* arg) {
  SchedCtx* ctx = (SchedCtx*)arg;
  while (!g_stop && !ctx->stop) {
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
        if (diff > 0 && diff <= evs[i].warning_min * 60 && !warning_already_sent(ctx, &evs[i])) {
          char warn_msg[256];
          snprintf(warn_msg, sizeof(warn_msg), 
                   "\r\n*** Event '%s' will run in %d minute(s) ***\r\n",
                   evs[i].name, (int)(diff / 60));
          online_broadcast(warn_msg);
          warning_mark_sent(ctx, &evs[i]);
        }
      }
      
      if (difftime(next_ts, now) <= 0) {
        run_event(ctx->db, evs[i].id, evs[i].command, evs[i].name);
      }
    }
    int ticks = ctx->cfg.scheduler_tick_sec > 0 ? ctx->cfg.scheduler_tick_sec : 30;
    for (int i = 0; i < ticks && !g_stop && !ctx->stop; i++) sleep(1);
  }
  return NULL;
}

void scheduler_start(const BbsConfig* cfg, BbsDb* db) {
  if (!cfg || !db || !cfg->scheduler_enabled) return;
  pthread_mutex_lock(&g_sched_mu);
  if (g_sched_ctx) {
    pthread_mutex_unlock(&g_sched_mu);
    return;
  }
  SchedCtx* ctx = (SchedCtx*)calloc(1, sizeof(SchedCtx));
  ctx->cfg = *cfg;
  ctx->db = db;
  if (pthread_create(&g_sched_thread, NULL, sched_thread, ctx) == 0) {
    g_sched_ctx = ctx;
  } else {
    free(ctx);
  }
  pthread_mutex_unlock(&g_sched_mu);
}

void scheduler_stop(void) {
  pthread_mutex_lock(&g_sched_mu);
  SchedCtx* ctx = g_sched_ctx;
  pthread_t th = g_sched_thread;
  if (ctx) ctx->stop = 1;
  pthread_mutex_unlock(&g_sched_mu);
  if (ctx) pthread_join(th, NULL);
  pthread_mutex_lock(&g_sched_mu);
  if (g_sched_ctx == ctx) {
    free(g_sched_ctx);
    g_sched_ctx = NULL;
    memset(&g_sched_thread, 0, sizeof(g_sched_thread));
  }
  pthread_mutex_unlock(&g_sched_mu);
}

void scheduler_run_logon_events(Session* s) {
  if (!s || !s->db) return;
  DbEvent evs[32];
  int count = db_events_list(s->db, evs, 32);
  for (int i = 0; i < count; i++) {
    if (!evs[i].enabled) continue;
    bool is_logon = strcmp(evs[i].event_type, "logon") == 0;
    bool is_permission = strcmp(evs[i].event_type, "permission") == 0;
    if (!is_logon && !is_permission) continue;
    if (is_permission && !acs_allows(s, evs[i].acs)) continue;
    
    log_info("scheduler: running %s event %s for user %s",
             evs[i].event_type, evs[i].name, s->user.handle);
    if (evs[i].command[0]) {
      char user_id_str[16], handle_str[64];
      snprintf(user_id_str, sizeof(user_id_str), "%d", s->user.id);
      snprintf(handle_str, sizeof(handle_str), "%s", s->user.handle);
      setenv("BBS_USER_ID", user_id_str, 1);
      setenv("BBS_USER_HANDLE", handle_str, 1);

      char errbuf[256] = {0};
      char** argv = NULL;
      if (!bbs_argv_parse_template(evs[i].command, NULL, &argv, errbuf, sizeof(errbuf))) {
        log_warn("%s event %s command rejected: %s", evs[i].event_type, evs[i].name, errbuf);
      } else {
        BbsProcessResult result;
        if (!bbs_exec_argv(argv, evs[i].name, NULL, -1, -1, -1,
                           0, &result, errbuf, sizeof(errbuf)) && errbuf[0]) {
          log_warn("%s event %s failed: %s", evs[i].event_type, evs[i].name, errbuf);
        }
        bbs_argv_free(argv);
      }

      unsetenv("BBS_USER_ID");
      unsetenv("BBS_USER_HANDLE");
    }
  }
}
