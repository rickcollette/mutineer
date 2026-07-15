#include "bbslib.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <time.h>
#include <unistd.h>

#define REQ_MAX 65536
#define RESP_MAX 262144
#define BRIDGE_MAX 32
#define BRIDGE_OUT_MAX 65536
#define BRIDGE_INPUT_MAX 16

typedef struct BridgeSession
{
  int used;
  int id;
  time_t touched;
  char handle[64];
  char output[BRIDGE_OUT_MAX];
  size_t output_len;
  char input[BRIDGE_INPUT_MAX][512];
  int input_head;
  int input_tail;
} BridgeSession;

static volatile sig_atomic_t g_stop = 0;
static BridgeSession g_bridge[BRIDGE_MAX];
static int g_next_bridge_id = 1;

static void on_signal(int sig)
{
  (void)sig;
  g_stop = 1;
}

static void usage(const char *prog)
{
  printf("Usage: %s [options]\n\n", prog);
  printf("Options:\n");
  printf("  -c, --config <path>   Config file (default: conf/mutineer.conf)\n");
  printf("  -b, --bind <addr>     Bind address (default: 127.0.0.1)\n");
  printf("  -p, --port <port>     Port (default: 8788)\n");
  printf("  -h, --help            Show help\n");
}

static void json_escape(char *out, size_t cap, const char *in)
{
  size_t o = 0;
  if (!out || cap == 0)
    return;
  for (const unsigned char *p = (const unsigned char *)(in ? in : ""); *p && o + 1 < cap; p++)
  {
    if (*p == '"' || *p == '\\')
    {
      if (o + 2 >= cap) break;
      out[o++] = '\\';
      out[o++] = (char)*p;
    }
    else if (*p == '\n')
    {
      if (o + 2 >= cap) break;
      out[o++] = '\\';
      out[o++] = 'n';
    }
    else if (*p == '\r')
    {
      if (o + 2 >= cap) break;
      out[o++] = '\\';
      out[o++] = 'r';
    }
    else if (*p < 0x20)
    {
      if (o + 6 >= cap) break;
      o += (size_t)snprintf(out + o, cap - o, "\\u%04x", *p);
    }
    else
      out[o++] = (char)*p;
  }
  out[o] = '\0';
}

static void http_send(int fd, int status, const char *ctype, const char *body)
{
  const char *reason = "OK";
  if (status == 201) reason = "Created";
  else if (status == 400) reason = "Bad Request";
  else if (status == 401) reason = "Unauthorized";
  else if (status == 404) reason = "Not Found";
  else if (status == 405) reason = "Method Not Allowed";
  else if (status >= 500) reason = "Internal Server Error";
  size_t len = body ? strlen(body) : 0;
  char hdr[512];
  int n = snprintf(hdr, sizeof(hdr),
                   "HTTP/1.1 %d %s\r\n"
                   "Content-Type: %s\r\n"
                   "Content-Length: %zu\r\n"
                   "Connection: close\r\n"
                   "Cache-Control: no-store\r\n\r\n",
                   status, reason, ctype ? ctype : "application/json", len);
  send(fd, hdr, (size_t)n, 0);
  if (len)
    send(fd, body, len, 0);
}

static const char *header_value(const char *req, const char *name)
{
  static char value[1024];
  size_t name_len = strlen(name);
  const char *p = req;
  value[0] = '\0';
  while ((p = strstr(p, "\n")) != NULL)
  {
    p++;
    if (!strncasecmp(p, name, name_len) && p[name_len] == ':')
    {
      p += name_len + 1;
      while (*p == ' ' || *p == '\t') p++;
      size_t n = 0;
      while (p[n] && p[n] != '\r' && p[n] != '\n' && n + 1 < sizeof(value))
      {
        value[n] = p[n];
        n++;
      }
      value[n] = '\0';
      return value;
    }
  }
  return NULL;
}

static bool authorized(const char *req, const char *token)
{
  if (!token || !token[0])
    return true;
  const char *auth = header_value(req, "Authorization");
  char expected[512];
  snprintf(expected, sizeof(expected), "Bearer %s", token);
  return auth && !strcmp(auth, expected);
}

static char *body_start(char *req)
{
  char *p = strstr(req, "\r\n\r\n");
  return p ? p + 4 : NULL;
}

static int bridge_write(void *user_data, const uint8_t *data, size_t len)
{
  BridgeSession *bs = (BridgeSession *)user_data;
  if (!bs || !data)
    return -1;
  if (len > BRIDGE_OUT_MAX - bs->output_len - 1)
    len = BRIDGE_OUT_MAX - bs->output_len - 1;
  if (len > 0)
  {
    memcpy(bs->output + bs->output_len, data, len);
    bs->output_len += len;
    bs->output[bs->output_len] = '\0';
  }
  bs->touched = time(NULL);
  return (int)len;
}

