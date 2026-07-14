/*
 * PLANK Storage Layer Implementation
 * Database operations using Mutineer BBS SQLite database
 */

#include "plank/plank_store.h"
#include "plank/plank.h"
#include "bbs_db.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef HAVE_SQLITE
#include <sqlite3.h>
#endif

/* Forward declarations */
static void escape_sql_string(char *out, size_t out_size, const char *in);

/* ============================================================================
 * STORE CONTEXT
 * ============================================================================ */

struct plank_store
{
    BbsDb *db;
#ifdef HAVE_SQLITE
    sqlite3 *sqlite;
#endif
    bool owns_db;
};

/* ============================================================================
 * STORE LIFECYCLE
 * ============================================================================ */

plank_store_t *plank_store_open(BbsDb *db)
{
    if (!db)
    {
        plank_set_error("NULL database handle");
        return NULL;
    }

    plank_store_t *store = calloc(1, sizeof(plank_store_t));
    if (!store)
    {
        plank_set_error("Failed to allocate store");
        return NULL;
    }

    store->db = db;
    store->owns_db = false;

#ifdef HAVE_SQLITE
    /* Get underlying sqlite3 handle - this depends on BbsDb internals */
    /* For now, we'll use db_exec for queries */
    store->sqlite = NULL;
#endif

    return store;
}

void plank_store_close(plank_store_t *store)
{
    if (!store)
        return;

    if (store->owns_db && store->db)
    {
        db_close(store->db);
    }

    free(store);
}

BbsDb *plank_store_get_db(plank_store_t *store)
{
    return store ? store->db : NULL;
}

