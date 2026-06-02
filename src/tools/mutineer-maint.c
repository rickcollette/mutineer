/*
 * mutineer-maint - Database maintenance utility
 * 
 * Usage: mutineer-maint [options] <command>
 * 
 * Commands:
 *   vacuum      Vacuum the database (reclaim space)
 *   reindex     Rebuild database indexes
 *   analyze     Update query planner statistics
 *   integrity   Check database integrity
 *   backup      Create database backup
 * 
 * Options:
 *   -c, --config <path>   Path to config file (default: conf/mutineer.conf)
 *   -o, --output <path>   Output path for backup command
 *   -v, --verbose         Verbose output
 *   -h, --help            Show this help
 * 
 * Exit codes:
 *   0 - Success
 *   1 - Invalid arguments
 *   2 - Database error
 *   3 - File error
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <getopt.h>
#include <time.h>
#include <sys/stat.h>

#include "bbs_db.h"
#include "bbs_config.h"

static void print_usage(const char* prog) {
    printf("Usage: %s [options] <command>\n\n", prog);
    printf("Database maintenance utility.\n\n");
    printf("Commands:\n");
    printf("  vacuum      Vacuum the database (reclaim space)\n");
    printf("  reindex     Rebuild database indexes\n");
    printf("  analyze     Update query planner statistics\n");
    printf("  integrity   Check database integrity\n");
    printf("  backup      Create database backup\n");
    printf("\nOptions:\n");
    printf("  -c, --config <path>   Path to config file (default: conf/mutineer.conf)\n");
    printf("  -o, --output <path>   Output path for backup command\n");
    printf("  -v, --verbose         Verbose output\n");
    printf("  -h, --help            Show this help\n");
    printf("\nExamples:\n");
    printf("  %s vacuum                     # Vacuum database\n", prog);
    printf("  %s backup -o /backup/bbs.db   # Create backup\n", prog);
    printf("  %s integrity -v               # Check integrity with verbose output\n", prog);
}

static int cmd_vacuum(BbsDb* db, bool verbose) {
    if (verbose) printf("Vacuuming database...\n");
    
    if (!db_exec(db, "VACUUM")) {
        fprintf(stderr, "Vacuum failed: %s\n", db_last_error(db));
        return 2;
    }
    
    if (verbose) printf("Done.\n");
    return 0;
}

static int cmd_reindex(BbsDb* db, bool verbose) {
    if (verbose) printf("Rebuilding indexes...\n");
    
    if (!db_exec(db, "REINDEX")) {
        fprintf(stderr, "Reindex failed: %s\n", db_last_error(db));
        return 2;
    }
    
    if (verbose) printf("Done.\n");
    return 0;
}

static int cmd_analyze(BbsDb* db, bool verbose) {
    if (verbose) printf("Updating statistics...\n");
    
    if (!db_exec(db, "ANALYZE")) {
        fprintf(stderr, "Analyze failed: %s\n", db_last_error(db));
        return 2;
    }
    
    if (verbose) printf("Done.\n");
    return 0;
}

static int cmd_integrity(BbsDb* db, bool verbose) {
    if (verbose) printf("Checking database integrity...\n");
    
    if (!db_exec(db, "PRAGMA integrity_check")) {
        fprintf(stderr, "Integrity check failed: %s\n", db_last_error(db));
        return 2;
    }
    
    if (verbose) printf("Database integrity OK.\n");
    return 0;
}

static int cmd_backup(BbsDb* db, const char* db_path, const char* output_path, bool verbose) {
    char backup_path[512];
    
    if (output_path) {
        snprintf(backup_path, sizeof(backup_path), "%s", output_path);
    } else {
        time_t now = time(NULL);
        struct tm* tm = localtime(&now);
        snprintf(backup_path, sizeof(backup_path), "mutineer_backup_%04d%02d%02d_%02d%02d%02d.db",
                 tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
                 tm->tm_hour, tm->tm_min, tm->tm_sec);
    }
    
    if (verbose) printf("Creating backup: %s\n", backup_path);
    
    char sql[1024];
    snprintf(sql, sizeof(sql), "VACUUM INTO '%s'", backup_path);
    
    if (!db_exec(db, sql)) {
        snprintf(sql, sizeof(sql), "cp '%s' '%s'", db_path, backup_path);
        if (system(sql) != 0) {
            fprintf(stderr, "Backup failed\n");
            return 3;
        }
    }
    
    struct stat st;
    if (stat(backup_path, &st) == 0) {
        if (verbose) {
            printf("Backup created: %s (%ld bytes)\n", backup_path, (long)st.st_size);
        } else {
            printf("%s\n", backup_path);
        }
    }
    
    return 0;
}

int main(int argc, char** argv) {
    const char* config_path = NULL;
    const char* output_path = NULL;
    bool verbose = false;
    
    static struct option long_options[] = {
        {"config",  required_argument, 0, 'c'},
        {"output",  required_argument, 0, 'o'},
        {"verbose", no_argument,       0, 'v'},
        {"help",    no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };
    
    int opt;
    while ((opt = getopt_long(argc, argv, "c:o:vh", long_options, NULL)) != -1) {
        switch (opt) {
            case 'c': config_path = optarg; break;
            case 'o': output_path = optarg; break;
            case 'v': verbose = true; break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }
    
    if (optind >= argc) {
        fprintf(stderr, "Error: Command required\n\n");
        print_usage(argv[0]);
        return 1;
    }
    
    const char* command = argv[optind];
    
    BbsConfig cfg;
    if (!cfg_load(config_path ? config_path : "conf/mutineer.conf", &cfg)) {
        fprintf(stderr, "Failed to load config\n");
        return 2;
    }
    
    BbsDb* db = db_open(cfg.db_path);
    if (!db) {
        fprintf(stderr, "Failed to open database: %s\n", cfg.db_path);
        return 2;
    }
    
    int rc = 0;
    
    if (strcmp(command, "vacuum") == 0) {
        rc = cmd_vacuum(db, verbose);
    } else if (strcmp(command, "reindex") == 0) {
        rc = cmd_reindex(db, verbose);
    } else if (strcmp(command, "analyze") == 0) {
        rc = cmd_analyze(db, verbose);
    } else if (strcmp(command, "integrity") == 0) {
        rc = cmd_integrity(db, verbose);
    } else if (strcmp(command, "backup") == 0) {
        rc = cmd_backup(db, cfg.db_path, output_path, verbose);
    } else {
        fprintf(stderr, "Unknown command: %s\n\n", command);
        print_usage(argv[0]);
        rc = 1;
    }
    
    db_close(db);
    return rc;
}
