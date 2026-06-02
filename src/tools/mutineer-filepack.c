/*
 * mutineer-filepack - Remove orphaned file records (files not on disk)
 * 
 * Usage: mutineer-filepack [options]
 * 
 * Options:
 *   -c, --config <path>   Path to config file (default: conf/mutineer.conf)
 *   -a, --area <id>       Only process specific area ID (default: all areas)
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
#include <unistd.h>

#include "bbs_db.h"
#include "bbs_config.h"

static void print_usage(const char* prog) {
    printf("Usage: %s [options]\n\n", prog);
    printf("Remove orphaned file records (files not on disk).\n\n");
    printf("Options:\n");
    printf("  -c, --config <path>   Path to config file (default: conf/mutineer.conf)\n");
    printf("  -a, --area <id>       Only process specific area ID (default: all areas)\n");
    printf("  -n, --dry-run         Show what would be deleted without deleting\n");
    printf("  -v, --verbose         Verbose output\n");
    printf("  -h, --help            Show this help\n");
    printf("\nExamples:\n");
    printf("  %s                        # Check all areas\n", prog);
    printf("  %s -a 1 -v                # Check area 1 with verbose output\n", prog);
    printf("  %s -n                     # Dry run, show orphans without deleting\n", prog);
}

int main(int argc, char** argv) {
    const char* config_path = NULL;
    int area_id = -1;
    bool dry_run = false;
    bool verbose = false;
    
    static struct option long_options[] = {
        {"config",   required_argument, 0, 'c'},
        {"area",     required_argument, 0, 'a'},
        {"dry-run",  no_argument,       0, 'n'},
        {"verbose",  no_argument,       0, 'v'},
        {"help",     no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };
    
    int opt;
    while ((opt = getopt_long(argc, argv, "c:a:nvh", long_options, NULL)) != -1) {
        switch (opt) {
            case 'c': config_path = optarg; break;
            case 'a': area_id = atoi(optarg); break;
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
        printf("File Pack Utility\n");
        printf("=================\n");
        printf("Database: %s\n", cfg.db_path);
        if (area_id >= 0) {
            printf("Area filter: %d\n", area_id);
        }
        printf("Dry run: %s\n", dry_run ? "yes" : "no");
        printf("\n");
    }
    
    DbFileArea areas[64];
    int num_areas = db_file_area_list(db, areas, 64);
    
    int total_verified = 0;
    int total_orphaned = 0;
    
    for (int a = 0; a < num_areas; a++) {
        if (area_id >= 0 && areas[a].id != area_id) {
            continue;
        }
        
        if (verbose) {
            printf("Checking area [%d] %s (%s)\n", areas[a].id, areas[a].name, areas[a].path);
        }
        
        DbFileRec files[256];
        int num_files = db_file_list(&areas[a], db, files, 256);
        
        int area_verified = 0;
        int area_orphaned = 0;
        
        for (int f = 0; f < num_files; f++) {
            char filepath[512];
            snprintf(filepath, sizeof(filepath), "%s/%s", areas[a].path, files[f].filename);
            
            if (access(filepath, F_OK) != 0) {
                area_orphaned++;
                total_orphaned++;
                
                if (verbose) {
                    printf("  [ORPHAN] %s\n", files[f].filename);
                }
                
                if (!dry_run) {
                    db_file_delete(db, files[f].id);
                }
            } else {
                area_verified++;
                total_verified++;
            }
        }
        
        if (verbose && num_files > 0) {
            printf("  Verified: %d, Orphaned: %d\n", area_verified, area_orphaned);
        }
    }
    
    if (!dry_run && total_orphaned > 0) {
        db_exec_simple(db, "VACUUM");
    }
    
    if (verbose) {
        printf("\nSummary:\n");
        printf("  Total verified: %d\n", total_verified);
        printf("  Total orphaned: %d\n", total_orphaned);
        if (!dry_run && total_orphaned > 0) {
            printf("  Database vacuumed.\n");
        }
    } else {
        printf("%d\n", total_orphaned);
    }
    
    db_close(db);
    return 0;
}
