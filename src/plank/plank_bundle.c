/*
 * PLANK Bundle Implementation
 * .plb (node-to-node) and .plp (user packet) bundle handling
 */

#include "plank/plank_bundle.h"
#include "plank/plank.h"
#include "plank/plank_wire.h"
#include "plank/plank_store.h"
#include "bbs_db.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#ifdef HAVE_SQLITE
#include <sqlite3.h>
#endif

static bool plank_make_temp_file(const char *prefix, char *path, size_t path_size, int *fd_out)
{
    if (!path || path_size == 0 || !fd_out)
        return false;
    const char *tmpdir = getenv("TMPDIR");
    if (!tmpdir || !tmpdir[0])
        tmpdir = "/tmp";
    int n = snprintf(path, path_size, "%s/%s_XXXXXX", tmpdir, prefix ? prefix : "plank");
    if (n < 0 || (size_t)n >= path_size)
    {
        plank_set_error("Temporary path too long");
        return false;
    }
    int fd = mkstemp(path);
    if (fd < 0)
    {
        plank_set_error("Failed to create temporary bundle file: %s", strerror(errno));
        return false;
    }
    *fd_out = fd;
    return true;
}

/* ============================================================================
 * BUNDLE WRITER
 * ============================================================================ */

struct plank_bundle_writer
{
    FILE *fp;
    char *path;
    plank_bundle_type_t type;
    uint8_t bundle_id[PLANK_BUNDLE_ID_SIZE];
    uint8_t source_node_id[PLANK_NODE_ID_SIZE];
    char source_node_addr[PLANK_MAX_ADDRESS];

    /* Directory entries */
    plank_bundle_dirent_t *entries;
    size_t entry_count;
    size_t entry_cap;

    /* Current offset (after header) */
    uint64_t data_offset;

    /* Manifest */
    plank_bundle_manifest_t manifest;

    /* State */
    bool header_written;
    bool finalized;
    char error[256];
};

plank_bundle_writer_t *plank_bundle_writer_create(const char *path,
                                                  plank_bundle_type_t type,
                                                  const uint8_t *source_node_id,
                                                  const char *source_node_addr)
{
    if (!path || !source_node_id || !source_node_addr)
    {
        plank_set_error("Invalid arguments for bundle writer");
        return NULL;
    }

    plank_bundle_writer_t *w = calloc(1, sizeof(plank_bundle_writer_t));
    if (!w)
    {
        plank_set_error("Failed to allocate bundle writer");
        return NULL;
    }

    w->path = strdup(path);
    if (!w->path)
    {
        free(w);
        plank_set_error("Failed to allocate path");
        return NULL;
    }

    w->fp = fopen(path, "wb");
    if (!w->fp)
    {
        plank_set_error("Failed to create bundle file: %s", strerror(errno));
        free(w->path);
        free(w);
        return NULL;
    }

    w->type = type;
    memcpy(w->source_node_id, source_node_id, PLANK_NODE_ID_SIZE);
    strncpy(w->source_node_addr, source_node_addr, sizeof(w->source_node_addr) - 1);

    /* Generate bundle ID */
    plank_crypto_random(w->bundle_id, PLANK_BUNDLE_ID_SIZE);

    /* Initialize entry array */
    w->entry_cap = 64;
    w->entries = calloc(w->entry_cap, sizeof(plank_bundle_dirent_t));
    if (!w->entries)
    {
        fclose(w->fp);
        free(w->path);
        free(w);
        plank_set_error("Failed to allocate entry array");
        return NULL;
    }

    /* Initialize manifest */
    memcpy(w->manifest.bundle_id, w->bundle_id, PLANK_BUNDLE_ID_SIZE);
    w->manifest.bundle_type = type;
    memcpy(w->manifest.source_node_id, source_node_id, PLANK_NODE_ID_SIZE);
    strncpy(w->manifest.source_node_addr, source_node_addr, sizeof(w->manifest.source_node_addr) - 1);
    w->manifest.created_at = (uint64_t)time(NULL);

    /* Reserve space for header - will be written at finalize */
    w->data_offset = PLANK_BUNDLE_HEADER_SIZE;
    fseek(w->fp, PLANK_BUNDLE_HEADER_SIZE, SEEK_SET);

    return w;
}

void plank_bundle_writer_set_target(plank_bundle_writer_t *w,
                                    const uint8_t *target_node_id)
{
    if (w && target_node_id)
    {
        memcpy(w->manifest.target_node_id, target_node_id, PLANK_NODE_ID_SIZE);
    }
}

void plank_bundle_writer_set_export_id(plank_bundle_writer_t *w,
                                       const uint8_t *export_id)
{
    if (w && export_id)
    {
        memcpy(w->manifest.export_id, export_id, PLANK_EXPORT_ID_SIZE);
    }
}

void plank_bundle_writer_set_cursors(plank_bundle_writer_t *w,
                                     uint64_t low, uint64_t high)
{
    if (w)
    {
        w->manifest.cursor_low = low;
        w->manifest.cursor_high = high;
    }
}

void plank_bundle_writer_set_scope(plank_bundle_writer_t *w, const char *scope)
{
    if (w && scope)
    {
        strncpy(w->manifest.scope, scope, sizeof(w->manifest.scope) - 1);
    }
}

void plank_bundle_writer_set_notes(plank_bundle_writer_t *w, const char *notes)
{
    if (w && notes)
    {
        strncpy(w->manifest.notes, notes, sizeof(w->manifest.notes) - 1);
    }
}

void plank_bundle_writer_set_compression(plank_bundle_writer_t *w,
                                         plank_compression_t mode)
{
    if (w)
    {
        w->manifest.compression_mode = mode;
    }
}

static bool grow_entries(plank_bundle_writer_t *w)
{
    if (w->entry_count >= w->entry_cap)
    {
        size_t new_cap = w->entry_cap * 2;
        plank_bundle_dirent_t *new_entries = realloc(w->entries,
                                                     new_cap * sizeof(plank_bundle_dirent_t));
        if (!new_entries)
        {
            snprintf(w->error, sizeof(w->error), "Failed to grow entry array");
            return false;
        }
        w->entries = new_entries;
        w->entry_cap = new_cap;
    }
    return true;
}

bool plank_bundle_writer_add_object(plank_bundle_writer_t *w,
                                    const plank_object_t *obj)
{
    if (!w || !obj || w->finalized)
    {
        plank_set_error("Invalid arguments for add_object");
        return false;
    }

    if (!obj->envelope_cbor || obj->envelope_cbor_len == 0)
    {
        plank_set_error("Object has no encoded envelope");
        return false;
    }

    if (!grow_entries(w))
        return false;

    /* Write object data */
    if (fwrite(obj->envelope_cbor, 1, obj->envelope_cbor_len, w->fp) != obj->envelope_cbor_len)
    {
        snprintf(w->error, sizeof(w->error), "Failed to write object data");
        return false;
    }

    /* Create directory entry */
    plank_bundle_dirent_t *entry = &w->entries[w->entry_count];
    memset(entry, 0, sizeof(*entry));

    entry->record_type = plank_htons(PLANK_RECORD_OBJECT);
    entry->flags = 0;
    memcpy(entry->record_id, obj->object_id, 24);
    entry->offset = plank_htonll(w->data_offset);
    entry->encoded_len = plank_htonll(obj->envelope_cbor_len);
    entry->decoded_len = plank_htonll(obj->envelope_cbor_len);
    plank_crypto_sha256(obj->envelope_cbor, obj->envelope_cbor_len, entry->digest);

    w->entry_count++;
    w->data_offset += obj->envelope_cbor_len;
    w->manifest.object_count++;
    w->manifest.record_count++;

    return true;
}

