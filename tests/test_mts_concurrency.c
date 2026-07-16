#include "mts.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define WORKERS 8
typedef struct {
  mts_session_t *session;
  int operations;
} worker_arg_t;

static void *worker(void *opaque) {
  worker_arg_t *arg = opaque;
  mts_session_t *s = arg->session;
  for (int i = 0; i < arg->operations; i++) {
    uint64_t room = (i & 1) ? 1 : 2;
    if (!mts_join(s, room))
      abort();
    mts_publish(s, MTS_PUBLIC, room, 0, NULL, "parallel public");
    mts_publish(s, MTS_DIRECTED, room, (uint32_t)((s->user_id % WORKERS) + 1),
                "peer", "parallel directed");
    mts_publish(s, MTS_PRIVATE, 0, (uint32_t)((s->user_id % WORKERS) + 1),
                "peer", "parallel private");
    /* Keep moderation in the sustained operation mix without making SQLite
       transaction latency dominate the topology and delivery stress. */
    if ((i % 1000) == 0) {
      uint32_t target = (uint32_t)((s->user_id % WORKERS) + 1);
      mts_store_sanction(&MTS, "mts_room_mutes", room, target, s->user_id,
                         "parallel test", mts_now() + 60, 1);
      mts_store_sanction(&MTS, "mts_room_mutes", room, target, s->user_id, "",
                         0, 0);
    }
  }
  return NULL;
}

int main(void) {
  int rounds =
      getenv("MTS_STRESS_ROUNDS") ? atoi(getenv("MTS_STRESS_ROUNDS")) : 1;
  int operations =
      getenv("MTS_STRESS_OPS") ? atoi(getenv("MTS_STRESS_OPS")) : 20;
  if (rounds < 1 || operations < 1)
    return 2;
  char dir[] = "/tmp/mts-concurrency-XXXXXX", error[256] = {0};
  if (!mkdtemp(dir))
    return 1;
  mts_config_t cfg;
  if (!mts_config_load(&cfg, "/missing", error, sizeof error) ||
      !mts_state_init(dir, &cfg, error, sizeof error))
    return 1;
  sqlite3_exec(MTS.db, "PRAGMA synchronous=OFF", NULL, NULL, NULL);
  MTS.skip_join_sanction_reads = 1;
  MTS.rooms[1] = (mts_room_t){.id = 2, .active = 1, .permanent = 1};
  snprintf(MTS.rooms[1].name, sizeof MTS.rooms[1].name, "Parallel");
  snprintf(MTS.rooms[1].normalized, sizeof MTS.rooms[1].normalized, "parallel");
  MTS.rooms[1].history_capacity = (size_t)cfg.history_per_room;
  MTS.rooms[1].history =
      calloc(MTS.rooms[1].history_capacity, sizeof(mts_event_t));
  mts_session_t sessions[WORKERS] = {0};
  worker_arg_t args[WORKERS];
  pthread_t threads[WORKERS];
  for (int i = 0; i < WORKERS; i++) {
    sessions[i].user_id = (uint32_t)(i + 1);
    sessions[i].node = i + 1;
    /* The stress workers exercise moderation concurrently, but bypass the
       per-move authorization SELECTs so the profile measures MTS locking and
       delivery rather than millions of identical SQLite reads. */
    sessions[i].flags = 1u;
    sessions[i].room_id = 1;
    sessions[i].active = 1;
    sessions[i].presence_registered = 1;
    snprintf(sessions[i].handle, sizeof sessions[i].handle, "worker%d", i + 1);
    pthread_mutex_init(&sessions[i].inbox_mu, NULL);
    MTS.sessions[MTS.session_count++] = &sessions[i];
    args[i] = (worker_arg_t){&sessions[i], rounds * operations};
  }
  for (int i = 0; i < WORKERS; i++)
    if (pthread_create(&threads[i], NULL, worker, &args[i]))
      return 1;
  for (int i = 0; i < WORKERS; i++)
    pthread_join(threads[i], NULL);
  uint64_t last = 0;
  for (int i = 0; i < WORKERS; i++) {
    pthread_mutex_lock(&sessions[i].inbox_mu);
    if (sessions[i].count > MTS_INBOX)
      return 1;
    for (size_t j = 0; j < sessions[i].count; j++) {
      mts_event_t *e = &sessions[i].inbox[(sessions[i].head + j) % MTS_INBOX];
      if (j && e->sequence <= last)
        return 1;
      last = e->sequence;
    }
    pthread_mutex_unlock(&sessions[i].inbox_mu);
    pthread_mutex_destroy(&sessions[i].inbox_mu);
    last = 0;
  }
  if (MTS.rooms[0].id != 1 || !MTS.rooms[0].permanent ||
      MTS.rooms[0].history_count > MTS.rooms[0].history_capacity ||
      MTS.rooms[1].history_count > MTS.rooms[1].history_capacity)
    return 1;
  MTS.session_count = 0;
  mts_state_shutdown();
  char path[768];
  snprintf(path, sizeof path, "%s/mts.db", dir);
  unlink(path);
  snprintf(path, sizeof path, "%s/mts.db-wal", dir);
  unlink(path);
  snprintf(path, sizeof path, "%s/mts.db-shm", dir);
  unlink(path);
  rmdir(dir);
  puts("MTS concurrency tests passed");
  return 0;
}
