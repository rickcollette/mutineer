#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/select.h>

#include "bbs_session.h"
#include "bbs_telnet.h"

static int make_listener(const char* bind_ip, int port) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return -1;

  int yes = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons((uint16_t)port);
  if (inet_pton(AF_INET, bind_ip, &addr.sin_addr) != 1) {
    close(fd);
    return -1;
  }

  if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    close(fd);
    return -1;
  }

  if (listen(fd, 64) < 0) {
    close(fd);
    return -1;
  }

  return fd;
}

extern volatile sig_atomic_t g_stop;
extern volatile sig_atomic_t g_broadcast_pending;
void broadcast_check(const char* data_path);

int net_run_listener(const BbsConfig* cfg, BbsDb* db, volatile sig_atomic_t* stop_flag) {
  int lfd = make_listener(cfg->bind, cfg->port);
  if (lfd < 0) {
    if (errno == EADDRINUSE) {
      fprintf(stderr, "Port %d already in use on %s. Exiting gracefully.\n", cfg->port, cfg->bind);
    } else {
      fprintf(stderr, "listener bind failed (%s:%d): %s\n", cfg->bind, cfg->port, strerror(errno));
    }
    return 3;
  }

  for (;;) {
    if (stop_flag && *stop_flag) break;
    
    /* Check for pending broadcast messages */
    if (g_broadcast_pending) {
      g_broadcast_pending = 0;
      broadcast_check(cfg->data_path);
    }
    
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(lfd, &rfds);
    struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
    int sel = select(lfd + 1, &rfds, NULL, NULL, &tv);
    if (sel < 0) {
      if (errno == EINTR) continue;
      fprintf(stderr, "select failed: %s\n", strerror(errno));
      continue;
    }
    if (sel == 0) continue;  /* timeout, check stop_flag */
    
    struct sockaddr_in peer;
    socklen_t plen = sizeof(peer);
    int cfd = accept(lfd, (struct sockaddr*)&peer, &plen);
    if (cfd < 0) {
      if (errno == EINTR) continue;
      fprintf(stderr, "accept failed: %s\n", strerror(errno));
      continue;
    }

    Session* s = (Session*)calloc(1, sizeof(Session));
    s->fd = cfd;
    (void)db;
    s->db = db_open(cfg->db_path);
    if (!s->db) {
      const char* msg = "\r\nSystem is temporarily unavailable. Please try again later.\r\n";
      (void)write(cfd, msg, strlen(msg));
      fprintf(stderr, "session db_open failed for %s\n", cfg->db_path);
      close(cfd);
      free(s);
      continue;
    }
    s->alive = 1;
    s->cfg = *cfg;
    s->tn.cols = 80;
    s->tn.rows = 24;
    s->started_at = time(NULL);
    s->node_num = 0; /* will be set after online_add assigns a slot */

    char ip[64];
    inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof(ip));
    snprintf(s->ip, sizeof(s->ip), "%s", ip);

    telnet_send_initial(cfd);

    pthread_t th;
    if (pthread_create(&th, NULL, session_thread_main, s) != 0) {
      close(cfd);
      free(s);
      continue;
    }
    pthread_detach(th);
  }

  close(lfd);
  return 0;
}