bool plank_bundle_writer_add_attachment(plank_bundle_writer_t *w,
                                        const uint8_t *attachment_id,
                                        const uint8_t *data,
                                        size_t data_len,
                                        bool compress)
{
    if (!w || !attachment_id || !data || w->finalized)
    {
        plank_set_error("Invalid arguments for add_attachment");
        return false;
    }

    if (!grow_entries(w))
        return false;

    /* Optionally compress */
    const uint8_t *write_data = data;
    size_t write_len = data_len;
    uint8_t *compressed = NULL;
    uint16_t flags = 0;

#ifdef HAVE_ZSTD
    if (compress && data_len > 256)
    {
        uint8_t *comp_out = NULL;
        size_t comp_len = 0;
        if (plank_crypto_zstd_compress(data, data_len, &comp_out, &comp_len, 3))
        {
            if (comp_len < data_len * 9 / 10)
            {
                compressed = comp_out;
                write_data = compressed;
                write_len = comp_len;
                flags = 0x0001; /* Compressed flag */
            }
            else
            {
                free(comp_out);
            }
        }
    }
#else
    (void)compress;
#endif

    /* Write attachment data */
    if (fwrite(write_data, 1, write_len, w->fp) != write_len)
    {
        free(compressed);
        snprintf(w->error, sizeof(w->error), "Failed to write attachment data");
        return false;
    }

    /* Create directory entry */
    plank_bundle_dirent_t *entry = &w->entries[w->entry_count];
    memset(entry, 0, sizeof(*entry));

    entry->record_type = plank_htons(PLANK_RECORD_ATTACHMENT);
    entry->flags = plank_htons(flags);
    memcpy(entry->record_id, attachment_id, 24);
    entry->offset = plank_htonll(w->data_offset);
    entry->encoded_len = plank_htonll(write_len);
    entry->decoded_len = plank_htonll(data_len);
    plank_crypto_sha256(write_data, write_len, entry->digest);

    w->entry_count++;
    w->data_offset += write_len;
    w->manifest.attachment_count++;
    w->manifest.record_count++;

    free(compressed);
    return true;
}

bool plank_bundle_writer_add_attachment_file(plank_bundle_writer_t *w,
                                             const uint8_t *attachment_id,
                                             const char *filepath,
                                             bool compress)
{
    if (!w || !attachment_id || !filepath)
        return false;

    FILE *f = fopen(filepath, "rb");
    if (!f)
    {
        snprintf(w->error, sizeof(w->error), "Failed to open attachment file: %s", filepath);
        return false;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0 || size > 100 * 1024 * 1024)
    {
        fclose(f);
        snprintf(w->error, sizeof(w->error), "Invalid attachment file size");
        return false;
    }

    uint8_t *data = malloc((size_t)size);
    if (!data)
    {
        fclose(f);
        return false;
    }

    if (fread(data, 1, (size_t)size, f) != (size_t)size)
    {
        free(data);
        fclose(f);
        return false;
    }
    fclose(f);

    bool ok = plank_bundle_writer_add_attachment(w, attachment_id, data, (size_t)size, compress);
    free(data);
    return ok;
}

bool plank_bundle_writer_add_checkpoint(plank_bundle_writer_t *w,
                                        const uint8_t *link_id,
                                        uint16_t direction,
                                        uint64_t seq_low,
                                        uint64_t seq_high)
{
    if (!w || w->finalized)
        return false;

    w->manifest.cursor_low = seq_low;
    w->manifest.cursor_high = seq_high;
    (void)link_id;
    (void)direction;
    return true;
}

bool plank_bundle_writer_add_index(plank_bundle_writer_t *w,
                                   const uint8_t *index_data, size_t len)
{
    (void)w;
    (void)index_data;
    (void)len;
    return true;
}

bool plank_bundle_writer_add_note(plank_bundle_writer_t *w,
                                  const char *note)
{
    if (w && note)
    {
        strncat(w->manifest.notes, note, sizeof(w->manifest.notes) - strlen(w->manifest.notes) - 1);
    }
    return true;
}

static bool encode_manifest(plank_bundle_writer_t *w, uint8_t **out, size_t *out_len)
{
    cbor_encoder_t enc;
    cbor_encoder_init_dynamic(&enc, 4096);

    cbor_map_builder_t mb;
    cbor_map_builder_init(&mb);

    /* Encode manifest fields */
    cbor_encoder_t tmp;
    uint8_t buf[1024];

    /* bundle_id */
    cbor_encoder_init(&tmp, buf, sizeof(buf));
    cbor_encode_bytes(&tmp, w->manifest.bundle_id, PLANK_BUNDLE_ID_SIZE);
    cbor_map_builder_add(&mb, "bundle_id", cbor_encoder_data(&tmp), cbor_encoder_len(&tmp));

    /* bundle_type */
    cbor_encoder_init(&tmp, buf, sizeof(buf));
    cbor_encode_uint(&tmp, w->manifest.bundle_type);
    cbor_map_builder_add(&mb, "bundle_type", cbor_encoder_data(&tmp), cbor_encoder_len(&tmp));

    /* source_node_id */
    cbor_encoder_init(&tmp, buf, sizeof(buf));
    cbor_encode_bytes(&tmp, w->manifest.source_node_id, PLANK_NODE_ID_SIZE);
    cbor_map_builder_add(&mb, "source_node_id", cbor_encoder_data(&tmp), cbor_encoder_len(&tmp));

    /* source_node_addr */
    cbor_encoder_init(&tmp, buf, sizeof(buf));
    cbor_encode_text(&tmp, w->manifest.source_node_addr);
    cbor_map_builder_add(&mb, "source_node_addr", cbor_encoder_data(&tmp), cbor_encoder_len(&tmp));

    /* created_at */
    cbor_encoder_init(&tmp, buf, sizeof(buf));
    cbor_encode_uint(&tmp, w->manifest.created_at);
    cbor_map_builder_add(&mb, "created_at", cbor_encoder_data(&tmp), cbor_encoder_len(&tmp));

    /* record_count */
    cbor_encoder_init(&tmp, buf, sizeof(buf));
    cbor_encode_uint(&tmp, w->manifest.record_count);
    cbor_map_builder_add(&mb, "record_count", cbor_encoder_data(&tmp), cbor_encoder_len(&tmp));

    /* object_count */
    cbor_encoder_init(&tmp, buf, sizeof(buf));
    cbor_encode_uint(&tmp, w->manifest.object_count);
    cbor_map_builder_add(&mb, "object_count", cbor_encoder_data(&tmp), cbor_encoder_len(&tmp));

    /* attachment_count */
    cbor_encoder_init(&tmp, buf, sizeof(buf));
    cbor_encode_uint(&tmp, w->manifest.attachment_count);
    cbor_map_builder_add(&mb, "attachment_count", cbor_encoder_data(&tmp), cbor_encoder_len(&tmp));

    /* cursors */
    if (w->manifest.cursor_low > 0 || w->manifest.cursor_high > 0)
    {
        cbor_encoder_init(&tmp, buf, sizeof(buf));
        cbor_encode_uint(&tmp, w->manifest.cursor_low);
        cbor_map_builder_add(&mb, "cursor_low", cbor_encoder_data(&tmp), cbor_encoder_len(&tmp));

        cbor_encoder_init(&tmp, buf, sizeof(buf));
        cbor_encode_uint(&tmp, w->manifest.cursor_high);
        cbor_map_builder_add(&mb, "cursor_high", cbor_encoder_data(&tmp), cbor_encoder_len(&tmp));
    }

    /* scope */
    if (w->manifest.scope[0])
    {
        cbor_encoder_init(&tmp, buf, sizeof(buf));
        cbor_encode_text(&tmp, w->manifest.scope);
        cbor_map_builder_add(&mb, "scope", cbor_encoder_data(&tmp), cbor_encoder_len(&tmp));
    }

    /* notes */
    if (w->manifest.notes[0])
    {
        cbor_encoder_init(&tmp, buf, sizeof(buf));
        cbor_encode_text(&tmp, w->manifest.notes);
        cbor_map_builder_add(&mb, "notes", cbor_encoder_data(&tmp), cbor_encoder_len(&tmp));
    }

    cbor_map_builder_encode(&mb, &enc);
    cbor_map_builder_free(&mb);

    if (!cbor_encoder_ok(&enc))
    {
        cbor_encoder_free(&enc);
        return false;
    }

    *out = malloc(cbor_encoder_len(&enc));
    if (!*out)
    {
        cbor_encoder_free(&enc);
        return false;
    }
    memcpy(*out, cbor_encoder_data(&enc), cbor_encoder_len(&enc));
    *out_len = cbor_encoder_len(&enc);

    cbor_encoder_free(&enc);
    return true;
}

