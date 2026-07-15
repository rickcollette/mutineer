#include "bbs_auth.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

typedef struct
{
  char ip[64];
  char handle[64];
  int attempts;
  time_t window_start;
} LoginAttempt;

#define MAX_LOGIN_ATTEMPTS 256

static LoginAttempt g_attempts[MAX_LOGIN_ATTEMPTS];
static pthread_mutex_t g_attempts_mu = PTHREAD_MUTEX_INITIALIZER;

static int login_window(const BbsConfig *cfg)
{
  return cfg && cfg->login_window_sec > 0 ? cfg->login_window_sec : 120;
}

static int login_max_attempts(const BbsConfig *cfg)
{
  return cfg && cfg->login_max_attempts > 0 ? cfg->login_max_attempts : 5;
}

bool bbs_login_throttled(const BbsConfig *cfg, const char *ip, const char *handle)
{
  time_t now = time(NULL);
  int window = login_window(cfg);
  int max_attempts = login_max_attempts(cfg);
  const char *safe_ip = ip ? ip : "";

  pthread_mutex_lock(&g_attempts_mu);
  for (int i = 0; i < MAX_LOGIN_ATTEMPTS; i++)
  {
    LoginAttempt *a = &g_attempts[i];
    if (a->attempts == 0)
      continue;
    if (difftime(now, a->window_start) > window)
    {
      a->attempts = 0;
      continue;
    }
    if (!strcmp(a->ip, safe_ip) || (handle && handle[0] && !strcmp(a->handle, handle)))
    {
      if (a->attempts >= max_attempts)
      {
        pthread_mutex_unlock(&g_attempts_mu);
        return true;
      }
    }
  }
  pthread_mutex_unlock(&g_attempts_mu);
  return false;
}

void bbs_login_record(const BbsConfig *cfg, const char *ip, const char *handle, bool success)
{
  if (success)
    return;

  time_t now = time(NULL);
  int window = login_window(cfg);
  const char *safe_ip = ip ? ip : "";
  const char *safe_handle = handle ? handle : "";

  pthread_mutex_lock(&g_attempts_mu);
  int slot = -1;
  for (int i = 0; i < MAX_LOGIN_ATTEMPTS; i++)
  {
    if (g_attempts[i].attempts == 0)
    {
      if (slot < 0)
        slot = i;
      continue;
    }
    if (!strcmp(g_attempts[i].ip, safe_ip) || (safe_handle[0] && !strcmp(g_attempts[i].handle, safe_handle)))
    {
      if (difftime(now, g_attempts[i].window_start) > window)
      {
        g_attempts[i].attempts = 0;
        slot = i;
      }
      else
      {
        g_attempts[i].attempts++;
        pthread_mutex_unlock(&g_attempts_mu);
        return;
      }
    }
  }
  if (slot >= 0)
  {
    LoginAttempt *a = &g_attempts[slot];
    snprintf(a->ip, sizeof(a->ip), "%s", safe_ip);
    snprintf(a->handle, sizeof(a->handle), "%s", safe_handle);
    a->attempts = 1;
    a->window_start = now;
  }
  pthread_mutex_unlock(&g_attempts_mu);
}
