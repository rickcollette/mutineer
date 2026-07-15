#include "bbs_config.h"
#include "bbs_util.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>

static char cfg_status_char(const char *v, char fallback)
{
  return (v && v[0]) ? (char)toupper((unsigned char)v[0]) : fallback;
}

static int cfg_bool(const char *v)
{
  if (!v)
    return 0;
  if (!strcasecmp(v, "true") || !strcasecmp(v, "yes") || !strcasecmp(v, "on"))
    return 1;
  if (!strcasecmp(v, "false") || !strcasecmp(v, "no") || !strcasecmp(v, "off"))
    return 0;
  return atoi(v) != 0;
}

static void cfg_defaults(BbsConfig *c)
{
  memset(c, 0, sizeof(*c));
  snprintf(c->bind, sizeof(c->bind), "0.0.0.0");
  c->port = 2929;
  snprintf(c->db_path, sizeof(c->db_path), "data/mutineer.db");
  snprintf(c->menu_main, sizeof(c->menu_main), "menus/main.mnu");
  c->idle_timeout_sec = 600;
  snprintf(c->motd, sizeof(c->motd), "art/motd.ans");
  snprintf(c->bbs_name, sizeof(c->bbs_name), "Mutineer BBS");
  snprintf(c->sysop_name, sizeof(c->sysop_name), "Sysop");
  snprintf(c->data_path, sizeof(c->data_path), "data");
  snprintf(c->logs_path, sizeof(c->logs_path), "logs/mutineer.log");
  snprintf(c->art_path, sizeof(c->art_path), "art");
  c->session_time_limit_min = 60; /* per-call default */
  c->wfc_enabled = 0;
  c->wfc_refresh_ms = 1000;
  c->wfc_blank_sec = 300; /* 5 minutes default */
  c->wfc_node_num = 1;
  c->wfc_fg_color = 11; /* cyan */
  c->wfc_bg_color = 0;  /* black */
  c->wfc_status_idle_char = 'I';
  c->wfc_status_logging_char = 'L';
  c->wfc_status_online_char = 'A';
  c->wfc_status_chat_char = 'S';
  c->wfc_shell_enabled = 0;
  c->wfc_shell_command[0] = '\0';
  c->console_enabled = 1;
  snprintf(c->console_bind, sizeof(c->console_bind), "127.0.0.1");
  c->console_port = 2931;
  c->console_idle_timeout_sec = 600;
  c->scheduler_enabled = 1;
  c->scheduler_tick_sec = 30;
  c->login_window_sec = 120;
  c->login_max_attempts = 5;
  c->password_upgrade = 1;
  c->default_credits = 5000;
  c->default_file_points = 0;
  snprintf(c->doors_path, sizeof(c->doors_path), "doors");
  snprintf(c->dropfile_path, sizeof(c->dropfile_path), "data/dropfiles");
  snprintf(c->protocol_path, sizeof(c->protocol_path), "conf/protocols.conf");
  c->protocol_timeout_sec = 300;
  c->plugins_enabled = 1;
  snprintf(c->plugins_dir, sizeof(c->plugins_dir), "plugins");
  c->allow_multi_login = 0; /* block duplicate logins by default */
  c->guest_enabled = 0;
  snprintf(c->guest_handle, sizeof(c->guest_handle), "GUEST");
  c->guest_level_id = 1;
  c->welcome_letter_enabled = 1;
  snprintf(c->welcome_letter_file, sizeof(c->welcome_letter_file), "art/welcome.txt");
  snprintf(c->welcome_letter_from, sizeof(c->welcome_letter_from), "Sysop");
  c->password_expire_days = 0; /* disabled by default */
  snprintf(c->dosbox_path, sizeof(c->dosbox_path), "dosbox");
  snprintf(c->door_runtime_path, sizeof(c->door_runtime_path), "data/door_runtime");
  snprintf(c->door_copy_mode, sizeof(c->door_copy_mode), "copy");
  c->door_default_timeout_sec = 300; /* 5 minutes */
  c->door_cleanup_on_exit = 1;
  c->door_keep_failed_runs = 0;
  snprintf(c->door_session_hmac_secret, sizeof(c->door_session_hmac_secret),
           "mutineer-dev-door-secret");
  c->max_page_sysop = 3;
  c->max_calls_per_day = 0; /* unlimited by default */
}

