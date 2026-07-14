#pragma once
#include <stddef.h>
#include <stdint.h>

typedef struct TelnetState {
  int cols;
  int rows;

  /* parser state */
  int st;
  uint8_t cmd;
  uint8_t sb_opt;
  uint8_t sb_buf[128];
  size_t  sb_len;

  /* terminal type (optional) */
  char term_type[64];
} TelnetState;

/* Send initial negotiation requests. */
void telnet_send_initial(int fd);
void telnet_password_begin(int fd);
void telnet_password_end(int fd);

/* Feed bytes; strips telnet sequences.
   Returns number of output bytes written into out. */
size_t telnet_feed(TelnetState* tn, int fd,
                   const uint8_t* in, size_t in_len,
                   uint8_t* out, size_t out_cap);
