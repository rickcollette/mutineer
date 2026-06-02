#pragma once
#include "bbs_session.h"

/* File command handlers (F* commands from ORIGINAL_BBS) */

void cmd_file_area_change(Session* s, const char* data);    /* FA */
void cmd_file_batch_download(Session* s, const char* data); /* FB */
void cmd_file_batch_clear(Session* s, const char* data);    /* FC */
void cmd_file_download(Session* s, const char* data);       /* FD */
void cmd_file_extended(Session* s, const char* data);       /* FE - extended description */
void cmd_file_find(Session* s, const char* data);           /* FF */
void cmd_file_area_list(Session* s, const char* data);      /* FG */
void cmd_file_list(Session* s, const char* data);           /* FL */
void cmd_file_new_scan(Session* s, const char* data);       /* FN */
void cmd_file_raw_dir(Session* s, const char* data);        /* FR */
void cmd_file_upload(Session* s, const char* data);         /* FU */
void cmd_file_view_archive(Session* s, const char* data);   /* FV */
void cmd_file_expert_toggle(Session* s, const char* data);  /* FX */
void cmd_file_zippy_search(Session* s, const char* data);   /* FZ */
void cmd_file_set_scan_date(Session* s, const char* data);  /* FP - set new-scan cutoff date */
void cmd_file_archive_test(Session* s, const char* data);   /* FT - test archive integrity */
void cmd_file_archive_extract(Session* s, const char* data);/* FQ - extract archive to temp */
void cmd_file_batch_remove(Session* s, const char* data);   /* FK - remove entry from batch queue */
void cmd_file_batch_upload(Session* s, const char* data);   /* FJ - batch upload multiple files */

/* Main dispatcher for F* commands */
void handle_file_command(Session* s, const char* cmd, const char* data);