bool plank_bundle_writer_finalize(plank_bundle_writer_t *w,
                                  const uint8_t *signing_key_priv)
{
    if (!w || w->finalized)
        return false;

    /* Encode manifest */
    uint8_t *manifest_cbor = NULL;
    size_t manifest_len = 0;
    if (!encode_manifest(w, &manifest_cbor, &manifest_len))
    {
        snprintf(w->error, sizeof(w->error), "Failed to encode manifest");
        return false;
    }

    /* Write directory entries */
    for (size_t i = 0; i < w->entry_count; i++)
    {
        if (fwrite(&w->entries[i], sizeof(plank_bundle_dirent_t), 1, w->fp) != 1)
        {
            free(manifest_cbor);
            snprintf(w->error, sizeof(w->error), "Failed to write directory entry");
            return false;
        }
    }

    /* Write manifest */
    if (fwrite(manifest_cbor, 1, manifest_len, w->fp) != manifest_len)
    {
        free(manifest_cbor);
        snprintf(w->error, sizeof(w->error), "Failed to write manifest");
        return false;
    }

    /* Compute signature over manifest if key provided */
    uint8_t signature[PLANK_SIGNATURE_SIZE] = {0};
    if (signing_key_priv)
    {
        if (!plank_crypto_sign_ed25519(manifest_cbor, manifest_len, signing_key_priv, signature))
        {
            free(manifest_cbor);
            snprintf(w->error, sizeof(w->error), "Failed to sign manifest");
            return false;
        }
    }
    free(manifest_cbor);

    /* Write signature */
    if (fwrite(signature, 1, PLANK_SIGNATURE_SIZE, w->fp) != PLANK_SIGNATURE_SIZE)
    {
        snprintf(w->error, sizeof(w->error), "Failed to write signature");
        return false;
    }

    /* Write terminal marker */
    uint8_t terminal[8];
    memcpy(terminal, PLANK_BUNDLE_TERMINAL, 8);
    if (fwrite(terminal, 1, 8, w->fp) != 8)
    {
        snprintf(w->error, sizeof(w->error), "Failed to write terminal");
        return false;
    }

    /* Go back and write header */
    fseek(w->fp, 0, SEEK_SET);

    plank_bundle_hdr_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    memcpy(hdr.magic, PLANK_BUNDLE_MAGIC, 4);
    hdr.format_version = plank_htons(PLANK_BUNDLE_FORMAT_VERSION);
    hdr.bundle_type = plank_htons((uint16_t)w->type);
    hdr.flags = 0;
    hdr.manifest_len = plank_htonl((uint32_t)manifest_len);
    hdr.dir_count = plank_htonl((uint32_t)w->entry_count);
    hdr.dir_entry_size = plank_htons(PLANK_DIRENT_SIZE);

    if (fwrite(&hdr, sizeof(hdr), 1, w->fp) != 1)
    {
        snprintf(w->error, sizeof(w->error), "Failed to write header");
        return false;
    }

    w->finalized = true;
    return true;
}

bool plank_bundle_writer_get_id(plank_bundle_writer_t *w, uint8_t *bundle_id_out)
{
    if (!w || !bundle_id_out)
        return false;
    memcpy(bundle_id_out, w->bundle_id, PLANK_BUNDLE_ID_SIZE);
    return true;
}

void plank_bundle_writer_close(plank_bundle_writer_t *w)
{
    if (!w)
        return;
    if (w->fp)
        fclose(w->fp);
    free(w->entries);
    free(w->path);
    free(w);
}

const char *plank_bundle_writer_error(plank_bundle_writer_t *w)
{
    return w ? w->error : "NULL writer";
}

/* ============================================================================
 * BUNDLE READER
 * ============================================================================ */

struct plank_bundle_reader
{
    FILE *fp;
    char *path;
    bool owns_path;

    plank_bundle_hdr_t header;
    plank_bundle_dirent_t *entries;
    size_t entry_count;

    plank_bundle_manifest_t manifest;

    uint8_t *manifest_cbor;
    size_t manifest_cbor_len;

    uint8_t signature[PLANK_SIGNATURE_SIZE];

    char error[256];
};

