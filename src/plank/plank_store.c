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

    char node_id_hex[33], pub_hex[65], priv_hex[129];
    plank_crypto_to_hex(identity->node_id, PLANK_NODE_ID_SIZE, node_id_hex);
    plank_crypto_to_hex(identity->signing_key_pub, PLANK_PUBKEY_SIZE, pub_hex);
    plank_crypto_to_hex(identity->signing_key_priv, PLANK_PRIVKEY_SIZE, priv_hex);

    char sql[2048];
    snprintf(sql, sizeof(sql),
             "INSERT OR REPLACE INTO plank_node_identity "
             "(id, node_id, node_name, network_name, signing_key_pub, signing_key_priv, "
             "software_name, software_version, is_cove, max_bundle_bytes, max_frame_bytes, "
             "poll_interval_sec, updated_at) VALUES "
             "(1, X'%s', '%s', '%s', X'%s', X'%s', '%s', '%s', %d, %u, %u, %u, datetime('now'))",
             node_id_hex, identity->node_name, identity->network_name,
             pub_hex, priv_hex,
             identity->software_name, identity->software_version,
             identity->is_cove ? 1 : 0,
             identity->max_bundle_bytes, identity->max_frame_bytes,
             identity->poll_interval_sec);

    if (!db_exec(store->db, sql))
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

    char node_id_hex[33], pub_hex[65];
    plank_crypto_to_hex(peer->node_id, PLANK_NODE_ID_SIZE, node_id_hex);
    plank_crypto_to_hex(peer->signing_key_pub, PLANK_PUBKEY_SIZE, pub_hex);

    char sql[1024];
    snprintf(sql, sizeof(sql),
             "INSERT INTO plank_peers "
             "(node_id, node_name, network_name, node_addr, signing_key_pub, "
             "tls_fingerprint, trust_level, status, notes) VALUES "
             "(X'%s', '%s', '%s', '%s', X'%s', '%s', %d, %d, '%s') "
             "ON CONFLICT(node_id) DO UPDATE SET "
             "node_name = excluded.node_name, network_name = excluded.network_name, "
             "node_addr = excluded.node_addr, signing_key_pub = excluded.signing_key_pub, "
             "tls_fingerprint = excluded.tls_fingerprint, trust_level = excluded.trust_level, "
             "status = excluded.status, notes = excluded.notes",
             node_id_hex, peer->node_name, peer->network_name, peer->node_addr,
             pub_hex, peer->tls_fingerprint, peer->trust_level, peer->status, peer->notes);

    if (!db_exec(store->db, sql))
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

    char link_id_hex[33];
    plank_crypto_to_hex(link->link_id, PLANK_LINK_ID_SIZE, link_id_hex);

    char sql[1024];
    snprintf(sql, sizeof(sql),
             "INSERT INTO plank_links "
             "(link_id, link_name, peer_id, remote_host, remote_port, direction, "
             "enabled, paused, retry_initial_sec, retry_max_sec, retry_limit, state) VALUES "
             "(X'%s', '%s', %d, '%s', %d, %d, %d, %d, %d, %d, %d, %d)",
             link_id_hex, link->link_name, link->peer_id, link->remote_host,
             link->remote_port, link->direction, link->enabled ? 1 : 0,
             link->paused ? 1 : 0, link->retry_initial_sec, link->retry_max_sec,
             link->retry_limit, link->state);

    if (!db_exec(store->db, sql))
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

    char sql[256];
    snprintf(sql, sizeof(sql),
             "UPDATE plank_links SET state = %d, updated_at = datetime('now') WHERE id = %d",
             state, link_id);

    return db_exec(store->db, sql);
}

