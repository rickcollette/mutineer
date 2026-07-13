#include "bbs_config.h"
#include "bbs_util.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

static char cfg_status_char(const char *v, char fallback)
{
  return (v && v[0]) ? (char)toupper((unsigned char)v[0]) : fallback;
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
  c->wfc_enabled = 1;
  c->wfc_refresh_ms = 1000;
  c->wfc_blank_sec = 300; /* 5 minutes default */
  c->wfc_node_num = 1;
  c->wfc_fg_color = 11; /* cyan */
  c->wfc_bg_color = 0;  /* black */
  c->wfc_status_idle_char = 'I';
  c->wfc_status_logging_char = 'L';
  c->wfc_status_online_char = 'A';
  c->wfc_status_chat_char = 'S';
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
  c->max_page_sysop = 3;
  c->max_calls_per_day = 0; /* unlimited by default */
}

bool cfg_load(const char *path, BbsConfig *out)
{
  cfg_defaults(out);

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
      out->wfc_enabled = atoi(v);
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
    else if (!strcmp(k, "scheduler_enabled"))
    {
      out->scheduler_enabled = atoi(v);
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
      out->password_upgrade = atoi(v);
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
      out->plugins_enabled = atoi(v) != 0;
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
      out->allow_multi_login = atoi(v);
    }
    else if (!strcmp(k, "guest_enabled"))
    {
      out->guest_enabled = atoi(v);
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
      out->welcome_letter_enabled = atoi(v);
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
      out->door_cleanup_on_exit = atoi(v);
    }
    else if (!strcmp(k, "door_keep_failed_runs"))
    {
      out->door_keep_failed_runs = atoi(v);
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