static bool decode_manifest(plank_bundle_reader_t *r)
{
    if (!r->manifest_cbor || r->manifest_cbor_len == 0)
        return false;

    cbor_decoder_t dec;
    cbor_decoder_init(&dec, r->manifest_cbor, r->manifest_cbor_len);

    size_t map_count = cbor_decode_map(&dec);
    if (!cbor_decoder_ok(&dec))
        return false;

    for (size_t i = 0; i < map_count; i++)
    {
        size_t key_len;
        const char *key = cbor_decode_text(&dec, &key_len);
        if (!key)
            break;

        if (strncmp(key, "bundle_id", key_len) == 0)
        {
            size_t len;
            const uint8_t *data = cbor_decode_bytes(&dec, &len);
            if (data && len == PLANK_BUNDLE_ID_SIZE)
            {
                memcpy(r->manifest.bundle_id, data, PLANK_BUNDLE_ID_SIZE);
            }
        }
        else if (strncmp(key, "bundle_type", key_len) == 0)
        {
            r->manifest.bundle_type = (plank_bundle_type_t)cbor_decode_uint(&dec);
        }
        else if (strncmp(key, "source_node_id", key_len) == 0)
        {
            size_t len;
            const uint8_t *data = cbor_decode_bytes(&dec, &len);
            if (data && len == PLANK_NODE_ID_SIZE)
            {
                memcpy(r->manifest.source_node_id, data, PLANK_NODE_ID_SIZE);
            }
        }
        else if (strncmp(key, "source_node_addr", key_len) == 0)
        {
            cbor_decode_text_copy(&dec, r->manifest.source_node_addr, sizeof(r->manifest.source_node_addr));
        }
        else if (strncmp(key, "created_at", key_len) == 0)
        {
            r->manifest.created_at = cbor_decode_uint(&dec);
        }
        else if (strncmp(key, "record_count", key_len) == 0)
        {
            r->manifest.record_count = (uint32_t)cbor_decode_uint(&dec);
        }
        else if (strncmp(key, "object_count", key_len) == 0)
        {
            r->manifest.object_count = (uint32_t)cbor_decode_uint(&dec);
        }
        else if (strncmp(key, "attachment_count", key_len) == 0)
        {
            r->manifest.attachment_count = (uint32_t)cbor_decode_uint(&dec);
        }
        else if (strncmp(key, "cursor_low", key_len) == 0)
        {
            r->manifest.cursor_low = cbor_decode_uint(&dec);
        }
        else if (strncmp(key, "cursor_high", key_len) == 0)
        {
            r->manifest.cursor_high = cbor_decode_uint(&dec);
        }
        else if (strncmp(key, "scope", key_len) == 0)
        {
            cbor_decode_text_copy(&dec, r->manifest.scope, sizeof(r->manifest.scope));
        }
        else if (strncmp(key, "notes", key_len) == 0)
        {
            cbor_decode_text_copy(&dec, r->manifest.notes, sizeof(r->manifest.notes));
        }
        else
        {
            cbor_skip(&dec);
        }
    }

    return cbor_decoder_ok(&dec);
}

static plank_bundle_reader_t *plank_bundle_reader_open_fp(FILE *fp,
                                                          const char *path,
                                                          bool owns_path);

plank_bundle_reader_t *plank_bundle_reader_open(const char *path)
{
    if (!path)
        return NULL;

    FILE *fp = fopen(path, "rb");
    if (!fp)
    {
        plank_set_error("Failed to open bundle: %s", strerror(errno));
        return NULL;
    }

    return plank_bundle_reader_open_fp(fp, path, false);
}

void plank_bundle_reader_close(plank_bundle_reader_t *r)
{
    if (!r)
        return;
    if (r->fp)
        fclose(r->fp);
    if (r->owns_path && r->path)
        unlink(r->path);
    free(r->entries);
    free(r->manifest_cbor);
    free(r->path);
    free(r);
}

const plank_bundle_manifest_t *plank_bundle_reader_manifest(plank_bundle_reader_t *r)
{
    return r ? &r->manifest : NULL;
}

size_t plank_bundle_reader_entry_count(const plank_bundle_reader_t *r)
{
    return r ? r->entry_count : 0;
}

uint32_t plank_bundle_reader_record_count(plank_bundle_reader_t *r)
{
    return r ? (uint32_t)r->entry_count : 0;
}

bool plank_bundle_reader_get_record(plank_bundle_reader_t *r, uint32_t index,
                                    plank_bundle_record_t *record)
{
    if (!r || !record || index >= r->entry_count)
        return false;

    const plank_bundle_dirent_t *entry = &r->entries[index];

    record->record_type = (plank_record_type_t)plank_ntohs(entry->record_type);
    record->flags = plank_ntohs(entry->flags);
    memcpy(record->record_id, entry->record_id, 24);
    memcpy(record->digest, entry->digest, PLANK_HASH_SIZE);
    record->offset = plank_ntohll(entry->offset);
    record->encoded_len = plank_ntohll(entry->encoded_len);
    record->decoded_len = plank_ntohll(entry->decoded_len);
    record->payload = NULL;
    record->payload_len = 0;

    return true;
}

bool plank_bundle_reader_load_payload(plank_bundle_reader_t *r, uint32_t index,
                                      uint8_t **data_out, size_t *len_out)
{
    if (!r || !data_out || !len_out || index >= r->entry_count)
        return false;

    plank_bundle_record_t record;
    if (!plank_bundle_reader_get_record(r, index, &record))
        return false;

    fseek(r->fp, (long)record.offset, SEEK_SET);

    uint8_t *raw = malloc((size_t)record.encoded_len);
    if (!raw)
    {
        plank_set_error("Failed to allocate payload buffer");
        return false;
    }

    if (fread(raw, 1, (size_t)record.encoded_len, r->fp) != (size_t)record.encoded_len)
    {
        free(raw);
        plank_set_error("Failed to read payload");
        return false;
    }

    /* Decompress if needed */
    bool is_compressed = (record.flags & 0x0001) != 0;
    if (is_compressed && record.decoded_len > record.encoded_len)
    {
#ifdef HAVE_ZSTD
        uint8_t *decompressed = NULL;
        size_t decomp_len = 0;

        if (!plank_crypto_zstd_decompress(raw, (size_t)record.encoded_len, &decompressed,
                                          &decomp_len, (size_t)record.decoded_len))
        {
            free(raw);
            plank_set_error("Decompression failed");
            return false;
        }

        free(raw);
        *data_out = decompressed;
        *len_out = decomp_len;
#else
        free(raw);
        plank_set_error("ZSTD decompression not available");
        return false;
#endif
    }
    else
    {
        *data_out = raw;
        *len_out = (size_t)record.encoded_len;
    }

    return true;
}

void plank_bundle_reader_free_payload(uint8_t *data)
{
    free(data);
}

static plank_bundle_reader_t *plank_bundle_reader_open_fp(FILE *fp,
                                                          const char *path,
                                                          bool owns_path)
{
    if (!fp)
        return NULL;

    plank_bundle_reader_t *r = calloc(1, sizeof(plank_bundle_reader_t));
    if (!r)
    {
        if (owns_path && path)
            unlink(path);
        return NULL;
    }

    if (path)
    {
        r->path = strdup(path);
        if (!r->path)
        {
            if (owns_path)
                unlink(path);
            fclose(fp);
            free(r);
            return NULL;
        }
    }

    r->fp = fp;
    r->owns_path = owns_path;

    /* Read header */
    if (fread(&r->header, sizeof(r->header), 1, r->fp) != 1)
    {
        plank_set_error("Failed to read bundle header");
        goto fail;
    }

    if (memcmp(r->header.magic, PLANK_BUNDLE_MAGIC, 4) != 0)
    {
        plank_set_error("Invalid bundle magic");
        goto fail;
    }

    r->entry_count = plank_ntohl(r->header.dir_count);

    /* Layout: [header][payload data][dir_entries][manifest_cbor][signature][terminal(8)] */
    {
        fseek(r->fp, 0, SEEK_END);
        long file_size = ftell(r->fp);

        uint32_t manifest_len = plank_ntohl(r->header.manifest_len);
        long dir_size = (long)(r->entry_count * PLANK_DIRENT_SIZE);
        long dir_offset = file_size - 8 - (long)PLANK_SIGNATURE_SIZE - (long)manifest_len - dir_size;

        if (r->entry_count > 0)
        {
            fseek(r->fp, dir_offset, SEEK_SET);

            r->entries = calloc(r->entry_count, sizeof(plank_bundle_dirent_t));
            if (!r->entries)
                goto fail;

            if (fread(r->entries, sizeof(plank_bundle_dirent_t), r->entry_count, r->fp) != r->entry_count)
            {
                plank_set_error("Failed to read directory entries");
                goto fail;
            }
        }

        /* Always read manifest and signature, regardless of entry count */
        if (manifest_len > 0)
        {
            long manifest_offset = file_size - 8 - (long)PLANK_SIGNATURE_SIZE - (long)manifest_len;
            fseek(r->fp, manifest_offset, SEEK_SET);

            r->manifest_cbor_len = manifest_len;
            r->manifest_cbor = malloc(manifest_len);
            if (!r->manifest_cbor)
                goto fail;

            if (fread(r->manifest_cbor, 1, manifest_len, r->fp) != manifest_len)
            {
                plank_set_error("Failed to read manifest");
                goto fail;
            }
        }

        /* Read signature */
        {
            long sig_offset = file_size - 8 - (long)PLANK_SIGNATURE_SIZE;
            fseek(r->fp, sig_offset, SEEK_SET);
            if (fread(r->signature, 1, PLANK_SIGNATURE_SIZE, r->fp) != PLANK_SIGNATURE_SIZE)
            {
                plank_set_error("Failed to read signature");
                goto fail;
            }
        }

        if (r->manifest_cbor && !decode_manifest(r))
        {
            plank_set_error("Failed to decode manifest");
            goto fail;
        }
    }

    return r;

fail:
    if (r->fp)
        fclose(r->fp);
    if (r->owns_path && r->path)
        unlink(r->path);
    free(r->entries);
    free(r->manifest_cbor);
    free(r->path);
    free(r);
    return NULL;
}

