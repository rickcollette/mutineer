#pragma once

#include "bbslib/lifecycle.h"
#include "plank/plank.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct BbsLibPlankStatus
{
  int peers;
  int links;
  int areas;
  char node_name[64];
  char network_name[64];
  char node_addr[128];
} BbsLibPlankStatus;

BbsLibResult bbslib_plank_status(BbsLibContext *ctx, BbsLibPlankStatus *out);

#ifdef __cplusplus
}
#endif
