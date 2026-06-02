#pragma once
#include "bbs_session.h"

/* QWK packet generation. last_qwk may be NULL to include all messages. */
bool qwk_generate_packet(Session* s, const char* output_path, const char* last_qwk);

/* QWK reply import */
bool qwk_import_rep(Session* s, const char* rep_path);

/* QWK commands */
void cmd_qwk_download(Session* s, const char* data);
void cmd_qwk_upload(Session* s, const char* data);