static int bridge_readline(void *user_data, uint8_t *buf, size_t cap, int timeout_sec, int echo)
{
  (void)timeout_sec;
  BridgeSession *bs = (BridgeSession *)user_data;
  if (!bs || !buf || cap == 0)
    return -1;
  if (bs->input_head == bs->input_tail)
    return -2;
  char *line = bs->input[bs->input_head];
  bs->input_head = (bs->input_head + 1) % BRIDGE_INPUT_MAX;
  size_t n = strlen(line);
  if (n >= cap) n = cap - 1;
  memcpy(buf, line, n);
  buf[n] = '\0';
  if (echo)
  {
    bridge_write(user_data, (const uint8_t *)line, n);
    bridge_write(user_data, (const uint8_t *)"\r\n", 2);
  }
  bs->touched = time(NULL);
  return (int)n;
}

static BridgeSession *bridge_find(int id)
{
  time_t now = time(NULL);
  for (int i = 0; i < BRIDGE_MAX; i++)
  {
    if (g_bridge[i].used && now - g_bridge[i].touched > 900)
      g_bridge[i].used = 0;
    if (g_bridge[i].used && g_bridge[i].id == id)
      return &g_bridge[i];
  }
  return NULL;
}

static BridgeSession *bridge_create(const char *handle)
{
  time_t now = time(NULL);
  for (int i = 0; i < BRIDGE_MAX; i++)
  {
    if (!g_bridge[i].used || now - g_bridge[i].touched > 900)
    {
      memset(&g_bridge[i], 0, sizeof(g_bridge[i]));
      g_bridge[i].used = 1;
      g_bridge[i].id = g_next_bridge_id++;
      g_bridge[i].touched = now;
      snprintf(g_bridge[i].handle, sizeof(g_bridge[i].handle), "%s", handle ? handle : "");
      return &g_bridge[i];
    }
  }
  return NULL;
}

static bool bridge_push_input(BridgeSession *bs, const char *line)
{
  int next = (bs->input_tail + 1) % BRIDGE_INPUT_MAX;
  if (next == bs->input_head)
    return false;
  snprintf(bs->input[bs->input_tail], sizeof(bs->input[bs->input_tail]), "%s", line ? line : "");
  bs->input_tail = next;
  bs->touched = time(NULL);
  return true;
}

static const char *query_value(const char *path, const char *key, char *out, size_t cap)
{
  const char *q = strchr(path, '?');
  if (!q) return NULL;
  q++;
  size_t key_len = strlen(key);
  while (*q)
  {
    if (!strncmp(q, key, key_len) && q[key_len] == '=')
    {
      q += key_len + 1;
      size_t n = 0;
      while (q[n] && q[n] != '&' && n + 1 < cap)
      {
        out[n] = q[n];
        n++;
      }
      out[n] = '\0';
      return out;
    }
    q = strchr(q, '&');
    if (!q) break;
    q++;
  }
  return NULL;
}

static void handle_bridge_action(int fd, BbsLibContext *ctx, BridgeSession *bs, const char *path, const char *body)
{
  char action[128] = "";
  char door_id[32] = "";
  query_value(path, "action", action, sizeof(action));
  query_value(path, "door_id", door_id, sizeof(door_id));
  if (!action[0] && body && body[0])
    snprintf(action, sizeof(action), "%s", body);

  BbsLibSessionAdapter adapter = {
      .write = bridge_write,
      .readline = bridge_readline,
      .user_data = bs,
  };
  BbsLibSessionOptions opts = {
      .handle = bs->handle[0] ? bs->handle : NULL,
      .ip = "mutineer-rest",
  };
  BbsLibSession *session = NULL;
  BbsLibResult r = bbslib_session_open(ctx, &opts, &adapter, &session);
  if (r == BBSLIB_OK)
  {
    if (door_id[0])
      r = bbslib_session_launch_door(session, atoi(door_id));
    else
      r = bbslib_session_run_action(session, action[0] ? action : "who");
    bbslib_session_close(session);
  }
  if (r != BBSLIB_OK)
  {
    char err[512];
    snprintf(err, sizeof(err), "{\"error\":\"%s\"}", bbslib_last_error(ctx));
    http_send(fd, r == BBSLIB_ERR_DENIED ? 401 : 400, "application/json", err);
    return;
  }
  http_send(fd, 200, "application/json", "{\"ok\":true}");
}

