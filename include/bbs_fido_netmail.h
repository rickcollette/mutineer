#pragma once
#include "bbs_db.h"
#include <stdbool.h>

typedef struct FidoNetmailExportResult
{
  int scanned;
  int exported;
  int failed;
  char last_path[256];
} FidoNetmailExportResult;

typedef struct FidoEchomailExportResult
{
  int scanned;
  int exported;
  int failed;
} FidoEchomailExportResult;

typedef struct FidoEchomailImportResult
{
  int scanned;
  int imported;
  int failed;
  int duplicate;
} FidoEchomailImportResult;

bool fido_netmail_export_pending(BbsDb *db, const char *out_dir, int limit, bool dry_run,
                                 FidoNetmailExportResult *result);

/* Export queued echomail messages to out_dir as text packet files. */
bool fido_echomail_export_pending(BbsDb *db, const char *out_dir, int limit, bool dry_run,
                                  FidoEchomailExportResult *result);

/* Import echomail packet files from in_dir and post to linked message areas. */
bool fido_echomail_import(BbsDb *db, const char *in_dir, FidoEchomailImportResult *result);