bool plank_store_link_set_error(plank_store_t *store, int link_id, const char *error)
{
    if (!store)
        return false;

    char sql[512];
    snprintf(sql, sizeof(sql),
             "UPDATE plank_links SET last_error = '%s', updated_at = datetime('now') WHERE id = %d",
             error ? error : "", link_id);

    return db_exec(store->db, sql);
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

    char sql[2048];
    snprintf(sql, sizeof(sql),
             "INSERT INTO plank_areas "
             "(area_addr, area_slug, origin_node_addr, title, description, "
             "distribution_mode, default_retention, posting_policy, attachment_policy, "
             "max_message_bytes, max_attachment_bytes, retention_days, max_hops, status) VALUES "
             "('%s', '%s', '%s', '%s', '%s', %d, %d, %d, %d, %u, %u, %u, %u, %d) "
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
             "updated_at = datetime('now')",
             area->area_addr, area->area_slug, area->origin_node_addr,
             area->title, area->description,
             area->distribution_mode, area->default_retention,
             area->posting_policy, area->attachment_policy,
             area->max_message_bytes, area->max_attachment_bytes,
             area->retention_days, area->max_hops, area->status);

    if (!db_exec(store->db, sql))
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

    /* Insert object */
    char node_id_hex[33], sig_hex[129];
    plank_crypto_to_hex(obj->origin_node_id, PLANK_NODE_ID_SIZE, node_id_hex);
    plank_crypto_to_hex(obj->signature, PLANK_SIGNATURE_SIZE, sig_hex);

    /* For body and envelope, we need to hex-encode the CBOR */
    char *body_hex = malloc(obj->body_cbor_len * 2 + 1);
    char *env_hex = malloc(obj->envelope_cbor_len * 2 + 1);
    if (!body_hex || !env_hex)
    {
        free(body_hex);
        free(env_hex);
        plank_set_error("Failed to allocate hex buffers");
        return false;
    }

    plank_crypto_to_hex(obj->body_cbor, obj->body_cbor_len, body_hex);
    plank_crypto_to_hex(obj->envelope_cbor, obj->envelope_cbor_len, env_hex);

    size_t sql_size = strlen(body_hex) + strlen(env_hex) + 1024;
    char *sql = malloc(sql_size);
    if (!sql)
    {
        free(body_hex);
        free(env_hex);
        plank_set_error("Failed to allocate SQL buffer");
        return false;
    }

    snprintf(sql, sql_size,
             "INSERT INTO plank_objects "
             "(object_id, object_class, origin_node_id, origin_node_addr, created_at_ts, "
             "signature, sig_alg, body_cbor, envelope_cbor, verified) VALUES "
             "(X'%s', %d, X'%s', '%s', %llu, X'%s', %d, X'%s', X'%s', 1)",
             obj_id_hex, obj->object_class, node_id_hex, obj->origin_node_addr,
             (unsigned long long)obj->created_at, sig_hex, obj->sig_alg,
             body_hex, env_hex);

    bool result = db_exec(store->db, sql);

    free(sql);
    free(body_hex);
    free(env_hex);

    if (!result)
    {
        plank_set_error("Failed to store object: %s", db_last_error(store->db));
        return false;
    }

    /* Add journal entry */
    int obj_db_id = db_last_insert_id(store->db);

    char journal_sql[512];
    if (source_link_id > 0)
    {
        snprintf(journal_sql, sizeof(journal_sql),
                 "INSERT INTO plank_journal "
                 "(object_id, object_class, source_kind, source_link_id, processing_state) VALUES "
                 "(X'%s', %d, %d, %d, 0)",
                 obj_id_hex, obj->object_class, source, source_link_id);
    }
    else
    {
        snprintf(journal_sql, sizeof(journal_sql),
                 "INSERT INTO plank_journal "
                 "(object_id, object_class, source_kind, processing_state) VALUES "
                 "(X'%s', %d, %d, 0)",
                 obj_id_hex, obj->object_class, source);
    }
    db_exec(store->db, journal_sql);

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
    if (!store || !bundle_id)
        return false;

    char bundle_id_hex[65], node_id_hex[33];
    plank_crypto_to_hex(bundle_id, PLANK_BUNDLE_ID_SIZE, bundle_id_hex);
    plank_crypto_to_hex(source_node_id, PLANK_NODE_ID_SIZE, node_id_hex);

    char sql[1024];
    snprintf(sql, sizeof(sql),
             "INSERT INTO plank_import_history "
             "(bundle_id, bundle_type, source_node_id, source_node_addr, import_result, "
             "source_link_id, object_count, duplicate_count, rejected_count) VALUES "
             "(X'%s', %d, X'%s', '%s', %d, %d, %d, %d, %d) "
             "ON CONFLICT(bundle_id) DO UPDATE SET "
             "last_seen_at = datetime('now')",
             bundle_id_hex, bundle_type, node_id_hex, source_node_addr,
             result, source_link_id, object_count, duplicate_count, rejected_count);

    return db_exec(store->db, sql);
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

    /* Build JSON array of object IDs */
    char obj_ids_json[4096] = "[";
    for (int i = 0; i < object_count && i < 100; i++)
    {
        char hex[65];
        plank_crypto_to_hex(object_ids[i], PLANK_OBJECT_ID_SIZE, hex);
        if (i > 0)
            strcat(obj_ids_json, ",");
        strcat(obj_ids_json, "\"");
        strcat(obj_ids_json, hex);
        strcat(obj_ids_json, "\"");
    }
    strcat(obj_ids_json, "]");

    char bundle_id_hex[65] = "";
    if (bundle_id)
    {
        plank_crypto_to_hex(bundle_id, PLANK_BUNDLE_ID_SIZE, bundle_id_hex);
    }

    /* Escape string values */
    char addr_esc[512] = "";
    char error_esc[512] = "";
    escape_sql_string(addr_esc, sizeof(addr_esc), target_node_addr);
    if (error_text)
        escape_sql_string(error_esc, sizeof(error_esc), error_text);

    char obj_json_esc[8192] = "";
    escape_sql_string(obj_json_esc, sizeof(obj_json_esc), obj_ids_json);

    char sql[8192];
    if (bundle_id)
    {
        snprintf(sql, sizeof(sql),
                 "INSERT INTO plank_deadletters "
                 "(target_link_id, target_node_addr, object_ids, bundle_id, "
                 "last_error_code, last_error_text, state) VALUES "
                 "(%d, %s, %s, X'%s', %d, %s, 0)",
                 target_link_id, addr_esc, obj_json_esc,
                 bundle_id_hex, error_code, error_text ? error_esc : "NULL");
    }
    else
    {
        snprintf(sql, sizeof(sql),
                 "INSERT INTO plank_deadletters "
                 "(target_link_id, target_node_addr, object_ids, "
                 "last_error_code, last_error_text, state) VALUES "
                 "(%d, %s, %s, %d, %s, 0)",
                 target_link_id, addr_esc, obj_json_esc,
                 error_code, error_text ? error_esc : "NULL");
    }

    return db_exec(store->db, sql);
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

    char obj_id_hex[65];
    plank_crypto_to_hex(object_id, PLANK_OBJECT_ID_SIZE, obj_id_hex);

    char *env_hex = malloc(envelope_cbor_len * 2 + 1);
    if (!env_hex)
        return false;
    plank_crypto_to_hex(envelope_cbor, envelope_cbor_len, env_hex);

    size_t sql_size = strlen(env_hex) + 1024;
    char *sql = malloc(sql_size);
    if (!sql)
    {
        free(env_hex);
        return false;
    }

    snprintf(sql, sql_size,
             "INSERT INTO plank_quarantine "
             "(object_id, object_class, source_link_id, source_node_addr, "
             "quarantine_reason, quarantine_text, envelope_cbor) VALUES "
             "(X'%s', %d, %d, '%s', %d, '%s', X'%s')",
             obj_id_hex, object_class, source_link_id,
             source_node_addr ? source_node_addr : "",
             reason, reason_text ? reason_text : "", env_hex);

    bool result = db_exec(store->db, sql);

    free(sql);
    free(env_hex);

    return result;
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

    char event_type_esc[256] = "";
    char node_addr_esc[512] = "";
    char user_handle_esc[256] = "";
    char details_esc[1024] = "";

    escape_sql_string(event_type_esc, sizeof(event_type_esc), event_type);
    if (node_addr)
        escape_sql_string(node_addr_esc, sizeof(node_addr_esc), node_addr);
    if (user_handle)
        escape_sql_string(user_handle_esc, sizeof(user_handle_esc), user_handle);
    if (details)
        escape_sql_string(details_esc, sizeof(details_esc), details);

    char obj_id_hex[65] = "";
    if (object_id)
    {
        plank_crypto_to_hex(object_id, PLANK_OBJECT_ID_SIZE, obj_id_hex);
    }

    char sql[2048];
    snprintf(sql, sizeof(sql),
             "INSERT INTO plank_audit "
             "(event_type, link_id, node_addr, user_handle, object_id, details) VALUES "
             "(%s, %d, %s, %s, %s, %s)",
             event_type_esc,
             link_id,
             node_addr ? node_addr_esc : "NULL",
             user_handle ? user_handle_esc : "NULL",
             object_id ? (char *)"" : "NULL", /* object_id handled separately */
             details ? details_esc : "NULL");

    return db_exec(store->db, sql);
}

/* ============================================================================
 * CONFIGURATION
 * ============================================================================ */

int plank_store_config_get_int(plank_store_t *store, const char *key, int default_val)
{
    if (!store || !key)
        return default_val;

    char sql[256];
    snprintf(sql, sizeof(sql),
             "SELECT CAST(v AS INTEGER) FROM plank_config WHERE k = '%s' LIMIT 1",
             key);
    int value = db_query_int(store->db, sql, default_val);
    return value;
}

bool plank_store_config_set(plank_store_t *store, const char *key, const char *value)
{
    if (!store || !key || !value)
        return false;

    char sql[256];
    snprintf(sql, sizeof(sql),
             "INSERT INTO plank_config (k, v) VALUES ('%s', '%s') "
             "ON CONFLICT(k) DO UPDATE SET v = excluded.v",
             key, value);

    return db_exec(store->db, sql);
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