plank_bundle_reader_t *plank_bundle_reader_open_mem(const uint8_t *data, size_t len)
{
    if (!data || len == 0)
        return NULL;

    char tmp_path[512];
    int fd = -1;
    if (!plank_make_temp_file("plank_bundle", tmp_path, sizeof(tmp_path), &fd))
        return NULL;

    size_t written = 0;
    while (written < len)
    {
        ssize_t n = write(fd, data + written, len - written);
        if (n <= 0)
        {
            close(fd);
            unlink(tmp_path);
            plank_set_error("Failed to write temporary bundle file");
            return NULL;
        }
        written += (size_t)n;
    }

    if (lseek(fd, 0, SEEK_SET) < 0)
    {
        close(fd);
        unlink(tmp_path);
        plank_set_error("Failed to reset temporary bundle file");
        return NULL;
    }

    FILE *fp = fdopen(fd, "rb");
    if (!fp)
    {
        close(fd);
        unlink(tmp_path);
        plank_set_error("Failed to open temporary bundle file");
        return NULL;
    }

    return plank_bundle_reader_open_fp(fp, tmp_path, true);
}

bool plank_bundle_reader_foreach_object(plank_bundle_reader_t *r,
                                        plank_bundle_object_cb cb, void *ctx)
{
    if (!r || !cb)
        return false;

    for (size_t i = 0; i < r->entry_count; i++)
    {
        plank_bundle_record_t record;
        if (!plank_bundle_reader_get_record(r, (uint32_t)i, &record))
            continue;

        if (record.record_type == PLANK_RECORD_OBJECT)
        {
            uint8_t *payload = NULL;
            size_t payload_len = 0;

            if (!plank_bundle_reader_load_payload(r, (uint32_t)i, &payload, &payload_len))
                continue;

            plank_object_t *obj = plank_object_decode(payload, payload_len);
            free(payload);

            if (obj)
            {
                bool cont = cb(obj, ctx);
                plank_object_free(obj);
                if (!cont)
                    return true;
            }
        }
    }

    return true;
}

bool plank_bundle_reader_foreach_attachment(plank_bundle_reader_t *r,
                                            plank_bundle_attachment_cb cb, void *ctx)
{
    if (!r || !cb)
        return false;

    for (size_t i = 0; i < r->entry_count; i++)
    {
        plank_bundle_record_t record;
        if (!plank_bundle_reader_get_record(r, (uint32_t)i, &record))
            continue;

        if (record.record_type == PLANK_RECORD_ATTACHMENT)
        {
            uint8_t *payload = NULL;
            size_t payload_len = 0;

            if (!plank_bundle_reader_load_payload(r, (uint32_t)i, &payload, &payload_len))
                continue;

            bool cont = cb(record.record_id, payload, payload_len, ctx);
            free(payload);
            if (!cont)
                return true;
        }
    }

    return true;
}

bool plank_bundle_reader_verify(plank_bundle_reader_t *r, const uint8_t *signing_key_pub)
{
    if (!r || !signing_key_pub)
        return false;

    /* Check if signature is present */
    bool has_sig = false;
    for (int i = 0; i < PLANK_SIGNATURE_SIZE; i++)
    {
        if (r->signature[i] != 0)
        {
            has_sig = true;
            break;
        }
    }

    if (!has_sig)
    {
        plank_set_error("Bundle has no signature");
        return false;
    }

    /* Verify signature over manifest CBOR */
    if (!r->manifest_cbor || r->manifest_cbor_len == 0)
    {
        plank_set_error("No manifest to verify");
        return false;
    }

    return plank_crypto_verify_ed25519(r->manifest_cbor, r->manifest_cbor_len,
                                       r->signature, signing_key_pub);
}

const char *plank_bundle_reader_error(plank_bundle_reader_t *r)
{
    return r ? r->error : "NULL reader";
}

/* ============================================================================
 * BUNDLE IMPORT
 * ============================================================================ */

bool plank_bundle_import(plank_store_t *store, const char *path,
                         int source_link_id, const uint8_t *signing_key_pub,
                         plank_bundle_import_result_t *result)
{
    if (!store || !path || !result)
        return false;

    memset(result, 0, sizeof(*result));

    plank_bundle_reader_t *reader = plank_bundle_reader_open(path);
    if (!reader)
    {
        strncpy(result->error, plank_last_error(), sizeof(result->error) - 1);
        return false;
    }

    const plank_bundle_manifest_t *manifest = plank_bundle_reader_manifest(reader);

    /* Check for duplicate bundle */
    if (plank_store_dedupe_exists(store, manifest->bundle_id))
    {
        plank_bundle_reader_close(reader);
        result->result = PLANK_IMPORT_DUPLICATE;
        strncpy(result->error, "Duplicate bundle", sizeof(result->error) - 1);
        return true;
    }

    /* Verify signature if key provided */
    if (signing_key_pub)
    {
        if (!plank_bundle_reader_verify(reader, signing_key_pub))
        {
            plank_bundle_reader_close(reader);
            result->result = PLANK_IMPORT_REJECTED;
            strncpy(result->error, "Invalid signature", sizeof(result->error) - 1);
            return false;
        }
    }

    /* Begin transaction */
    if (!plank_store_begin(store))
    {
        plank_bundle_reader_close(reader);
        return false;
    }

    /* Import each entry */
    size_t entry_count = plank_bundle_reader_entry_count(reader);
    for (size_t i = 0; i < entry_count; i++)
    {
        plank_bundle_record_t record;
        if (!plank_bundle_reader_get_record(reader, (uint32_t)i, &record))
            continue;

        if (record.record_type == PLANK_RECORD_OBJECT)
        {
            uint8_t *payload = NULL;
            size_t payload_len = 0;

            if (!plank_bundle_reader_load_payload(reader, (uint32_t)i, &payload, &payload_len))
            {
                result->objects_rejected++;
                continue;
            }

            plank_object_t *obj = plank_object_decode(payload, payload_len);
            free(payload);

            if (!obj)
            {
                result->objects_rejected++;
                continue;
            }

            if (plank_store_object_exists(store, obj->object_id))
            {
                plank_object_free(obj);
                result->objects_duplicate++;
                continue;
            }

            int64_t local_seq;
            if (plank_store_object_put(store, obj, PLANK_SOURCE_LINK, source_link_id, &local_seq))
            {
                result->objects_accepted++;
            }
            else
            {
                result->objects_rejected++;
            }

            plank_object_free(obj);
        }
        else if (record.record_type == PLANK_RECORD_ATTACHMENT)
        {
            result->attachments_stored++;
        }
    }

    /* Record import */
    plank_store_dedupe_record(store, manifest->bundle_id);

    /* Commit transaction */
    if (!plank_store_commit(store))
    {
        plank_store_rollback(store);
        plank_bundle_reader_close(reader);
        return false;
    }

    plank_bundle_reader_close(reader);
    result->result = PLANK_IMPORT_ACCEPTED;

    return true;
}