static void handle_request(int fd, BbsLibContext *ctx, char *req, const char *token)
{
  char method[16] = "";
  char path[512] = "";
  sscanf(req, "%15s %511s", method, path);
  char *body = body_start(req);

  if (!strcmp(method, "GET") && !strcmp(path, "/health"))
  {
    http_send(fd, 200, "application/json", "{\"ok\":true}");
    return;
  }
  if (!strcmp(method, "GET") && (!strcmp(path, "/api/status") || !strcmp(path, "/api/metrics")))
  {
    char json[RESP_MAX];
    if (bbslib_status_json(ctx, json, sizeof(json)) == BBSLIB_OK)
      http_send(fd, 200, "application/json", json);
    else
      http_send(fd, 500, "application/json", "{\"error\":\"status unavailable\"}");
    return;
  }
  if (!strcmp(method, "GET") && !strcmp(path, "/api/nodes"))
  {
    DbNode nodes[256];
    int n = bbslib_node_list(ctx, nodes, 256);
    char json[RESP_MAX];
    size_t o = (size_t)snprintf(json, sizeof(json), "{\"nodes\":[");
    for (int i = 0; i < n && o + 256 < sizeof(json); i++)
    {
      char handle[128], status[64], activity[160], ip[128];
      json_escape(handle, sizeof(handle), nodes[i].handle);
      json_escape(status, sizeof(status), nodes[i].status);
      json_escape(activity, sizeof(activity), nodes[i].activity);
      json_escape(ip, sizeof(ip), nodes[i].ip);
      o += (size_t)snprintf(json + o, sizeof(json) - o, "%s{\"node\":%d,\"user_id\":%d,\"handle\":\"%s\",\"status\":\"%s\",\"activity\":\"%s\",\"ip\":\"%s\"}",
                            i ? "," : "", nodes[i].node_num, nodes[i].user_id, handle, status, activity, ip);
    }
    snprintf(json + o, sizeof(json) - o, "]}");
    http_send(fd, 200, "application/json", json);
    return;
  }
  if (!strcmp(method, "GET") && !strcmp(path, "/api/messages/areas"))
  {
    DbMsgArea areas[256];
    int n = bbslib_msg_area_list(ctx, areas, 256);
    char json[RESP_MAX];
    size_t o = (size_t)snprintf(json, sizeof(json), "{\"areas\":[");
    for (int i = 0; i < n && o + 180 < sizeof(json); i++)
    {
      char name[128];
      json_escape(name, sizeof(name), areas[i].name);
      o += (size_t)snprintf(json + o, sizeof(json) - o, "%s{\"id\":%d,\"name\":\"%s\"}", i ? "," : "", areas[i].id, name);
    }
    snprintf(json + o, sizeof(json) - o, "]}");
    http_send(fd, 200, "application/json", json);
    return;
  }
  if (!strcmp(method, "GET") && !strcmp(path, "/api/files/areas"))
  {
    DbFileArea areas[256];
    int n = bbslib_file_area_list(ctx, areas, 256);
    char json[RESP_MAX];
    size_t o = (size_t)snprintf(json, sizeof(json), "{\"areas\":[");
    for (int i = 0; i < n && o + 220 < sizeof(json); i++)
    {
      char name[128], path_esc[512];
      json_escape(name, sizeof(name), areas[i].name);
      json_escape(path_esc, sizeof(path_esc), areas[i].path);
      o += (size_t)snprintf(json + o, sizeof(json) - o, "%s{\"id\":%d,\"name\":\"%s\",\"path\":\"%s\"}", i ? "," : "", areas[i].id, name, path_esc);
    }
    snprintf(json + o, sizeof(json) - o, "]}");
    http_send(fd, 200, "application/json", json);
    return;
  }
  if (!strcmp(method, "GET") && !strcmp(path, "/api/plank/status"))
  {
    BbsLibPlankStatus st;
    if (bbslib_plank_status(ctx, &st) != BBSLIB_OK)
    {
      http_send(fd, 500, "application/json", "{\"error\":\"plank unavailable\"}");
      return;
    }
    char node[128], net[128], addr[256], json[1024];
    json_escape(node, sizeof(node), st.node_name);
    json_escape(net, sizeof(net), st.network_name);
    json_escape(addr, sizeof(addr), st.node_addr);
    snprintf(json, sizeof(json), "{\"peers\":%d,\"links\":%d,\"areas\":%d,\"node_name\":\"%s\",\"network_name\":\"%s\",\"node_addr\":\"%s\"}",
             st.peers, st.links, st.areas, node, net, addr);
    http_send(fd, 200, "application/json", json);
    return;
  }

  if (!strncmp(path, "/api/bridge/", 12) || !strcmp(path, "/api/bridge/session"))
  {
    if (!authorized(req, token))
    {
      http_send(fd, 401, "application/json", "{\"error\":\"unauthorized\"}");
      return;
    }
    if (!strcmp(method, "POST") && !strncmp(path, "/api/bridge/session", 19) &&
        (path[19] == '\0' || path[19] == '?'))
    {
      char handle[64] = "";
      query_value(path, "handle", handle, sizeof(handle));
      BridgeSession *bs = bridge_create(handle[0] ? handle : NULL);
      if (!bs)
      {
        http_send(fd, 500, "application/json", "{\"error\":\"no bridge slots\"}");
        return;
      }
      char json[128];
      snprintf(json, sizeof(json), "{\"id\":%d}", bs->id);
      http_send(fd, 201, "application/json", json);
      return;
    }
    int id = 0;
    const char *idp = path + strlen("/api/bridge/session/");
    if (strncmp(path, "/api/bridge/session/", strlen("/api/bridge/session/")) == 0)
      id = atoi(idp);
    BridgeSession *bs = bridge_find(id);
    if (!bs)
    {
      http_send(fd, 404, "application/json", "{\"error\":\"bridge session not found\"}");
      return;
    }
    if (!strcmp(method, "POST") && strstr(path, "/input"))
    {
      if (!bridge_push_input(bs, body ? body : ""))
        http_send(fd, 400, "application/json", "{\"error\":\"input queue full\"}");
      else
        http_send(fd, 200, "application/json", "{\"ok\":true}");
      return;
    }
    if (!strcmp(method, "GET") && strstr(path, "/output"))
    {
      char escaped[BRIDGE_OUT_MAX * 2];
      json_escape(escaped, sizeof(escaped), bs->output);
      char json[RESP_MAX];
      snprintf(json, sizeof(json), "{\"id\":%d,\"output\":\"%s\"}", bs->id, escaped);
      http_send(fd, 200, "application/json", json);
      return;
    }
    if (!strcmp(method, "POST") && strstr(path, "/action"))
    {
      handle_bridge_action(fd, ctx, bs, path, body);
      return;
    }
  }

  http_send(fd, 404, "application/json", "{\"error\":\"not found\"}");
}

