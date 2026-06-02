/*
 * hello.c - Hello World Sample Plugin
 *
 * This is a minimal interactive plugin demonstrating the BBS plugin API.
 * It greets the user, allows them to type messages, and exits on "exit".
 */

#include "bbs_plugin_api.h"
#include <string.h>
#include <stdlib.h>

/* Store the host API globally for this plugin */
static const bbs_host_api_t* H = NULL;

/* Per-instance state */
typedef struct hello_inst {
  int message_count;
} hello_inst_t;

/* Instance vtable functions */

static bbs_rc_t hello_enter(void* inst, bbs_session_t* s) {
  hello_inst_t* hi = (hello_inst_t*)inst;
  hi->message_count = 0;
  
  H->io->printf(s, "\r\n");
  H->io->printf(s, "\x1b[1;36m========================================\x1b[0m\r\n");
  H->io->printf(s, "\x1b[1;36m       HELLO WORLD PLUGIN v1.0         \x1b[0m\r\n");
  H->io->printf(s, "\x1b[1;36m========================================\x1b[0m\r\n");
  H->io->printf(s, "\r\n");
  H->io->printf(s, "Welcome, \x1b[1;33m%s\x1b[0m!\r\n", H->session_username(s));
  H->io->printf(s, "Your user ID is: %u\r\n", H->session_user_id(s));
  H->io->printf(s, "Connecting from: %s\r\n", H->session_remote_addr(s));
  H->io->printf(s, "\r\n");
  
  H->log(BBS_LOG_INFO, "hello", "user entered plugin");
  
  return BBS_OK;
}

static bbs_rc_t hello_run(void* inst, bbs_session_t* s) {
  hello_inst_t* hi = (hello_inst_t*)inst;
  char line[256];
  
  H->io->printf(s, "Type anything and I'll echo it back.\r\n");
  H->io->printf(s, "Type 'exit' to return to the BBS.\r\n");
  H->io->printf(s, "\r\n");
  
  while (1) {
    H->io->printf(s, "\x1b[1;32m[hello]\x1b[0m> ");
    
    bbs_rc_t rc = H->io->readline(s, line, sizeof(line), 1);
    if (rc != BBS_OK) {
      if (rc == BBS_EIO) {
        return BBS_EIO;  /* Connection lost */
      }
      continue;  /* Timeout, try again */
    }
    
    /* Check for exit command */
    if (strcmp(line, "exit") == 0 || strcmp(line, "quit") == 0 || 
        strcmp(line, "q") == 0 || strcmp(line, "Q") == 0) {
      break;
    }
    
    /* Check for clear screen */
    if (strcmp(line, "cls") == 0 || strcmp(line, "clear") == 0) {
      H->io->cls(s);
      continue;
    }
    
    /* Check for help */
    if (strcmp(line, "help") == 0 || strcmp(line, "?") == 0) {
      H->io->printf(s, "\r\n");
      H->io->printf(s, "Commands:\r\n");
      H->io->printf(s, "  exit, quit, q - Return to BBS\r\n");
      H->io->printf(s, "  cls, clear    - Clear screen\r\n");
      H->io->printf(s, "  help, ?       - Show this help\r\n");
      H->io->printf(s, "  count         - Show message count\r\n");
      H->io->printf(s, "  <anything>    - Echo back\r\n");
      H->io->printf(s, "\r\n");
      continue;
    }
    
    /* Check for count */
    if (strcmp(line, "count") == 0) {
      H->io->printf(s, "You have typed %d message(s) so far.\r\n", hi->message_count);
      continue;
    }
    
    /* Echo the input back */
    if (line[0]) {
      hi->message_count++;
      H->io->printf(s, "You typed: \x1b[1;33m%s\x1b[0m\r\n", line);
    }
  }
  
  return BBS_OK;
}

static void hello_exit(void* inst, bbs_session_t* s) {
  hello_inst_t* hi = (hello_inst_t*)inst;
  
  H->io->printf(s, "\r\n");
  H->io->printf(s, "You typed %d message(s) during this session.\r\n", hi->message_count);
  H->io->printf(s, "\x1b[1;36mGoodbye from Hello Plugin!\x1b[0m\r\n");
  H->io->printf(s, "\r\n");
  
  H->log(BBS_LOG_INFO, "hello", "user exited plugin");
}

static void hello_on_event(void* inst, const bbs_event_t* ev) {
  (void)inst;
  (void)ev;
}

static void hello_destroy(void* inst) {
  free(inst);
}

/* Static vtable */
static const bbs_plugin_instance_vtbl_t HELLO_VTBL = {
  .on_enter = hello_enter,
  .run = hello_run,
  .on_exit = hello_exit,
  .on_event = hello_on_event,
  .destroy = hello_destroy,
  .reserved = {NULL}
};

/* Plugin global functions */

static bbs_rc_t hello_init(const bbs_host_api_t* host) {
  H = host;
  host->log(BBS_LOG_INFO, "hello", "plugin initialized");
  return BBS_OK;
}

static void hello_shutdown(void) {
  if (H) {
    H->log(BBS_LOG_INFO, "hello", "plugin shutting down");
  }
}

static bbs_rc_t hello_create_instance(bbs_session_t* s, void** out_inst, 
                                       const bbs_plugin_instance_vtbl_t** out_vtbl) {
  (void)s;
  
  hello_inst_t* inst = calloc(1, sizeof(hello_inst_t));
  if (!inst) return BBS_EINTERNAL;
  
  *out_inst = inst;
  *out_vtbl = &HELLO_VTBL;
  return BBS_OK;
}

/* Required exported function */
bbs_rc_t bbs_plugin_query(uint32_t host_abi_version,
                          const bbs_host_api_t* host,
                          bbs_plugin_desc_t* out) {
  if (!out || !host) return BBS_EINVAL;
  
  /* Check ABI version compatibility */
  uint32_t host_major = (host_abi_version >> 16) & 0xFFFF;
  uint32_t our_major = (BBS_PLUGIN_ABI_VERSION >> 16) & 0xFFFF;
  if (host_major != our_major) return BBS_EUNSUPPORTED;
  
  /* Zero everything so reserved pointers are NULL */
  memset(out, 0, sizeof(*out));
  
  out->abi_version = BBS_PLUGIN_ABI_VERSION;
  out->size = sizeof(*out);
  out->magic = BBS_PLUGIN_MAGIC;
  
  out->id = "com.mutineer.hello";
  out->name = "Hello World Plugin";
  out->version = "1.0.0";
  out->author = "Mutineer BBS";
  out->description = "A simple interactive plugin demonstrating the plugin API.";
  out->caps = BBS_CAP_INTERACTIVE;
  
  out->init = hello_init;
  out->shutdown = hello_shutdown;
  out->create_instance = hello_create_instance;
  out->commands = NULL;
  out->on_event = NULL;
  
  return BBS_OK;
}
