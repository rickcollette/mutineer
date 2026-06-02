/*
 * mutineer-userpack - Pack/purge deleted or inactive users
 * 
 * Usage: mutineer-userpack [options]
 * 
 * Options:
 *   -c, --config <path>   Path to config file (default: conf/mutineer.conf)
 *   -d, --deleted         Remove users marked as deleted
 *   -i, --inactive <days> Remove users inactive for N days
 *   -l, --level <n>       Only remove users with security level <= N (default: 10)
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
#include "bbs_flags.h"

static void print_usage(const char* prog) {
    printf("Usage: %s [options]\n\n", prog);
    printf("Pack/purge deleted or inactive users from the database.\n\n");
    printf("Options:\n");
    printf("  -c, --config <path>   Path to config file (default: conf/mutineer.conf)\n");
    printf("  -d, --deleted         Remove users marked as deleted\n");
    printf("  -i, --inactive <days> Remove users inactive for N days\n");
    printf("  -l, --level <n>       Only remove users with security level <= N (default: 10)\n");
    printf("  -n, --dry-run         Show what would be deleted without deleting\n");
    printf("  -v, --verbose         Verbose output\n");
    printf("  -h, --help            Show this help\n");
    printf("\nExamples:\n");
    printf("  %s -d                     # Remove deleted users\n", prog);
    printf("  %s -i 365                 # Remove users inactive for 1 year\n", prog);
    printf("  %s -d -i 180 -l 20        # Remove deleted + 180-day inactive (level <= 20)\n", prog);
}

int main(int argc, char** argv) {
    const char* config_path = NULL;
    bool remove_deleted = false;
    int inactive_days = 0;
    int max_level = 10;
    bool dry_run = false;
    bool verbose = false;
    
    static struct option long_options[] = {
        {"config",   required_argument, 0, 'c'},
        {"deleted",  no_argument,       0, 'd'},
        {"inactive", required_argument, 0, 'i'},
        {"level",    required_argument, 0, 'l'},
        {"dry-run",  no_argument,       0, 'n'},
        {"verbose",  no_argument,       0, 'v'},
        {"help",     no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };
    
    int opt;
    while ((opt = getopt_long(argc, argv, "c:di:l:nvh", long_options, NULL)) != -1) {
        switch (opt) {
            case 'c': config_path = optarg; break;
            case 'd': remove_deleted = true; break;
            case 'i': inactive_days = atoi(optarg); break;
            case 'l': max_level = atoi(optarg); break;
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
    
    if (!remove_deleted && inactive_days <= 0) {
        fprintf(stderr, "Error: Must specify --deleted and/or --inactive\n\n");
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
        printf("User Pack Utility\n");
        printf("=================\n");
        printf("Database: %s\n", cfg.db_path);
        printf("Remove deleted: %s\n", remove_deleted ? "yes" : "no");
        if (inactive_days > 0) {
            printf("Remove inactive > %d days: yes\n", inactive_days);
        }
        printf("Max security level: %d\n", max_level);
        printf("Dry run: %s\n", dry_run ? "yes" : "no");
        printf("\n");
    }
    
    int before_count = db_count_users(db);
    int total_deleted = 0;
    
    if (remove_deleted) {
        char sql[256];
        snprintf(sql, sizeof(sql),
            "DELETE FROM users WHERE (status_flags & %d) != 0 AND level <= %d",
            STATUS_DELETED, max_level);
        
        if (dry_run) {
            printf("[DRY RUN] Would execute: %s\n", sql);
        } else {
            int deleted = db_exec_simple(db, sql);
            total_deleted += deleted;
            if (verbose) {
                printf("Removed %d deleted user(s)\n", deleted);
            }
        }
    }
    
    if (inactive_days > 0) {
        char sql[512];
        snprintf(sql, sizeof(sql),
            "DELETE FROM users WHERE level <= %d AND "
            "(status_flags & %d) = 0 AND "
            "last_login_at < datetime('now', '-%d days')",
            max_level, STATUS_DELETED, inactive_days);
        
        if (dry_run) {
            printf("[DRY RUN] Would execute: %s\n", sql);
        } else {
            int deleted = db_exec_simple(db, sql);
            total_deleted += deleted;
            if (verbose) {
                printf("Removed %d inactive user(s)\n", deleted);
            }
        }
    }
    
    if (!dry_run) {
        db_exec_simple(db, "VACUUM");
        
        int after_count = db_count_users(db);
        
        if (verbose) {
            printf("\nUsers before: %d\n", before_count);
            printf("Users after:  %d\n", after_count);
            printf("Total removed: %d\n", total_deleted);
            printf("Database vacuumed.\n");
        } else {
            printf("%d\n", total_deleted);
        }
    } else {
        printf("\nTotal users: %d\n", before_count);
    }
    
    db_close(db);
    return 0;
}
