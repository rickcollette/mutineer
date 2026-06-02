#pragma once
#include <stdbool.h>
#include "bbs_config.h"
#include "bbs_db.h"

/* Run sanity checks for required files and directories.
 * Creates missing directories if possible.
 * Returns true if all critical checks pass. */
bool startup_sanity_check(const BbsConfig* cfg);

/* Initialize database with schema and seed data.
 * Creates default users, message areas, file areas if missing.
 * Returns true on success. */
bool startup_init_database(BbsDb* db, const BbsConfig* cfg);