struct plank_bundle_export_query_ctx
{
    plank_bundle_writer_t *writer;
    int object_count;
    uint64_t cursor_high;
    bool ok;
};

static bool bundle_export_row(void *row, void *ctx_ptr)
{
    sqlite3_stmt *st = (sqlite3_stmt *)row;
    struct plank_bundle_export_query_ctx *ctx = (struct plank_bundle_export_query_ctx *)ctx_ptr;

    const void *payload = sqlite3_column_blob(st, 0);
    int payload_len = sqlite3_column_bytes(st, 0);
    if (!payload || payload_len <= 0)
    {
        ctx->ok = false;
        return false;
    }

    plank_object_t *obj = plank_object_decode(payload, (size_t)payload_len);
    if (!obj)
    {
        ctx->ok = false;
        return false;
    }

    if (!plank_bundle_writer_add_object(ctx->writer, obj))
    {
        plank_object_free(obj);
        ctx->ok = false;
        return false;
    }
    plank_object_free(obj);

    ctx->object_count++;

    if (sqlite3_column_count(st) > 1)
    {
        int64_t seq = sqlite3_column_int64(st, 1);
        if ((uint64_t)seq > ctx->cursor_high)
        {
            ctx->cursor_high = (uint64_t)seq;
        }
    }

    return true;
}

static bool bundle_export_objects_query(plank_bundle_writer_t *writer,
                                        BbsDb *db,
                                        const char *sql,
                                        int *object_count_out,
                                        uint64_t *cursor_high_out)
{
    if (!writer || !db || !sql)
        return false;

    struct plank_bundle_export_query_ctx ctx;
    ctx.writer = writer;
    ctx.object_count = 0;
    ctx.cursor_high = 0;
    ctx.ok = true;

#ifdef HAVE_SQLITE
    if (!db_query(db, sql, bundle_export_row, &ctx))
    {
        return false;
    }
#else
    return false;
#endif

    if (object_count_out)
        *object_count_out = ctx.object_count;
    if (cursor_high_out)
        *cursor_high_out = ctx.cursor_high;
    return ctx.ok;
}

struct user_handle_query_ctx
{
    char *handle;
    size_t handle_size;
    bool found;
};

#ifdef HAVE_SQLITE
static bool user_handle_query_row(void *row, void *ctx_ptr)
{
    sqlite3_stmt *st = (sqlite3_stmt *)row;
    struct user_handle_query_ctx *ctx = (struct user_handle_query_ctx *)ctx_ptr;
    const unsigned char *text = sqlite3_column_text(st, 0);
    if (text)
    {
        snprintf(ctx->handle, ctx->handle_size, "%s", text);
        ctx->found = true;
    }
    return false;
}
#endif

static void append_sql_quoted(char *buf, size_t buf_size, const char *text)
{
    if (!buf || !text)
        return;
    strncat(buf, "'", buf_size - strlen(buf) - 1);
    while (*text && strlen(buf) + 2 < buf_size)
    {
        if (*text == '\'')
        {
            strncat(buf, "''", buf_size - strlen(buf) - 1);
        }
        else
        {
            char tmp[2] = {*text, '\0'};
            strncat(buf, tmp, buf_size - strlen(buf) - 1);
        }
        text++;
    }
    strncat(buf, "'", buf_size - strlen(buf) - 1);
}

static bool query_user_handle(BbsDb *db, int user_id, char *handle, size_t handle_size)
{
    if (!db || !handle || handle_size == 0)
        return false;

    handle[0] = '\0';

#ifdef HAVE_SQLITE
    char sql[256];
    snprintf(sql, sizeof(sql), "SELECT handle FROM users WHERE id = %d LIMIT 1", user_id);

    struct user_handle_query_ctx ctx = {handle, handle_size, false};

    if (!db_query(db, sql, user_handle_query_row, &ctx))
        return false;
    return ctx.found;
#else
    (void)db;
    (void)user_id;
    return false;
#endif
}

static uint64_t query_user_export_cursor(BbsDb *db, int user_id)
{
    if (!db || user_id < 0)
        return 0;

    char sql[256];
    snprintf(sql, sizeof(sql),
             "SELECT COALESCE(MAX(cursor_high), 0) "
             "FROM plank_user_exports WHERE user_id = %d AND status IN (1, 2)",
             user_id);
    return (uint64_t)db_query_int(db, sql, 0);
}

bool plank_bundle_import_mem(plank_store_t *store, const uint8_t *data, size_t len,
                             int source_link_id, const uint8_t *signing_key_pub,
                             plank_bundle_import_result_t *result)
{
    if (!store || !data || len == 0 || !result)
        return false;

    char tmp_path[512];
    int fd = -1;
    if (!plank_make_temp_file("plank_bundle", tmp_path, sizeof(tmp_path), &fd))
        return false;

    size_t written = 0;
    while (written < len)
    {
        ssize_t n = write(fd, data + written, len - written);
        if (n <= 0)
        {
            close(fd);
            unlink(tmp_path);
            plank_set_error("Failed to write temporary bundle data");
            return false;
        }
        written += (size_t)n;
    }

    if (lseek(fd, 0, SEEK_SET) < 0)
    {
        close(fd);
        unlink(tmp_path);
        plank_set_error("Failed to reset temporary bundle file");
        return false;
    }

    close(fd);
    bool ok = plank_bundle_import(store, tmp_path, source_link_id, signing_key_pub, result);
    unlink(tmp_path);
    return ok;
}

