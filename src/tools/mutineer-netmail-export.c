/*
 * mutineer-netmail-export - Export pending FidoNet netmail packets.
 */

#include "bbs_config.h"
#include "bbs_db.h"
#include "bbs_fido_netmail.h"
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *prog)
{
  printf("Usage: %s [options]\n\n", prog);
  printf("Export pending FidoNet netmail into outbound packet files.\n\n");
  printf("Options:\n");
  printf("  -c, --config <path>   Config file (default: conf/mutineer.conf)\n");
  printf("  -o, --out <dir>       Output directory (default: <data_path>/mail/outbound)\n");
  printf("  -l, --limit <n>       Maximum messages to export (default: 100)\n");
  printf("  -n, --dry-run         Count/export preview without writing files\n");
  printf("  -v, --verbose         Verbose output\n");
  printf("  -h, --help            Show this help\n");
}

int main(int argc, char **argv)
{
  const char *config_path = "conf/mutineer.conf";
  const char *out_dir_arg = NULL;
  int limit = 100;
  bool dry_run = false;
  bool verbose = false;

  static struct option opts[] = {
      {"config", required_argument, 0, 'c'},
      {"out", required_argument, 0, 'o'},
      {"limit", required_argument, 0, 'l'},
      {"dry-run", no_argument, 0, 'n'},
      {"verbose", no_argument, 0, 'v'},
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}};

  int ch;
  while ((ch = getopt_long(argc, argv, "c:o:l:nvh", opts, NULL)) != -1)
  {
    switch (ch)
    {
    case 'c': config_path = optarg; break;
    case 'o': out_dir_arg = optarg; break;
    case 'l': limit = atoi(optarg); break;
    case 'n': dry_run = true; break;
    case 'v': verbose = true; break;
    case 'h':
      usage(argv[0]);
      return 0;
    default:
      usage(argv[0]);
      return 1;
    }
  }

  BbsConfig cfg;
  if (!cfg_load(config_path, &cfg))
  {
    fprintf(stderr, "Failed to load config: %s\n", config_path);
    return 2;
  }

  char out_dir[256];
  if (out_dir_arg)
    snprintf(out_dir, sizeof(out_dir), "%s", out_dir_arg);
  else
    snprintf(out_dir, sizeof(out_dir), "%s/mail/outbound", cfg.data_path);

  BbsDb *db = db_open(cfg.db_path);
  if (!db)
  {
    fprintf(stderr, "Failed to open database: %s\n", cfg.db_path);
    return 2;
  }

  FidoNetmailExportResult result;
  bool ok = fido_netmail_export_pending(db, out_dir, limit, dry_run, &result);
  db_close(db);

  if (verbose)
  {
    printf("Scanned:  %d\n", result.scanned);
    printf("Exported: %d\n", result.exported);
    printf("Failed:   %d\n", result.failed);
    if (result.last_path[0])
      printf("Last:     %s\n", result.last_path);
  }
  else
  {
    printf("%d\n", result.exported);
  }

  return ok ? 0 : 2;
}