bool cfg_load(const char *path, BbsConfig *out)
{
  cfg_defaults(out);
  snprintf(out->source_path, sizeof(out->source_path), "%s", path ? path : "");

  FILE *f = fopen(path, "rb");
  if (!f)
    return false;

  char line[512];
  while (fgets(line, sizeof(line), f))
  {
    str_trim(line);
    if (line[0] == '\0' || line[0] == '#')
      continue;

    char *eq = strchr(line, '=');
    if (!eq)
      continue;
    *eq = '\0';
    char *k = line;
    char *v = eq + 1;
    str_trim(k);
    str_trim(v);

    if (!strcmp(k, "bind"))
    {
      snprintf(out->bind, sizeof(out->bind), "%s", v);
    }
    else if (!strcmp(k, "port"))
    {
      out->port = atoi(v);
    }
    else if (!strcmp(k, "db_path"))
    {
      snprintf(out->db_path, sizeof(out->db_path), "%s", v);
    }
    else if (!strcmp(k, "menu_main"))
    {
      snprintf(out->menu_main, sizeof(out->menu_main), "%s", v);
    }
    else if (!strcmp(k, "idle_timeout_sec"))
    {
      out->idle_timeout_sec = atoi(v);
    }
    else if (!strcmp(k, "motd"))
    {
      snprintf(out->motd, sizeof(out->motd), "%s", v);
    }
    else if (!strcmp(k, "bbs_name"))
    {
      snprintf(out->bbs_name, sizeof(out->bbs_name), "%s", v);
    }
    else if (!strcmp(k, "sysop_name"))
    {
      snprintf(out->sysop_name, sizeof(out->sysop_name), "%s", v);
    }
    else if (!strcmp(k, "data_path"))
    {
      snprintf(out->data_path, sizeof(out->data_path), "%s", v);
    }
    else if (!strcmp(k, "logs_path"))
    {
      snprintf(out->logs_path, sizeof(out->logs_path), "%s", v);
    }
    else if (!strcmp(k, "art_path"))
    {
      snprintf(out->art_path, sizeof(out->art_path), "%s", v);
    }
    else if (!strcmp(k, "session_time_limit_min"))
    {
      out->session_time_limit_min = atoi(v);
    }
    else if (!strcmp(k, "wfc_enabled"))
    {
      out->wfc_enabled = cfg_bool(v);
    }
    else if (!strcmp(k, "wfc_refresh_ms"))
    {
      out->wfc_refresh_ms = atoi(v);
    }
    else if (!strcmp(k, "wfc_blank_sec"))
    {
      out->wfc_blank_sec = atoi(v);
    }
    else if (!strcmp(k, "wfc_node_num"))
    {
      out->wfc_node_num = atoi(v);
    }
    else if (!strcmp(k, "wfc_fg_color"))
    {
      out->wfc_fg_color = atoi(v);
    }
    else if (!strcmp(k, "wfc_bg_color"))
    {
      out->wfc_bg_color = atoi(v);
    }
    else if (!strcmp(k, "wfc_status_idle_char"))
    {
      out->wfc_status_idle_char = cfg_status_char(v, out->wfc_status_idle_char);
    }
    else if (!strcmp(k, "wfc_status_logging_char"))
    {
      out->wfc_status_logging_char = cfg_status_char(v, out->wfc_status_logging_char);
    }
    else if (!strcmp(k, "wfc_status_online_char"))
    {
      out->wfc_status_online_char = cfg_status_char(v, out->wfc_status_online_char);
    }
    else if (!strcmp(k, "wfc_status_chat_char"))
    {
      out->wfc_status_chat_char = cfg_status_char(v, out->wfc_status_chat_char);
    }
    else if (!strcmp(k, "wfc_shell_enabled"))
    {
      out->wfc_shell_enabled = cfg_bool(v);
    }
    else if (!strcmp(k, "wfc_shell_command"))
    {
      snprintf(out->wfc_shell_command, sizeof(out->wfc_shell_command), "%s", v);
    }
    else if (!strcmp(k, "console_enabled"))
    {
      out->console_enabled = cfg_bool(v);
    }
    else if (!strcmp(k, "console_bind"))
    {
      snprintf(out->console_bind, sizeof(out->console_bind), "%s", v);
    }
    else if (!strcmp(k, "console_port"))
    {
      out->console_port = atoi(v);
    }
    else if (!strcmp(k, "console_idle_timeout_sec"))
    {
      out->console_idle_timeout_sec = atoi(v);
    }
    else if (!strcmp(k, "scheduler_enabled"))
    {
      out->scheduler_enabled = cfg_bool(v);
    }
    else if (!strcmp(k, "scheduler_tick_sec"))
    {
      out->scheduler_tick_sec = atoi(v);
    }
    else if (!strcmp(k, "login_window_sec"))
    {
      out->login_window_sec = atoi(v);
    }
    else if (!strcmp(k, "login_max_attempts"))
    {
      out->login_max_attempts = atoi(v);
    }
    else if (!strcmp(k, "password_upgrade"))
    {
      out->password_upgrade = cfg_bool(v);
    }
    else if (!strcmp(k, "default_credits"))
    {
      out->default_credits = atoi(v);
    }
    else if (!strcmp(k, "default_file_points"))
    {
      out->default_file_points = atoi(v);
    }
    else if (!strcmp(k, "doors_path"))
    {
      snprintf(out->doors_path, sizeof(out->doors_path), "%s", v);
    }
    else if (!strcmp(k, "dropfile_path"))
    {
      snprintf(out->dropfile_path, sizeof(out->dropfile_path), "%s", v);
    }
    else if (!strcmp(k, "protocol_path"))
    {
      snprintf(out->protocol_path, sizeof(out->protocol_path), "%s", v);
    }
    else if (!strcmp(k, "protocol_timeout_sec"))
    {
      out->protocol_timeout_sec = atoi(v);
    }
    else if (!strcmp(k, "plugins_enabled"))
    {
      out->plugins_enabled = cfg_bool(v);
    }
    else if (!strcmp(k, "plugins_dir"))
    {
      snprintf(out->plugins_dir, sizeof(out->plugins_dir), "%s", v);
    }
    else if (!strcmp(k, "plugins_allowlist"))
    {
      snprintf(out->plugins_allowlist, sizeof(out->plugins_allowlist), "%s", v);
    }
    else if (!strcmp(k, "plugins_denylist"))
    {
      snprintf(out->plugins_denylist, sizeof(out->plugins_denylist), "%s", v);
    }
    else if (!strcmp(k, "allow_multi_login"))
    {
      out->allow_multi_login = cfg_bool(v);
    }
    else if (!strcmp(k, "guest_enabled"))
    {
      out->guest_enabled = cfg_bool(v);
    }
    else if (!strcmp(k, "guest_handle"))
    {
      snprintf(out->guest_handle, sizeof(out->guest_handle), "%s", v);
    }
    else if (!strcmp(k, "guest_level_id"))
    {
      out->guest_level_id = atoi(v);
    }
    else if (!strcmp(k, "welcome_letter_enabled"))
    {
      out->welcome_letter_enabled = cfg_bool(v);
    }
    else if (!strcmp(k, "welcome_letter_file"))
    {
      snprintf(out->welcome_letter_file, sizeof(out->welcome_letter_file), "%s", v);
    }
    else if (!strcmp(k, "welcome_letter_from"))
    {
      snprintf(out->welcome_letter_from, sizeof(out->welcome_letter_from), "%s", v);
    }
    else if (!strcmp(k, "password_expire_days"))
    {
      out->password_expire_days = atoi(v);
    }
    else if (!strcmp(k, "dosbox_path"))
    {
      snprintf(out->dosbox_path, sizeof(out->dosbox_path), "%s", v);
    }
    else if (!strcmp(k, "door_runtime_path"))
    {
      snprintf(out->door_runtime_path, sizeof(out->door_runtime_path), "%s", v);
    }
    else if (!strcmp(k, "door_copy_mode"))
    {
      snprintf(out->door_copy_mode, sizeof(out->door_copy_mode), "%s", v);
    }
    else if (!strcmp(k, "door_default_timeout_sec"))
    {
      out->door_default_timeout_sec = atoi(v);
    }
    else if (!strcmp(k, "door_cleanup_on_exit"))
    {
      out->door_cleanup_on_exit = cfg_bool(v);
    }
    else if (!strcmp(k, "door_keep_failed_runs"))
    {
      out->door_keep_failed_runs = cfg_bool(v);
    }
    else if (!strcmp(k, "door_session_hmac_secret"))
    {
      snprintf(out->door_session_hmac_secret, sizeof(out->door_session_hmac_secret), "%s", v);
    }
    else if (!strcmp(k, "max_page_sysop"))
    {
      out->max_page_sysop = atoi(v);
    }
    else if (!strcmp(k, "max_calls_per_day"))
    {
      out->max_calls_per_day = atoi(v);
    }
    else if (!strcmp(k, "chat_log_path"))
    {
      snprintf(out->chat_log_path, sizeof(out->chat_log_path), "%s", v);
    }
  }

  fclose(f);
  return true;
}