bool plank_bundle_export_for_link(plank_store_t *store, int link_id,
                                  const char *output_path,
                                  const uint8_t *signing_key_priv,
                                  uint64_t *new_cursor_out,
                                  int *object_count_out)
{
    if (!store || !output_path || !signing_key_priv)
        return false;

    plank_node_identity_t identity;
    if (!plank_store_get_identity(store, &identity))
    {
        plank_set_error("Failed to load node identity");
        return false;
    }

    plank_bundle_writer_t *writer = plank_bundle_writer_create(
        output_path, PLANK_BUNDLE_LINK_SYNC, identity.node_id, identity.node_addr);
    if (!writer)
    {
        plank_set_error("Failed to create bundle writer");
        return false;
    }

    char notes[128];
    snprintf(notes, sizeof(notes), "Link sync export for link %d", link_id);
    plank_bundle_writer_set_notes(writer, notes);

    const char *sql =
        "SELECT o.envelope_cbor, j.local_seq "
        "FROM plank_objects o "
        "JOIN plank_journal j ON o.object_id = j.object_id "
        "ORDER BY j.local_seq ASC";

    BbsDb *db = plank_store_get_db(store);
    int exported = 0;
    uint64_t cursor_high = 0;
    if (!bundle_export_objects_query(writer, db, sql, &exported, &cursor_high))
    {
        plank_bundle_writer_close(writer);
        plank_set_error("Failed to export objects for link");
        return false;
    }

    if (!plank_bundle_writer_finalize(writer, signing_key_priv))
    {
        plank_set_error("Failed to finalize link bundle: %s", plank_bundle_writer_error(writer));
        plank_bundle_writer_close(writer);
        return false;
    }

    if (new_cursor_out)
        *new_cursor_out = cursor_high;
    if (object_count_out)
        *object_count_out = exported;

    plank_bundle_writer_close(writer);
    return true;
}

bool plank_bundle_export_user_packet(plank_store_t *store, int user_id,
                                     const char **area_addrs, int area_count,
                                     bool include_direct_mail,
                                     const char *output_path,
                                     const uint8_t *signing_key_priv,
                                     uint8_t *export_id_out,
                                     int *message_count_out)
{
    return plank_bundle_export_user_packet_ex(store, user_id, area_addrs, area_count,
                                             include_direct_mail, true, 0,
                                             output_path, signing_key_priv,
                                             export_id_out, message_count_out);
}

bool plank_bundle_export_user_packet_ex(plank_store_t *store, int user_id,
                                        const char **area_addrs, int area_count,
                                        bool include_direct_mail,
                                        bool include_read,
                                        int max_messages,
                                        const char *output_path,
                                        const uint8_t *signing_key_priv,
                                        uint8_t *export_id_out,
                                        int *message_count_out)
{
    if (!store || user_id < 0 || !output_path || !signing_key_priv)
        return false;

    plank_node_identity_t identity;
    if (!plank_store_get_identity(store, &identity))
    {
        plank_set_error("Failed to load node identity");
        return false;
    }

    plank_bundle_writer_t *writer = plank_bundle_writer_create(
        output_path, PLANK_BUNDLE_USER_EXPORT, identity.node_id, identity.node_addr);
    if (!writer)
    {
        plank_set_error("Failed to create bundle writer");
        return false;
    }

    uint8_t export_id[PLANK_EXPORT_ID_SIZE];
    if (!plank_crypto_random_export_id(export_id))
    {
        plank_bundle_writer_close(writer);
        plank_set_error("Failed to generate export ID");
        return false;
    }
    plank_bundle_writer_set_export_id(writer, export_id);

    char notes[256] = "Offline user export packet";
    plank_bundle_writer_set_notes(writer, notes);

    BbsDb *db = plank_store_get_db(store);
    char user_handle[64] = "";
    if (include_direct_mail && !query_user_handle(db, user_id, user_handle, sizeof(user_handle)))
    {
        plank_bundle_writer_close(writer);
        plank_set_error("Failed to load user %d for direct-mail export filtering", user_id);
        return false;
    }

    uint64_t cursor_low = include_read ? 0 : query_user_export_cursor(db, user_id);

    char sql[4096];
    const char *base =
        "SELECT o.envelope_cbor, j.local_seq "
        "FROM plank_objects o "
        "JOIN plank_messages m ON o.object_id = m.object_id "
        "JOIN plank_journal j ON o.object_id = j.object_id ";

    char filter[2048] = "WHERE (";
    bool has_filter = false;

    if (area_count > 0)
    {
        strncat(filter, "m.area_addr IN (", sizeof(filter) - strlen(filter) - 1);
        for (int i = 0; i < area_count; i++)
        {
            if (i > 0)
                strncat(filter, ",", sizeof(filter) - strlen(filter) - 1);
            append_sql_quoted(filter, sizeof(filter), area_addrs[i]);
        }
        strncat(filter, ")", sizeof(filter) - strlen(filter) - 1);
        has_filter = true;
    }
    else
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "m.message_type = %d", PLANK_MSG_AREA_POST);
        strncat(filter, buf, sizeof(filter) - strlen(filter) - 1);
        has_filter = true;
    }

    if (include_direct_mail)
    {
        if (has_filter)
        {
            strncat(filter, " OR ", sizeof(filter) - strlen(filter) - 1);
        }
        char user_addr_like[256];
        char handle_like[256];
        char buf[768];
        snprintf(user_addr_like, sizeof(user_addr_like), "%%%s@%%", user_handle);
        snprintf(handle_like, sizeof(handle_like), "%%\"%s\"%%", user_handle);
        snprintf(buf, sizeof(buf), "(m.message_type != %d AND (m.to_addrs LIKE ",
                 PLANK_MSG_AREA_POST);
        strncat(filter, buf, sizeof(filter) - strlen(filter) - 1);
        append_sql_quoted(filter, sizeof(filter), user_addr_like);
        strncat(filter, " OR m.to_addrs LIKE ", sizeof(filter) - strlen(filter) - 1);
        append_sql_quoted(filter, sizeof(filter), handle_like);
        strncat(filter, "))", sizeof(filter) - strlen(filter) - 1);
        has_filter = true;
    }

    strncat(filter, ")", sizeof(filter) - strlen(filter) - 1);

    if (!include_read && cursor_low > 0)
    {
        char cursor_filter[128];
        snprintf(cursor_filter, sizeof(cursor_filter), " AND j.local_seq > %llu",
                 (unsigned long long)cursor_low);
        strncat(filter, cursor_filter, sizeof(filter) - strlen(filter) - 1);
    }

    if (max_messages > 0)
    {
        snprintf(sql, sizeof(sql), "%s %s ORDER BY j.local_seq ASC LIMIT %d",
                 base, filter, max_messages);
    }
    else
    {
        snprintf(sql, sizeof(sql), "%s %s ORDER BY j.local_seq ASC", base, filter);
    }

    struct plank_bundle_export_query_ctx user_ctx;
    user_ctx.writer = writer;
    user_ctx.object_count = 0;
    user_ctx.cursor_high = 0;
    user_ctx.ok = true;

#ifdef HAVE_SQLITE
    if (!db_query(db, sql, bundle_export_row, &user_ctx) || !user_ctx.ok)
    {
        plank_bundle_writer_close(writer);
        plank_set_error("Failed to query messages for user export");
        return false;
    }
#else
    plank_bundle_writer_close(writer);
    return false;
#endif

    int export_row_id = 0;
    if (!plank_store_user_export_create(store, user_id, export_id, &export_row_id))
    {
        plank_bundle_writer_close(writer);
        return false;
    }

    if (!plank_bundle_writer_finalize(writer, signing_key_priv))
    {
        plank_set_error("Failed to finalize user export bundle: %s", plank_bundle_writer_error(writer));
        plank_bundle_writer_close(writer);
        return false;
    }

    uint8_t bundle_id[PLANK_BUNDLE_ID_SIZE];
    if (!plank_bundle_writer_get_id(writer, bundle_id))
    {
        plank_set_error("Failed to read finalized user export bundle ID");
        plank_bundle_writer_close(writer);
        return false;
    }

    if (!plank_store_user_export_complete(store, export_row_id, bundle_id, output_path,
                                          user_ctx.object_count, 0,
                                          cursor_low, user_ctx.cursor_high))
    {
        plank_bundle_writer_close(writer);
        return false;
    }

    if (export_id_out)
        memcpy(export_id_out, export_id, PLANK_EXPORT_ID_SIZE);
    if (message_count_out)
        *message_count_out = user_ctx.object_count;

    plank_bundle_writer_close(writer);
    return true;
}

