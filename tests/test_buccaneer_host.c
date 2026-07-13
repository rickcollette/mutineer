#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "include/bucc_host.h"

#define CHECK(cond, msg) do { \
  if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); return 1; } \
} while (0)

static void term_print(void* ctx, const char* text) {
  (void)ctx;
  (void)text;
}

static void term_println(void* ctx, const char* text) {
  (void)ctx;
  (void)text;
}

static int kv_set_called = 0;

static void kv_set(void* ctx, const char* key, const char* value) {
  (void)ctx;
  (void)key;
  (void)value;
  kv_set_called++;
}

int main(void) {
  bucc_host_context_t* ctx = bucc_host_context_new();
  CHECK(ctx != NULL, "host context allocates");

  bucc_term_api_t term = {
    .print = term_print,
    .println = term_println
  };
  bucc_kv_api_t kv = {
    .set = kv_set
  };
  bucc_host_set_term_api(ctx, &term, NULL);
  bucc_host_set_kv_api(ctx, &kv, NULL);
  bucc_host_set_allowed_capabilities(ctx, BUCC_CAP_TERM_OUTPUT);

  bucc_value_t ok = bucc_host_dispatch(NULL, "TERM", "PRINTLN", NULL, 0, ctx);
  CHECK(ok.kind == BUCC_VAL_NULL, "allowed TERM.PRINTLN succeeds");

  bucc_value_t denied = bucc_host_dispatch(NULL, "KV", "SET", NULL, 0, ctx);
  CHECK(denied.kind == BUCC_VAL_ERROR, "denied KV.SET returns an error");
  CHECK(kv_set_called == 0, "denied KV.SET does not call handler");
  CHECK(denied.as.error && strstr(denied.as.error->message, "Capability denied") != NULL,
        "denied error names capability denial");
  bucc_value_release(&denied);

  bucc_host_context_free(ctx);
  return 0;
}