bool plank_store_init_schema(plank_store_t *store, const char *schema_path)
{
    if (!store || !store->db)
    {
        plank_set_error("Invalid store");
        return false;
    }

    /* Read and execute schema file */
    FILE *f = fopen(schema_path, "r");
    if (!f)
    {
        plank_set_error("Failed to open schema file: %s", schema_path);
        return false;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *sql = malloc(size + 1);
    if (!sql)
    {
        fclose(f);
        plank_set_error("Failed to allocate schema buffer");
        return false;
    }

    size_t read = fread(sql, 1, size, f);
    fclose(f);
    sql[read] = '\0';

    bool result = db_exec(store->db, sql);
    free(sql);

    if (!result)
    {
        plank_set_error("Failed to execute schema: %s", db_last_error(store->db));
    }

    return result;
}

/* ============================================================================
 * NODE IDENTITY
 * ============================================================================ */

struct identity_query_ctx
{
    plank_node_identity_t *out;
    bool found;
};

#ifdef HAVE_SQLITE
static bool identity_query_row(void *row, void *ctx_ptr)
{
    sqlite3_stmt *st = (sqlite3_stmt *)row;
    struct identity_query_ctx *ctx = (struct identity_query_ctx *)ctx_ptr;
    plank_node_identity_t *result = ctx->out;
    memset(result, 0, sizeof(*result));

    const void *blob = sqlite3_column_blob(st, 0);
    int blob_size = sqlite3_column_bytes(st, 0);
    if (blob && blob_size == PLANK_NODE_ID_SIZE)
    {
        memcpy(result->node_id, blob, PLANK_NODE_ID_SIZE);
    }

    const unsigned char *text = sqlite3_column_text(st, 1);
    if (text)
        snprintf(result->node_name, sizeof(result->node_name), "%s", text);

    text = sqlite3_column_text(st, 2);
    if (text)
        snprintf(result->network_name, sizeof(result->network_name), "%s", text);

    blob = sqlite3_column_blob(st, 3);
    blob_size = sqlite3_column_bytes(st, 3);
    if (blob && blob_size == PLANK_PUBKEY_SIZE)
    {
        memcpy(result->signing_key_pub, blob, PLANK_PUBKEY_SIZE);
    }

    blob = sqlite3_column_blob(st, 4);
    blob_size = sqlite3_column_bytes(st, 4);
    if (blob && blob_size == PLANK_PRIVKEY_SIZE)
    {
        memcpy(result->signing_key_priv, blob, PLANK_PRIVKEY_SIZE);
    }

    blob = sqlite3_column_blob(st, 5);
    blob_size = sqlite3_column_bytes(st, 5);
    if (blob && blob_size == PLANK_PUBKEY_SIZE)
    {
        memcpy(result->link_key_pub, blob, PLANK_PUBKEY_SIZE);
    }

    blob = sqlite3_column_blob(st, 6);
    blob_size = sqlite3_column_bytes(st, 6);
    if (blob && blob_size == PLANK_PRIVKEY_SIZE)
    {
        memcpy(result->link_key_priv, blob, PLANK_PRIVKEY_SIZE);
    }

    text = sqlite3_column_text(st, 7);
    if (text)
        snprintf(result->software_name, sizeof(result->software_name), "%s", text);

    text = sqlite3_column_text(st, 8);
    if (text)
        snprintf(result->software_version, sizeof(result->software_version), "%s", text);

    result->is_cove = sqlite3_column_int(st, 9) != 0;
    result->max_bundle_bytes = (uint32_t)sqlite3_column_int(st, 10);
    result->max_frame_bytes = (uint32_t)sqlite3_column_int(st, 11);
    result->poll_interval_sec = (uint32_t)sqlite3_column_int(st, 12);

    /* node_addr is derived from node_name@network_name, not stored separately */
    plank_format_node_addr(result->node_name, result->network_name,
                           result->node_addr, sizeof(result->node_addr));

    ctx->found = true;
    return false;
}
#endif

bool plank_store_get_identity(plank_store_t *store, plank_node_identity_t *out)
{
    if (!store || !out)
        return false;

    struct identity_query_ctx ctx;
    ctx.out = out;
    ctx.found = false;

    const char *sql =
        "SELECT node_id, node_name, network_name, signing_key_pub, signing_key_priv, "
        "link_key_pub, link_key_priv, software_name, software_version, is_cove, "
        "max_bundle_bytes, max_frame_bytes, poll_interval_sec "
        "FROM plank_node_identity WHERE id = 1";

#ifdef HAVE_SQLITE
    if (!db_query(store->db, sql, identity_query_row, &ctx))
    {
        return false;
    }
    return ctx.found;
#else
    (void)store;
    (void)out;
    return false;
#endif
}

bool plank_store_set_identity(plank_store_t *store, const plank_node_identity_t *identity)
{
    if (!store || !identity)
        return false;

    const char *sql =
        "INSERT OR REPLACE INTO plank_node_identity "
        "(id, node_id, node_name, network_name, signing_key_pub, signing_key_priv, "
        "software_name, software_version, is_cove, max_bundle_bytes, max_frame_bytes, "
        "poll_interval_sec, updated_at) VALUES "
        "(1, ?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, datetime('now'))";
    DbBind binds[] = {
        DB_BIND_BLOB_VAL(identity->node_id, PLANK_NODE_ID_SIZE),
        DB_BIND_TEXT_VAL(identity->node_name),
        DB_BIND_TEXT_VAL(identity->network_name),
        DB_BIND_BLOB_VAL(identity->signing_key_pub, PLANK_PUBKEY_SIZE),
        DB_BIND_BLOB_VAL(identity->signing_key_priv, PLANK_PRIVKEY_SIZE),
        DB_BIND_TEXT_VAL(identity->software_name),
        DB_BIND_TEXT_VAL(identity->software_version),
        DB_BIND_INT_VAL(identity->is_cove ? 1 : 0),
        DB_BIND_INT64_VAL(identity->max_bundle_bytes),
        DB_BIND_INT64_VAL(identity->max_frame_bytes),
        DB_BIND_INT64_VAL(identity->poll_interval_sec),
    };

    if (!db_exec_prepared(store->db, sql, binds, (int)(sizeof(binds) / sizeof(binds[0]))))
    {
        plank_set_error("Failed to set identity: %s", db_last_error(store->db));
        return false;
    }

    return true;
}

bool plank_store_generate_identity(plank_store_t *store,
                                   const char *node_name,
                                   const char *network_name)
{
    if (!store || !node_name || !network_name)
    {
        plank_set_error("Invalid arguments");
        return false;
    }

    plank_node_identity_t identity;
    memset(&identity, 0, sizeof(identity));

    /* Generate node ID */
    if (!plank_crypto_random_node_id(identity.node_id))
    {
        plank_set_error("Failed to generate node ID");
        return false;
    }

    /* Generate signing key pair */
    if (!plank_crypto_keygen_ed25519(identity.signing_key_pub, identity.signing_key_priv))
    {
        plank_set_error("Failed to generate signing key");
        return false;
    }

    /* Generate link key pair */
    if (!plank_crypto_keygen_x25519(identity.link_key_pub, identity.link_key_priv))
    {
        plank_set_error("Failed to generate link key");
        return false;
    }

    strncpy(identity.node_name, node_name, sizeof(identity.node_name) - 1);
    strncpy(identity.network_name, network_name, sizeof(identity.network_name) - 1);

    /* Format node address */
    plank_format_node_addr(node_name, network_name,
                           identity.node_addr, sizeof(identity.node_addr));

    strncpy(identity.software_name, "Mutineer", sizeof(identity.software_name) - 1);
    strncpy(identity.software_version, PLANK_VERSION_STRING, sizeof(identity.software_version) - 1);

    identity.is_cove = false;
    identity.max_bundle_bytes = PLANK_DEFAULT_MAX_BUNDLE_SIZE;
    identity.max_frame_bytes = PLANK_DEFAULT_MAX_FRAME_SIZE;
    identity.poll_interval_sec = 300;

    return plank_store_set_identity(store, &identity);
}

/* ============================================================================
 * PEER MANAGEMENT
 * ============================================================================ */

bool plank_store_peer_upsert(plank_store_t *store, const plank_peer_t *peer)
{
    if (!store || !peer)
        return false;

    const char *sql =
        "INSERT INTO plank_peers "
        "(node_id, node_name, network_name, node_addr, signing_key_pub, "
        "tls_fingerprint, trust_level, status, notes) VALUES "
        "(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9) "
        "ON CONFLICT(node_id) DO UPDATE SET "
        "node_name = excluded.node_name, network_name = excluded.network_name, "
        "node_addr = excluded.node_addr, signing_key_pub = excluded.signing_key_pub, "
        "tls_fingerprint = excluded.tls_fingerprint, trust_level = excluded.trust_level, "
        "status = excluded.status, notes = excluded.notes";
    DbBind binds[] = {
        DB_BIND_BLOB_VAL(peer->node_id, PLANK_NODE_ID_SIZE),
        DB_BIND_TEXT_VAL(peer->node_name),
        DB_BIND_TEXT_VAL(peer->network_name),
        DB_BIND_TEXT_VAL(peer->node_addr),
        DB_BIND_BLOB_VAL(peer->signing_key_pub, PLANK_PUBKEY_SIZE),
        DB_BIND_TEXT_VAL(peer->tls_fingerprint),
        DB_BIND_INT_VAL(peer->trust_level),
        DB_BIND_INT_VAL(peer->status),
        DB_BIND_TEXT_VAL(peer->notes),
    };

    if (!db_exec_prepared(store->db, sql, binds, (int)(sizeof(binds) / sizeof(binds[0]))))
    {
        plank_set_error("Failed to upsert peer: %s", db_last_error(store->db));
        return false;
    }

    return true;
}

/* ============================================================================
 * LINK MANAGEMENT
 * ============================================================================ */

bool plank_store_link_add(plank_store_t *store, const plank_link_t *link, int *id_out)
{
    if (!store || !link)
        return false;

    const char *sql =
        "INSERT INTO plank_links "
        "(link_id, link_name, peer_id, remote_host, remote_port, direction, "
        "enabled, paused, retry_initial_sec, retry_max_sec, retry_limit, state) VALUES "
        "(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12)";
    DbBind binds[] = {
        DB_BIND_BLOB_VAL(link->link_id, PLANK_LINK_ID_SIZE),
        DB_BIND_TEXT_VAL(link->link_name),
        DB_BIND_INT_VAL(link->peer_id),
        DB_BIND_TEXT_VAL(link->remote_host),
        DB_BIND_INT_VAL(link->remote_port),
        DB_BIND_INT_VAL(link->direction),
        DB_BIND_INT_VAL(link->enabled ? 1 : 0),
        DB_BIND_INT_VAL(link->paused ? 1 : 0),
        DB_BIND_INT_VAL(link->retry_initial_sec),
        DB_BIND_INT_VAL(link->retry_max_sec),
        DB_BIND_INT_VAL(link->retry_limit),
        DB_BIND_INT_VAL(link->state),
    };

    if (!db_exec_prepared(store->db, sql, binds, (int)(sizeof(binds) / sizeof(binds[0]))))
    {
        plank_set_error("Failed to add link: %s", db_last_error(store->db));
        return false;
    }

    if (id_out)
    {
        *id_out = db_last_insert_id(store->db);
    }

    return true;
}

bool plank_store_link_set_state(plank_store_t *store, int link_id, plank_link_state_t state)
{
    if (!store)
        return false;

    DbBind binds[] = {
        DB_BIND_INT_VAL(state),
        DB_BIND_INT_VAL(link_id),
    };
    return db_exec_prepared(store->db,
                            "UPDATE plank_links SET state = ?1, updated_at = datetime('now') WHERE id = ?2",
                            binds, 2);
}

bool plank_store_link_set_error(plank_store_t *store, int link_id, const char *error)
{
    if (!store)
        return false;

    DbBind binds[] = {
        DB_BIND_TEXT_VAL(error ? error : ""),
        DB_BIND_INT_VAL(link_id),
    };
    return db_exec_prepared(store->db,
                            "UPDATE plank_links SET last_error = ?1, updated_at = datetime('now') WHERE id = ?2",
                            binds, 2);
}

/* ============================================================================
 * LINK CURSORS
 * ============================================================================ */

bool plank_store_cursor_set_export(plank_store_t *store, int link_id, uint64_t seq)
{
    if (!store)
        return false;

    char sql[256];
    snprintf(sql, sizeof(sql),
             "INSERT INTO plank_link_cursors (link_id, direction, journal_seq, updated_at) "
             "VALUES (%d, 1, %llu, datetime('now')) "
             "ON CONFLICT(link_id, direction) DO UPDATE SET "
             "journal_seq = excluded.journal_seq, updated_at = excluded.updated_at",
             link_id, (unsigned long long)seq);

    return db_exec(store->db, sql);
}

bool plank_store_cursor_set_import(plank_store_t *store, int link_id, uint64_t seq)
{
    if (!store)
        return false;

    char sql[256];
    snprintf(sql, sizeof(sql),
             "INSERT INTO plank_link_cursors (link_id, direction, journal_seq, updated_at) "
             "VALUES (%d, 2, %llu, datetime('now')) "
             "ON CONFLICT(link_id, direction) DO UPDATE SET "
             "journal_seq = excluded.journal_seq, updated_at = excluded.updated_at",
             link_id, (unsigned long long)seq);

    return db_exec(store->db, sql);
}

/* ============================================================================
 * AREA MANAGEMENT
 * ============================================================================ */

bool plank_store_area_upsert(plank_store_t *store, const plank_area_t *area, int *id_out)
{
    if (!store || !area)
        return false;

    const char *sql =
        "INSERT INTO plank_areas "
        "(area_addr, area_slug, origin_node_addr, title, description, "
        "distribution_mode, default_retention, posting_policy, attachment_policy, "
        "max_message_bytes, max_attachment_bytes, retention_days, max_hops, status) VALUES "
        "(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14) "
        "ON CONFLICT(area_addr) DO UPDATE SET "
        "title = excluded.title, description = excluded.description, "
        "distribution_mode = excluded.distribution_mode, "
        "default_retention = excluded.default_retention, "
        "posting_policy = excluded.posting_policy, "
        "attachment_policy = excluded.attachment_policy, "
        "max_message_bytes = excluded.max_message_bytes, "
        "max_attachment_bytes = excluded.max_attachment_bytes, "
        "retention_days = excluded.retention_days, "
        "max_hops = excluded.max_hops, status = excluded.status, "
        "updated_at = datetime('now')";
    DbBind binds[] = {
        DB_BIND_TEXT_VAL(area->area_addr),
        DB_BIND_TEXT_VAL(area->area_slug),
        DB_BIND_TEXT_VAL(area->origin_node_addr),
        DB_BIND_TEXT_VAL(area->title),
        DB_BIND_TEXT_VAL(area->description),
        DB_BIND_INT_VAL(area->distribution_mode),
        DB_BIND_INT_VAL(area->default_retention),
        DB_BIND_INT_VAL(area->posting_policy),
        DB_BIND_INT_VAL(area->attachment_policy),
        DB_BIND_INT64_VAL(area->max_message_bytes),
        DB_BIND_INT64_VAL(area->max_attachment_bytes),
        DB_BIND_INT64_VAL(area->retention_days),
        DB_BIND_INT64_VAL(area->max_hops),
        DB_BIND_INT_VAL(area->status),
    };

    if (!db_exec_prepared(store->db, sql, binds, (int)(sizeof(binds) / sizeof(binds[0]))))
    {
        plank_set_error("Failed to upsert area: %s", db_last_error(store->db));
        return false;
    }

    if (id_out)
    {
        *id_out = db_last_insert_id(store->db);
    }

    return true;
}

/* ============================================================================
 * OBJECT STORAGE
 * ============================================================================ */

bool plank_store_object_put(plank_store_t *store, const plank_object_t *obj,
                            plank_source_kind_t source, int source_link_id,
                            int64_t *local_seq_out)
{
    if (!store || !obj)
        return false;

    /* Check for duplicate */
    char obj_id_hex[65];
    plank_crypto_to_hex(obj->object_id, PLANK_OBJECT_ID_SIZE, obj_id_hex);

    if (plank_store_object_exists(store, obj->object_id))
    {
        plank_set_error("Object already exists");
        return false;
    }

    const char *sql =
        "INSERT INTO plank_objects "
        "(object_id, object_class, origin_node_id, origin_node_addr, created_at_ts, "
        "signature, sig_alg, body_cbor, envelope_cbor, verified) VALUES "
        "(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, 1)";
    DbBind binds[] = {
        DB_BIND_BLOB_VAL(obj->object_id, PLANK_OBJECT_ID_SIZE),
        DB_BIND_INT_VAL(obj->object_class),
        DB_BIND_BLOB_VAL(obj->origin_node_id, PLANK_NODE_ID_SIZE),
        DB_BIND_TEXT_VAL(obj->origin_node_addr),
        DB_BIND_INT64_VAL(obj->created_at),
        DB_BIND_BLOB_VAL(obj->signature, PLANK_SIGNATURE_SIZE),
        DB_BIND_INT_VAL(obj->sig_alg),
        DB_BIND_BLOB_VAL(obj->body_cbor, obj->body_cbor_len),
        DB_BIND_BLOB_VAL(obj->envelope_cbor, obj->envelope_cbor_len),
    };

    if (!db_exec_prepared(store->db, sql, binds, (int)(sizeof(binds) / sizeof(binds[0]))))
    {
        plank_set_error("Failed to store object: %s", db_last_error(store->db));
        return false;
    }

    /* Add journal entry */
    int obj_db_id = db_last_insert_id(store->db);

    bool journal_ok = false;
    if (source_link_id > 0)
    {
        DbBind journal_binds[] = {
            DB_BIND_BLOB_VAL(obj->object_id, PLANK_OBJECT_ID_SIZE),
            DB_BIND_INT_VAL(obj->object_class),
            DB_BIND_INT_VAL(source),
            DB_BIND_INT_VAL(source_link_id),
        };
        journal_ok = db_exec_prepared(store->db,
                                      "INSERT INTO plank_journal "
                                      "(object_id, object_class, source_kind, source_link_id, processing_state) VALUES "
                                      "(?1, ?2, ?3, ?4, 0)",
                                      journal_binds, 4);
    }
    else
    {
        DbBind journal_binds[] = {
            DB_BIND_BLOB_VAL(obj->object_id, PLANK_OBJECT_ID_SIZE),
            DB_BIND_INT_VAL(obj->object_class),
            DB_BIND_INT_VAL(source),
        };
        journal_ok = db_exec_prepared(store->db,
                                      "INSERT INTO plank_journal "
                                      "(object_id, object_class, source_kind, processing_state) VALUES "
                                      "(?1, ?2, ?3, 0)",
                                      journal_binds, 3);
    }
    if (!journal_ok)
    {
        plank_set_error("Failed to journal object: %s", db_last_error(store->db));
        return false;
    }

    if (local_seq_out)
    {
        *local_seq_out = db_last_insert_id(store->db);
    }

    (void)obj_db_id;
    return true;
}

static bool object_exists_row(void *row, void *ctx_ptr)
{
    (void)row;
    bool *found = (bool *)ctx_ptr;
    *found = true;
    return false;
}

bool plank_store_object_exists(plank_store_t *store, const uint8_t *object_id)
{
    if (!store || !object_id)
        return false;

    char obj_id_hex[65];
    plank_crypto_to_hex(object_id, PLANK_OBJECT_ID_SIZE, obj_id_hex);

    char sql[256];
    snprintf(sql, sizeof(sql),
             "SELECT 1 FROM plank_objects WHERE object_id = X'%s' LIMIT 1", obj_id_hex);

    bool found = false;
#ifdef HAVE_SQLITE
    if (!db_query(store->db, sql, object_exists_row, &found))
    {
        return false;
    }
#endif
    return found;
}

/* ============================================================================
 * IMPORT HISTORY
 * ============================================================================ */

bool plank_store_import_record(plank_store_t *store,
                               const uint8_t *bundle_id,
                               plank_bundle_type_t bundle_type,
                               const uint8_t *source_node_id,
                               const char *source_node_addr,
                               int source_link_id,
                               plank_import_result_t result,
                               int object_count,
                               int duplicate_count,
                               int rejected_count)
{
    if (!store || !bundle_id || !source_node_id)
        return false;

    const char *sql =
        "INSERT INTO plank_import_history "
        "(bundle_id, bundle_type, source_node_id, source_node_addr, import_result, "
        "source_link_id, object_count, duplicate_count, rejected_count) VALUES "
        "(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9) "
        "ON CONFLICT(bundle_id) DO UPDATE SET "
        "last_seen_at = datetime('now')";
    DbBind binds[] = {
        DB_BIND_BLOB_VAL(bundle_id, PLANK_BUNDLE_ID_SIZE),
        DB_BIND_INT_VAL(bundle_type),
        DB_BIND_BLOB_VAL(source_node_id, PLANK_NODE_ID_SIZE),
        DB_BIND_TEXT_VAL(source_node_addr ? source_node_addr : ""),
        DB_BIND_INT_VAL(result),
        source_link_id > 0 ? DB_BIND_INT_VAL(source_link_id) : DB_BIND_NULL_VAL,
        DB_BIND_INT_VAL(object_count),
        DB_BIND_INT_VAL(duplicate_count),
        DB_BIND_INT_VAL(rejected_count),
    };

    return db_exec_prepared(store->db, sql, binds, (int)(sizeof(binds) / sizeof(binds[0])));
}

bool plank_store_import_exists(plank_store_t *store, const uint8_t *bundle_id)
{
    if (!store || !bundle_id)
        return false;

    char bundle_id_hex[65];
    plank_crypto_to_hex(bundle_id, PLANK_BUNDLE_ID_SIZE, bundle_id_hex);

    char sql[256];
    snprintf(sql, sizeof(sql),
             "SELECT 1 FROM plank_import_history WHERE bundle_id = X'%s' LIMIT 1",
             bundle_id_hex);

    bool found = false;
#ifdef HAVE_SQLITE
    if (!db_query(store->db, sql, object_exists_row, &found))
    {
        return false;
    }
#endif
    return found;
}

/* ============================================================================
 * DEDUPLICATION
 * ============================================================================ */

bool plank_store_dedupe_exists(plank_store_t *store, const uint8_t *object_id)
{
    if (!store || !object_id)
        return false;

    char obj_id_hex[65];
    plank_crypto_to_hex(object_id, PLANK_OBJECT_ID_SIZE, obj_id_hex);

    char sql[256];
    snprintf(sql, sizeof(sql),
             "SELECT 1 FROM plank_dedupe WHERE object_id = X'%s' LIMIT 1",
             obj_id_hex);

    bool found = false;
#ifdef HAVE_SQLITE
    if (!db_query(store->db, sql, object_exists_row, &found))
    {
        return false;
    }
#endif
    return found;
}

bool plank_store_dedupe_record(plank_store_t *store, const uint8_t *object_id)
{
    if (!store || !object_id)
        return false;

    char obj_id_hex[65];
    plank_crypto_to_hex(object_id, PLANK_OBJECT_ID_SIZE, obj_id_hex);

    char sql[256];
    snprintf(sql, sizeof(sql),
             "INSERT INTO plank_dedupe (object_id, seen_count) VALUES (X'%s', 1) "
             "ON CONFLICT(object_id) DO UPDATE SET "
             "last_seen_at = datetime('now'), seen_count = seen_count + 1",
             obj_id_hex);

    return db_exec(store->db, sql);
}

int plank_store_dedupe_prune(plank_store_t *store, int days_old)
{
    if (!store)
        return 0;

    char sql[256];
    snprintf(sql, sizeof(sql),
             "DELETE FROM plank_dedupe WHERE first_seen_at < datetime('now', '-%d days')",
             days_old);

    return db_exec_simple(store->db, sql);
}

/* ============================================================================
 * DEAD LETTERS
 * ============================================================================ */

bool plank_store_deadletter_add(plank_store_t *store,
                                int target_link_id,
                                const char *target_node_addr,
                                const uint8_t **object_ids,
                                int object_count,
                                const uint8_t *bundle_id,
                                int error_code,
                                const char *error_text)
{
    if (!store)
        return false;

    if (object_count < 0 || object_count > 100 || (object_count > 0 && !object_ids))
    {
        plank_set_error("Invalid deadletter object count");
        return false;
    }

    size_t obj_ids_json_cap = 3 + (size_t)object_count * 68;
    char *obj_ids_json = malloc(obj_ids_json_cap);
    if (!obj_ids_json)
    {
        plank_set_error("Failed to allocate deadletter object list");
        return false;
    }

    size_t off = 0;
    obj_ids_json[off++] = '[';
    for (int i = 0; i < object_count; i++)
    {
        if (!object_ids[i])
        {
            free(obj_ids_json);
            plank_set_error("Invalid deadletter object id");
            return false;
        }
        char hex[65];
        plank_crypto_to_hex(object_ids[i], PLANK_OBJECT_ID_SIZE, hex);
        if (i > 0)
            obj_ids_json[off++] = ',';
        obj_ids_json[off++] = '"';
        memcpy(obj_ids_json + off, hex, 64);
        off += 64;
        obj_ids_json[off++] = '"';
    }
    obj_ids_json[off++] = ']';
    obj_ids_json[off] = '\0';

    const char *sql_with_bundle =
        "INSERT INTO plank_deadletters "
        "(target_link_id, target_node_addr, object_ids, bundle_id, "
        "last_error_code, last_error_text, state) VALUES "
        "(?1, ?2, ?3, ?4, ?5, ?6, 0)";
    const char *sql_without_bundle =
        "INSERT INTO plank_deadletters "
        "(target_link_id, target_node_addr, object_ids, "
        "last_error_code, last_error_text, state) VALUES "
        "(?1, ?2, ?3, ?4, ?5, 0)";
    bool ok;
    if (bundle_id)
    {
        DbBind binds[] = {
            DB_BIND_INT_VAL(target_link_id),
            DB_BIND_TEXT_VAL(target_node_addr ? target_node_addr : ""),
            DB_BIND_TEXT_VAL(obj_ids_json),
            DB_BIND_BLOB_VAL(bundle_id, PLANK_BUNDLE_ID_SIZE),
            DB_BIND_INT_VAL(error_code),
            error_text ? DB_BIND_TEXT_VAL(error_text) : DB_BIND_NULL_VAL,
        };
        ok = db_exec_prepared(store->db, sql_with_bundle, binds, 6);
    }
    else
    {
        DbBind binds[] = {
            DB_BIND_INT_VAL(target_link_id),
            DB_BIND_TEXT_VAL(target_node_addr ? target_node_addr : ""),
            DB_BIND_TEXT_VAL(obj_ids_json),
            DB_BIND_INT_VAL(error_code),
            error_text ? DB_BIND_TEXT_VAL(error_text) : DB_BIND_NULL_VAL,
        };
        ok = db_exec_prepared(store->db, sql_without_bundle, binds, 5);
    }

    free(obj_ids_json);
    return ok;
}

bool plank_store_deadletter_requeue(plank_store_t *store, int id)
{
    if (!store || id <= 0)
        return false;

    char sql[256];
    snprintf(sql, sizeof(sql),
             "UPDATE plank_deadletters SET state = 1, retry_count = retry_count + 1, "
             "last_failure_at = datetime('now') WHERE id = %d",
             id);

    return db_exec(store->db, sql);
}

bool plank_store_deadletter_abandon(plank_store_t *store, int id)
{
    if (!store || id <= 0)
        return false;

    char sql[256];
    snprintf(sql, sizeof(sql),
             "UPDATE plank_deadletters SET state = 2 WHERE id = %d",
             id);

    return db_exec(store->db, sql);
}

/* ============================================================================
 * QUARANTINE
 * ============================================================================ */

bool plank_store_quarantine_add(plank_store_t *store,
                                const uint8_t *object_id,
                                plank_object_class_t object_class,
                                int source_link_id,
                                const char *source_node_addr,
                                plank_quarantine_reason_t reason,
                                const char *reason_text,
                                const uint8_t *envelope_cbor,
                                size_t envelope_cbor_len)
{
    if (!store || !object_id || !envelope_cbor)
        return false;

    const char *sql =
        "INSERT INTO plank_quarantine "
        "(object_id, object_class, source_link_id, source_node_addr, "
        "quarantine_reason, quarantine_text, envelope_cbor) VALUES "
        "(?1, ?2, ?3, ?4, ?5, ?6, ?7)";
    DbBind binds[] = {
        DB_BIND_BLOB_VAL(object_id, PLANK_OBJECT_ID_SIZE),
        DB_BIND_INT_VAL(object_class),
        DB_BIND_INT_VAL(source_link_id),
        DB_BIND_TEXT_VAL(source_node_addr ? source_node_addr : ""),
        DB_BIND_INT_VAL(reason),
        DB_BIND_TEXT_VAL(reason_text ? reason_text : ""),
        DB_BIND_BLOB_VAL(envelope_cbor, envelope_cbor_len),
    };

    return db_exec_prepared(store->db, sql, binds, (int)(sizeof(binds) / sizeof(binds[0])));
}

/* ============================================================================
 * USER EXPORTS
 * ============================================================================ */

bool plank_store_user_export_create(plank_store_t *store, int user_id,
                                    const uint8_t *export_id, int *id_out)
{
    if (!store || !export_id)
        return false;

    char export_id_hex[33];
    plank_crypto_to_hex(export_id, PLANK_EXPORT_ID_SIZE, export_id_hex);

    char sql[256];
    snprintf(sql, sizeof(sql),
             "INSERT INTO plank_user_exports (export_id, user_id, status) VALUES "
             "(X'%s', %d, 0)",
             export_id_hex, user_id);

    if (!db_exec(store->db, sql))
    {
        plank_set_error("Failed to create user export: %s", db_last_error(store->db));
        return false;
    }

    if (id_out)
    {
        *id_out = db_last_insert_id(store->db);
    }

    return true;
}

bool plank_store_user_export_complete(plank_store_t *store, int id,
                                      const uint8_t *bundle_id,
                                      const char *packet_path,
                                      int message_count,
                                      int attachment_count,
                                      uint64_t cursor_low,
                                      uint64_t cursor_high)
{
    if (!store || id <= 0 || !bundle_id || !packet_path)
        return false;

    char bundle_id_hex[65];
    char packet_path_esc[1024];
    plank_crypto_to_hex(bundle_id, PLANK_BUNDLE_ID_SIZE, bundle_id_hex);
    escape_sql_string(packet_path_esc, sizeof(packet_path_esc), packet_path);

    char sql[2048];
    snprintf(sql, sizeof(sql),
             "UPDATE plank_user_exports SET "
             "bundle_id = X'%s', exported_at = datetime('now'), packet_path = %s, "
             "message_count = %d, attachment_count = %d, "
             "cursor_low = %llu, cursor_high = %llu, status = 1 "
             "WHERE id = %d",
             bundle_id_hex, packet_path_esc, message_count, attachment_count,
             (unsigned long long)cursor_low, (unsigned long long)cursor_high, id);

    if (!db_exec(store->db, sql))
    {
        plank_set_error("Failed to complete user export: %s", db_last_error(store->db));
        return false;
    }

    return true;
}

/* ============================================================================
 * AUDIT LOG
 * ============================================================================ */

static void escape_sql_string(char *out, size_t out_size, const char *in)
{
    if (!out || !in || out_size < 2)
        return;

    out[0] = '\'';
    size_t pos = 1;

    while (*in && pos + 2 < out_size)
    {
        if (*in == '\'')
        {
            out[pos++] = '\'';
            out[pos++] = '\'';
        }
        else if (*in == '\\')
        {
            out[pos++] = '\\';
            out[pos++] = '\\';
        }
        else
        {
            out[pos++] = *in;
        }
        in++;
    }

    if (pos + 1 < out_size)
    {
        out[pos++] = '\'';
    }
    out[pos] = '\0';
}

bool plank_store_audit_log(plank_store_t *store,
                           const char *event_type,
                           int link_id,
                           const char *node_addr,
                           const char *user_handle,
                           const uint8_t *object_id,
                           const char *details)
{
    if (!store || !event_type)
        return false;

    DbBind binds[] = {
        DB_BIND_TEXT_VAL(event_type),
        link_id > 0 ? DB_BIND_INT_VAL(link_id) : DB_BIND_NULL_VAL,
        node_addr ? DB_BIND_TEXT_VAL(node_addr) : DB_BIND_NULL_VAL,
        user_handle ? DB_BIND_TEXT_VAL(user_handle) : DB_BIND_NULL_VAL,
        object_id ? DB_BIND_BLOB_VAL(object_id, PLANK_OBJECT_ID_SIZE) : DB_BIND_NULL_VAL,
        details ? DB_BIND_TEXT_VAL(details) : DB_BIND_NULL_VAL,
    };

    return db_exec_prepared(store->db,
                            "INSERT INTO plank_audit "
                            "(event_type, link_id, node_addr, user_handle, object_id, details) VALUES "
                            "(?1, ?2, ?3, ?4, ?5, ?6)",
                            binds, 6);
}

/* ============================================================================
 * CONFIGURATION
 * ============================================================================ */

int plank_store_config_get_int(plank_store_t *store, const char *key, int default_val)
{
    if (!store || !key)
        return default_val;

    DbBind binds[] = {DB_BIND_TEXT_VAL(key)};
    return db_query_int_prepared(store->db,
                                 "SELECT CAST(v AS INTEGER) FROM plank_config WHERE k = ?1 LIMIT 1",
                                 binds, 1, default_val);
}

bool plank_store_config_set(plank_store_t *store, const char *key, const char *value)
{
    if (!store || !key || !value)
        return false;

    DbBind binds[] = {
        DB_BIND_TEXT_VAL(key),
        DB_BIND_TEXT_VAL(value),
    };
    return db_exec_prepared(store->db,
                            "INSERT INTO plank_config (k, v) VALUES (?1, ?2) "
                            "ON CONFLICT(k) DO UPDATE SET v = excluded.v",
                            binds, 2);
}

/* ============================================================================
 * TRANSACTIONS
 * ============================================================================ */

bool plank_store_begin(plank_store_t *store)
{
    if (!store)
        return false;
    return db_exec(store->db, "BEGIN TRANSACTION");
}

bool plank_store_commit(plank_store_t *store)
{
    if (!store)
        return false;
    return db_exec(store->db, "COMMIT");
}

bool plank_store_rollback(plank_store_t *store)
{
    if (!store)
        return false;
    return db_exec(store->db, "ROLLBACK");
}
