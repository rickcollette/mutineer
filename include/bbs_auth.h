#pragma once

#include "bbs_config.h"
#include <stdbool.h>

bool bbs_login_throttled(const BbsConfig *cfg, const char *ip, const char *handle);
void bbs_login_record(const BbsConfig *cfg, const char *ip, const char *handle, bool success);
