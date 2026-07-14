#include "bbs_telnet.h"
#include "bbs_util.h"
#include <string.h>
#include <unistd.h>

/* Telnet constants */
enum { IAC=255, DONT=254, DO=253, WONT=252, WILL=251, SB=250, SE=240 };
enum { TELOPT_ECHO=1, TELOPT_SGA=3, TELOPT_TTYPE=24, TELOPT_NAWS=31 };
enum { TTYPE_IS=0, TTYPE_SEND=1 };

enum ParserState { ST_DATA=0, ST_IAC=1, ST_IAC_CMD=2, ST_SB=3, ST_SB_IAC=4 };

static void tn_send3(int fd, uint8_t a, uint8_t b, uint8_t c) {
  uint8_t buf[3] = {a,b,c};
  fd_write_all(fd, buf, 3);
}

static void tn_send_ttype_send(int fd) {
  uint8_t buf[] = { IAC, SB, TELOPT_TTYPE, TTYPE_SEND, IAC, SE };
  fd_write_all(fd, buf, sizeof(buf));
}

void telnet_send_initial(int fd) {
  /* Common baseline */
  tn_send3(fd, IAC, WILL, TELOPT_SGA);
  tn_send3(fd, IAC, DO,   TELOPT_SGA);

  /* Ask for NAWS (window size) and terminal type */
  tn_send3(fd, IAC, DO,   TELOPT_NAWS);
  tn_send3(fd, IAC, DO,   TELOPT_TTYPE);

  /* We can echo by default; we can disable echo for password later if desired. */
  tn_send3(fd, IAC, WILL, TELOPT_ECHO);

  /* Request terminal type immediately (clients may ignore until DO TTYPE accepted; harmless). */
  tn_send_ttype_send(fd);
}

void telnet_password_begin(int fd) {
  tn_send3(fd, IAC, WILL, TELOPT_ECHO);
  tn_send3(fd, IAC, DO, TELOPT_SGA);
}

void telnet_password_end(int fd) {
  tn_send3(fd, IAC, WONT, TELOPT_ECHO);
  tn_send3(fd, IAC, DO, TELOPT_SGA);
}

static void handle_iac_cmd(TelnetState* tn, int fd, uint8_t cmd, uint8_t opt) {
  (void)tn;

  /* Respond conservatively: only accept what we support. */
  if (cmd == DO) {
    switch (opt) {
      case TELOPT_SGA:
      case TELOPT_ECHO:
        tn_send3(fd, IAC, WILL, opt);
        break;
      default:
        tn_send3(fd, IAC, WONT, opt);
        break;
    }
  } else if (cmd == WILL) {
    switch (opt) {
      case TELOPT_SGA:
      case TELOPT_NAWS:
      case TELOPT_TTYPE:
        tn_send3(fd, IAC, DO, opt);
        break;
      default:
        tn_send3(fd, IAC, DONT, opt);
        break;
    }
  } else if (cmd == DONT) {
    /* no-op for now */
  } else if (cmd == WONT) {
    /* no-op for now */
  }
}

static void process_sb(TelnetState* tn, int fd) {
  (void)fd;
  if (tn->sb_len < 1) return;
  uint8_t opt = tn->sb_buf[0];

  if (opt == TELOPT_NAWS) {
    if (tn->sb_len >= 1 + 4) {
      int w = (tn->sb_buf[1] << 8) | tn->sb_buf[2];
      int h = (tn->sb_buf[3] << 8) | tn->sb_buf[4];
      if (w > 0 && w < 1000) tn->cols = w;
      if (h > 0 && h < 1000) tn->rows = h;
    }
  } else if (opt == TELOPT_TTYPE) {
    /* [opt][IS][...string...] */
    if (tn->sb_len >= 3 && tn->sb_buf[1] == TTYPE_IS) {
      size_t n = tn->sb_len - 2;
      if (n >= sizeof(tn->term_type)) n = sizeof(tn->term_type) - 1;
      memcpy(tn->term_type, &tn->sb_buf[2], n);
      tn->term_type[n] = 0;
    }
  }
}

size_t telnet_feed(TelnetState* tn, int fd,
                   const uint8_t* in, size_t in_len,
                   uint8_t* out, size_t out_cap) {
  size_t o = 0;

  for (size_t i = 0; i < in_len; i++) {
    uint8_t b = in[i];

    switch (tn->st) {
      case ST_DATA:
        if (b == IAC) {
          tn->st = ST_IAC;
        } else {
          if (o < out_cap) out[o++] = b;
        }
        break;

      case ST_IAC:
        if (b == IAC) {
          if (o < out_cap) out[o++] = IAC; /* escaped 255 */
          tn->st = ST_DATA;
        } else if (b == DO || b == DONT || b == WILL || b == WONT) {
          tn->cmd = b;
          tn->st = ST_IAC_CMD;
        } else if (b == SB) {
          tn->sb_len = 0;
          tn->st = ST_SB;
        } else {
          tn->st = ST_DATA; /* ignore */
        }
        break;

      case ST_IAC_CMD:
        handle_iac_cmd(tn, fd, tn->cmd, b);
        tn->st = ST_DATA;
        break;

      case ST_SB:
        if (b == IAC) {
          tn->st = ST_SB_IAC;
        } else {
          if (tn->sb_len < sizeof(tn->sb_buf)) tn->sb_buf[tn->sb_len++] = b;
        }
        break;

      case ST_SB_IAC:
        if (b == SE) {
          process_sb(tn, fd);
          tn->st = ST_DATA;
        } else if (b == IAC) {
          if (tn->sb_len < sizeof(tn->sb_buf)) tn->sb_buf[tn->sb_len++] = IAC;
          tn->st = ST_SB;
        } else {
          tn->st = ST_SB;
        }
        break;

      default:
        tn->st = ST_DATA;
        break;
    }
  }

  return o;
}
