/*
 * mutineer-msgpack - Pack/purge old messages from the database
 * 
 * Usage: mutineer-msgpack [options]
 * 
 * Options:
 *   -c, --config <path>   Path to config file (default: conf/mutineer.conf)
 *   -d, --days <n>        Delete messages older than N days (required)
 *   -a, --area <id>       Only process specific area ID (default: all areas)
 *   -p, --private         Also delete private mail (default: skip private)
 *   -n, --dry-run         Show what would be deleted without deleting
 *   -v, --verbose         Verbose output
 *   -h, --help            Show this help
 * 
 * Exit codes:
 *   0 - Success
 *   1 - Invalid arguments
 *   2 - Database error
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <getopt.h>

#include "bbs_db.h"
#include "bbs_config.h"

static void print_usage(const char* prog) {
    printf("Usage: %s [options]\n\n", prog);
    printf("Pack/purge old messages from the database.\n\n");
    printf("Options:\n");
    printf("  -c, --config <path>   Path to config file (default: conf/mutineer.conf)\n");
    printf("  -d, --days <n>        Delete messages older than N days (required)\n");
    printf("  -a, --area <id>       Only process specific area ID (default: all areas)\n");
    printf("  -p, --private         Also delete private mail (default: skip private)\n");
    printf("  -n, --dry-run         Show what would be deleted without deleting\n");
    printf("  -v, --verbose         Verbose output\n");
    printf("  -h, --help            Show this help\n");
    printf("\nExamples:\n");
    printf("  %s -d 90                  # Delete messages older than 90 days\n", prog);
    printf("  %s -d 30 -a 1             # Delete from area 1 only\n", prog);
    printf("  %s -d 60 -n -v            # Dry run with verbose output\n", prog);
}

int main(int argc, char** argv) {
    const char* config_path = NULL;
    int days = 0;
    int area_id = -1;
    bool include_private = false;
    bool dry_run = false;
    bool verbose = false;
    
    static struct option long_options[] = {
        {"config",   required_argument, 0, 'c'},
        {"days",     required_argument, 0, 'd'},
        {"area",     required_argument, 0, 'a'},
        {"private",  no_argument,       0, 'p'},
        {"dry-run",  no_argument,       0, 'n'},
        {"verbose",  no_argument,       0, 'v'},
        {"help",     no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };
    
    int opt;
    while ((opt = getopt_long(argc, argv, "c:d:a:pnvh", long_options, NULL)) != -1) {
        switch (opt) {
            case 'c': config_path = optarg; break;
            case 'd': days = atoi(optarg); break;
            case 'a': area_id = atoi(optarg); break;
            case 'p': include_private = true; break;
            case 'n': dry_run = true; break;
            case 'v': verbose = true; break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }
    
    if (days <= 0) {
        fprintf(stderr, "Error: --days is required and must be positive\n\n");
        print_usage(argv[0]);
        return 1;
    }
    
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
    
    if (verbose) {
        printf("Message Pack Utility\n");
        printf("====================\n");
        printf("Database: %s\n", cfg.db_path);
        printf("Delete messages older than: %d days\n", days);
        if (area_id >= 0) {
            printf("Area filter: %d\n", area_id);
        }
        printf("Include private mail: %s\n", include_private ? "yes" : "no");
        printf("Dry run: %s\n", dry_run ? "yes" : "no");
        printf("\n");
    }
    
    int before_count = db_count_messages(db);
    
    char sql[512];
    if (area_id >= 0) {
        if (include_private) {
            snprintf(sql, sizeof(sql),
                "DELETE FROM messages WHERE area_id = %d AND created_at < datetime('now', '-%d days')",
                area_id, days);
        } else {
            snprintf(sql, sizeof(sql),
                "DELETE FROM messages WHERE area_id = %d AND to_user = 0 AND created_at < datetime('now', '-%d days')",
                area_id, days);
        }
    } else {
        if (include_private) {
            snprintf(sql, sizeof(sql),
                "DELETE FROM messages WHERE created_at < datetime('now', '-%d days')",
                days);
        } else {
            snprintf(sql, sizeof(sql),
                "DELETE FROM messages WHERE to_user = 0 AND created_at < datetime('now', '-%d days')",
                days);
        }
    }
    
    if (dry_run) {
        char count_sql[512];
        if (area_id >= 0) {
            if (include_private) {
                snprintf(count_sql, sizeof(count_sql),
                    "SELECT COUNT(*) FROM messages WHERE area_id = %d AND created_at < datetime('now', '-%d days')",
                    area_id, days);
            } else {
                snprintf(count_sql, sizeof(count_sql),
                    "SELECT COUNT(*) FROM messages WHERE area_id = %d AND to_user = 0 AND created_at < datetime('now', '-%d days')",
                    area_id, days);
            }
        } else {
            if (include_private) {
                snprintf(count_sql, sizeof(count_sql),
                    "SELECT COUNT(*) FROM messages WHERE created_at < datetime('now', '-%d days')",
                    days);
            } else {
                snprintf(count_sql, sizeof(count_sql),
                    "SELECT COUNT(*) FROM messages WHERE to_user = 0 AND created_at < datetime('now', '-%d days')",
                    days);
            }
        }
        
        printf("[DRY RUN] Would execute: %s\n", sql);
        printf("Total messages before: %d\n", before_count);
    } else {
        int deleted = db_exec_simple(db, sql);
        
        if (verbose) {
            printf("Deleted %d message(s)\n", deleted);
        }
        
        db_exec_simple(db, "VACUUM");
        
        int after_count = db_count_messages(db);
        
        if (verbose) {
            printf("Messages before: %d\n", before_count);
            printf("Messages after:  %d\n", after_count);
            printf("Database vacuumed.\n");
        } else {
            printf("%d\n", deleted);
        }
    }
    
    db_close(db);
    return 0;
}