bool plank_bundle_import_reply(plank_store_t *store, const char *path,
                               int user_id, const uint8_t *signing_key_pub,
                               plank_reply_import_result_t *result)
{
    if (!store || !path || user_id < 0 || !result)
        return false;

    memset(result, 0, sizeof(*result));

    plank_bundle_reader_t *reader = plank_bundle_reader_open(path);
    if (!reader)
    {
        strncpy(result->error, plank_last_error(), sizeof(result->error) - 1);
        return false;
    }

    const plank_bundle_manifest_t *manifest = plank_bundle_reader_manifest(reader);
    if (!manifest)
    {
        strncpy(result->error, "Failed to read manifest", sizeof(result->error) - 1);
        plank_bundle_reader_close(reader);
        return false;
    }

    if (manifest->bundle_type != PLANK_BUNDLE_USER_REPLY)
    {
        plank_bundle_reader_close(reader);
        result->result = PLANK_IMPORT_REJECTED;
        strncpy(result->error, "Not a reply bundle", sizeof(result->error) - 1);
        return false;
    }

    if (plank_store_dedupe_exists(store, manifest->bundle_id))
    {
        plank_bundle_reader_close(reader);
        result->result = PLANK_IMPORT_DUPLICATE;
        result->messages_duplicate = (int)manifest->object_count;
        strncpy(result->error, "Duplicate reply bundle", sizeof(result->error) - 1);
        return true;
    }

    if (signing_key_pub && !plank_bundle_reader_verify(reader, signing_key_pub))
    {
        plank_bundle_reader_close(reader);
        result->result = PLANK_IMPORT_REJECTED;
        strncpy(result->error, "Invalid signature", sizeof(result->error) - 1);
        return false;
    }

    if (!plank_store_begin(store))
    {
        plank_bundle_reader_close(reader);
        return false;
    }

    size_t entry_count = plank_bundle_reader_entry_count(reader);
    for (size_t i = 0; i < entry_count; i++)
    {
        plank_bundle_record_t record;
        if (!plank_bundle_reader_get_record(reader, (uint32_t)i, &record))
            continue;

        if (record.record_type == PLANK_RECORD_OBJECT)
        {
            uint8_t *payload = NULL;
            size_t payload_len = 0;

            if (!plank_bundle_reader_load_payload(reader, (uint32_t)i, &payload, &payload_len))
            {
                result->messages_rejected++;
                continue;
            }

            plank_object_t *obj = plank_object_decode(payload, payload_len);
            free(payload);
            if (!obj)
            {
                result->messages_rejected++;
                continue;
            }

            if (obj->object_class != PLANK_CLASS_MESSAGE)
            {
                plank_object_free(obj);
                result->messages_rejected++;
                continue;
            }

            if (plank_store_object_exists(store, obj->object_id))
            {
                plank_object_free(obj);
                result->messages_duplicate++;
                continue;
            }

            if (plank_store_object_put(store, obj, PLANK_SOURCE_OFFLINE, 0, NULL))
            {
                result->messages_imported++;
            }
            else
            {
                result->messages_rejected++;
            }
            plank_object_free(obj);
        }
    }

    result->result = (result->messages_rejected > 0 || result->messages_duplicate > 0)
                         ? PLANK_IMPORT_PARTIAL
                         : PLANK_IMPORT_ACCEPTED;
    plank_store_dedupe_record(store, manifest->bundle_id);

    if (!plank_store_commit(store))
    {
        plank_store_rollback(store);
        plank_bundle_reader_close(reader);
        return false;
    }

    /* Record reply import */
    char export_id_hex[33] = "";
    char bundle_id_hex[65] = "";
    plank_crypto_to_hex(manifest->export_id, PLANK_EXPORT_ID_SIZE, export_id_hex);
    plank_crypto_to_hex(manifest->bundle_id, PLANK_BUNDLE_ID_SIZE, bundle_id_hex);

    char details[256];
    snprintf(details, sizeof(details), "imported=%d, duplicates=%d, rejected=%d",
             result->messages_imported, result->messages_duplicate, result->messages_rejected);
    char details_sql[512] = "";
    append_sql_quoted(details_sql, sizeof(details_sql), details);

    char sql[1024];
    snprintf(sql, sizeof(sql),
             "INSERT INTO plank_user_replies "
             "(export_id, bundle_id, user_id, imported_at, message_count, attachment_count, import_result, details) VALUES "
             "(X'%s', X'%s', %d, datetime('now'), %d, 0, %d, %s)",
             export_id_hex, bundle_id_hex, user_id,
             result->messages_imported,
             result->result,
             details_sql);
    BbsDb *db = plank_store_get_db(store);
    if (!db_exec(db, sql))
    {
        plank_set_error("Failed to record reply import: %s", db_last_error(db));
        plank_bundle_reader_close(reader);
        return false;
    }

    snprintf(sql, sizeof(sql),
             "UPDATE plank_user_exports SET status = 2 WHERE export_id = X'%s'",
             export_id_hex);
    db_exec(db, sql);

    plank_bundle_reader_close(reader);
    return true;
}

/* ============================================================================
 * BUNDLE INSPECTION
 * ============================================================================ */

void plank_bundle_inspect(const char *path, FILE *out)
{
    if (!path || !out)
        return;

    plank_bundle_reader_t *r = plank_bundle_reader_open(path);
    if (!r)
    {
        fprintf(out, "Failed to open bundle: %s\n", plank_last_error());
        return;
    }

    const plank_bundle_manifest_t *m = plank_bundle_reader_manifest(r);

    fprintf(out, "Bundle: %s\n", path);
    fprintf(out, "  Type: %d\n", m->bundle_type);
    fprintf(out, "  Source: %s\n", m->source_node_addr);
    fprintf(out, "  Created: %lu\n", (unsigned long)m->created_at);
    fprintf(out, "  Records: %u\n", m->record_count);
    fprintf(out, "  Objects: %u\n", m->object_count);
    fprintf(out, "  Attachments: %u\n", m->attachment_count);

    if (m->scope[0])
    {
        fprintf(out, "  Scope: %s\n", m->scope);
    }
    if (m->notes[0])
    {
        fprintf(out, "  Notes: %s\n", m->notes);
    }

    plank_bundle_reader_close(r);
}

plank_bundle_type_t plank_bundle_get_type(const char *path)
{
    if (!path)
        return 0;

    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;

    plank_bundle_hdr_t hdr;
    if (fread(&hdr, sizeof(hdr), 1, f) != 1)
    {
        fclose(f);
        return 0;
    }
    fclose(f);

    if (memcmp(hdr.magic, PLANK_BUNDLE_MAGIC, 4) != 0)
    {
        return 0;
    }

    return (plank_bundle_type_t)plank_ntohs(hdr.bundle_type);
}