int main(int argc, char **argv)
{
  const char *config = "conf/mutineer.conf";
  const char *bind_addr = "127.0.0.1";
  int port = 8788;
  static struct option opts[] = {
      {"config", required_argument, 0, 'c'},
      {"bind", required_argument, 0, 'b'},
      {"port", required_argument, 0, 'p'},
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}};
  int ch;
  while ((ch = getopt_long(argc, argv, "c:b:p:h", opts, NULL)) != -1)
  {
    switch (ch)
    {
    case 'c': config = optarg; break;
    case 'b': bind_addr = optarg; break;
    case 'p': port = atoi(optarg); break;
    case 'h': usage(argv[0]); return 0;
    default: usage(argv[0]); return 1;
    }
  }

  BbsLibContext *ctx = NULL;
  BbsLibResult r = bbslib_open_path(config, &ctx);
  if (r != BBSLIB_OK)
  {
    fprintf(stderr, "mutineer-rest: %s\n", bbslib_result_string(r));
    return 2;
  }

  int srv = socket(AF_INET, SOCK_STREAM, 0);
  if (srv < 0)
  {
    perror("socket");
    bbslib_close(ctx);
    return 2;
  }
  int one = 1;
  setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons((uint16_t)port);
  if (inet_pton(AF_INET, bind_addr, &addr.sin_addr) != 1)
  {
    fprintf(stderr, "invalid bind address: %s\n", bind_addr);
    close(srv);
    bbslib_close(ctx);
    return 2;
  }
  if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) != 0 || listen(srv, 16) != 0)
  {
    perror("bind/listen");
    close(srv);
    bbslib_close(ctx);
    return 2;
  }

  signal(SIGINT, on_signal);
  signal(SIGTERM, on_signal);
  const char *token = getenv("MUTINEER_REST_TOKEN");
  printf("mutineer-rest listening on http://%s:%d\n", bind_addr, port);
  while (!g_stop)
  {
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(srv, &rfds);
    struct timeval tv = {.tv_sec = 1, .tv_usec = 0};
    int ready = select(srv + 1, &rfds, NULL, NULL, &tv);
    if (ready < 0)
    {
      if (errno == EINTR) continue;
      break;
    }
    if (ready == 0)
      continue;
    int cfd = accept(srv, NULL, NULL);
    if (cfd < 0)
    {
      if (errno == EINTR) continue;
      break;
    }
    char req[REQ_MAX];
    ssize_t n = recv(cfd, req, sizeof(req) - 1, 0);
    if (n > 0)
    {
      req[n] = '\0';
      handle_request(cfd, ctx, req, token);
    }
    close(cfd);
  }
  close(srv);
  bbslib_close(ctx);
  return 0;
}
