#pragma once
#include "bbs_config.h"
#include "bbs_db.h"

void console_service_start(const BbsConfig *cfg, BbsDb *db);
void console_service_stop(void);
