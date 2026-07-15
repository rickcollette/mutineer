#pragma once

#include <stddef.h>
#include <stdbool.h>
#include "bbs_config.h"
#include "bbs_db.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BBSLIB_VERSION_MAJOR 0
#define BBSLIB_VERSION_MINOR 1
#define BBSLIB_VERSION_PATCH 0
#define BBSLIB_VERSION_STRING "0.1.0"

typedef enum BbsLibResult
{
  BBSLIB_OK = 0,
  BBSLIB_ERR_INVALID = -1,
  BBSLIB_ERR_CONFIG = -2,
  BBSLIB_ERR_DB = -3,
  BBSLIB_ERR_NOT_FOUND = -4,
  BBSLIB_ERR_AUTH = -5,
  BBSLIB_ERR_DENIED = -6,
  BBSLIB_ERR_UNSUPPORTED = -7,
  BBSLIB_ERR_IO = -8,
  BBSLIB_ERR_BUFFER = -9
} BbsLibResult;

typedef enum BbsLibOpenFlags
{
  BBSLIB_OPEN_NONE = 0,
  BBSLIB_OPEN_INIT_SCHEMA = 1 << 0,
  BBSLIB_OPEN_SEED_DEFAULTS = 1 << 1
} BbsLibOpenFlags;

typedef struct BbsLibContext BbsLibContext;

typedef struct BbsLibOpenOptions
{
  const char *config_path;
  const char *schema_path;
  const char *sysop_password_hash;
  unsigned flags;
} BbsLibOpenOptions;

BbsLibResult bbslib_open(const BbsLibOpenOptions *opts, BbsLibContext **out);
BbsLibResult bbslib_open_path(const char *config_path, BbsLibContext **out);
void bbslib_close(BbsLibContext *ctx);

const char *bbslib_last_error(const BbsLibContext *ctx);
const BbsConfig *bbslib_config(const BbsLibContext *ctx);
BbsDb *bbslib_db(const BbsLibContext *ctx);

const char *bbslib_version(void);
const char *bbslib_result_string(BbsLibResult result);

#ifdef __cplusplus
}
#endif