bool cfg_save(const char *path, const BbsConfig *c)
{
  if (!path || !path[0] || !c)
    return false;

  char tmp[512], bak[512];
  snprintf(tmp, sizeof(tmp), "%s.tmp", path);
  snprintf(bak, sizeof(bak), "%s.bak", path);

  FILE *f = fopen(tmp, "w");
  if (!f)
    return false;

#define WSTR(k, v) fprintf(f, "%s=%s\n", (k), (v))
#define WINT(k, v) fprintf(f, "%s=%d\n", (k), (int)(v))
#define WCHR(k, v) fprintf(f, "%s=%c\n", (k), (v))
  WSTR("bind", c->bind);
  WINT("port", c->port);
  WSTR("db_path", c->db_path);
  WSTR("menu_main", c->menu_main);
  WINT("idle_timeout_sec", c->idle_timeout_sec);
  WSTR("motd", c->motd);
  WSTR("bbs_name", c->bbs_name);
  WSTR("sysop_name", c->sysop_name);
  WSTR("data_path", c->data_path);
  WSTR("logs_path", c->logs_path);
  WSTR("art_path", c->art_path);
  WINT("session_time_limit_min", c->session_time_limit_min);
  WINT("wfc_enabled", c->wfc_enabled);
  WINT("wfc_refresh_ms", c->wfc_refresh_ms);
  WINT("wfc_blank_sec", c->wfc_blank_sec);
  WINT("wfc_node_num", c->wfc_node_num);
  WINT("wfc_fg_color", c->wfc_fg_color);
  WINT("wfc_bg_color", c->wfc_bg_color);
  WCHR("wfc_status_idle_char", c->wfc_status_idle_char);
  WCHR("wfc_status_logging_char", c->wfc_status_logging_char);
  WCHR("wfc_status_online_char", c->wfc_status_online_char);
  WCHR("wfc_status_chat_char", c->wfc_status_chat_char);
  WINT("wfc_shell_enabled", c->wfc_shell_enabled);
  WSTR("wfc_shell_command", c->wfc_shell_command);
  WINT("console_enabled", c->console_enabled);
  WSTR("console_bind", c->console_bind);
  WINT("console_port", c->console_port);
  WINT("console_idle_timeout_sec", c->console_idle_timeout_sec);
  WINT("scheduler_enabled", c->scheduler_enabled);
  WINT("scheduler_tick_sec", c->scheduler_tick_sec);
  WINT("login_window_sec", c->login_window_sec);
  WINT("login_max_attempts", c->login_max_attempts);
  WINT("password_upgrade", c->password_upgrade);
  WINT("default_credits", c->default_credits);
  WINT("default_file_points", c->default_file_points);
  WSTR("doors_path", c->doors_path);
  WSTR("dropfile_path", c->dropfile_path);
  WSTR("protocol_path", c->protocol_path);
  WINT("protocol_timeout_sec", c->protocol_timeout_sec);
  WINT("plugins_enabled", c->plugins_enabled);
  WSTR("plugins_dir", c->plugins_dir);
  WSTR("plugins_allowlist", c->plugins_allowlist);
  WSTR("plugins_denylist", c->plugins_denylist);
  WINT("allow_multi_login", c->allow_multi_login);
  WINT("guest_enabled", c->guest_enabled);
  WSTR("guest_handle", c->guest_handle);
  WINT("guest_level_id", c->guest_level_id);
  WINT("welcome_letter_enabled", c->welcome_letter_enabled);
  WSTR("welcome_letter_file", c->welcome_letter_file);
  WSTR("welcome_letter_from", c->welcome_letter_from);
  WINT("password_expire_days", c->password_expire_days);
  WSTR("dosbox_path", c->dosbox_path);
  WSTR("door_runtime_path", c->door_runtime_path);
  WSTR("door_copy_mode", c->door_copy_mode);
  WINT("door_default_timeout_sec", c->door_default_timeout_sec);
  WINT("door_cleanup_on_exit", c->door_cleanup_on_exit);
  WINT("door_keep_failed_runs", c->door_keep_failed_runs);
  WSTR("door_session_hmac_secret", c->door_session_hmac_secret);
  WINT("max_page_sysop", c->max_page_sysop);
  WINT("max_calls_per_day", c->max_calls_per_day);
  WSTR("chat_log_path", c->chat_log_path);
#undef WSTR
#undef WINT
#undef WCHR

  if (fclose(f) != 0) {
    unlink(tmp);
    return false;
  }
  unlink(bak);
  if (access(path, F_OK) == 0 && rename(path, bak) != 0) {
    unlink(tmp);
    return false;
  }
  if (rename(tmp, path) != 0) {
    if (access(bak, F_OK) == 0) rename(bak, path);
    unlink(tmp);
    return false;
  }
  return true;
}
