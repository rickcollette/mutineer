/*
 * PLANK Link Protocol Implementation
 * Node-to-node session management and wire protocol
 */

#include "plank/plank_link.h"
#include "plank/plank.h"
#include "plank/plank_wire.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <fcntl.h>
#include <poll.h>
#include <time.h>

#ifdef HAVE_OPENSSL
#include <openssl/ssl.h>
#include <openssl/err.h>
#endif

/* ============================================================================
 * SESSION STRUCTURE
 * ============================================================================ */

struct plank_link_session {
    plank_store_t* store;
    int link_id;
    int socket_fd;
    plank_link_state_t state;
    plank_link_direction_t direction;
    
#ifdef HAVE_OPENSSL
    SSL_CTX* ssl_ctx;
    SSL* ssl;
#endif
    
    plank_node_identity_t identity;
    
    uint8_t peer_node_id[PLANK_NODE_ID_SIZE];
    char peer_node_addr[PLANK_MAX_ADDRESS];
    uint8_t peer_signing_key_pub[PLANK_PUBKEY_SIZE];
    
    uint8_t local_nonce[PLANK_NONCE_SIZE];
    uint8_t remote_nonce[PLANK_NONCE_SIZE];
    uint64_t local_timestamp;
    uint64_t remote_timestamp;
    
    uint8_t* recv_buf;
    size_t recv_buf_size;
    size_t recv_buf_len;
    
    plank_link_callbacks_t callbacks;
    
    plank_error_code_t last_error_code;
    char last_error[256];
};

/* ============================================================================
 * SESSION LIFECYCLE
 * ============================================================================ */

static plank_link_session_t* session_alloc(plank_store_t* store, int link_id,
                                           const plank_link_callbacks_t* callbacks) {
    plank_link_session_t* s = calloc(1, sizeof(plank_link_session_t));
    if (!s) return NULL;
    
    s->store = store;
    s->link_id = link_id;
    s->socket_fd = -1;
    s->state = PLANK_LINK_IDLE;
    
    if (callbacks) s->callbacks = *callbacks;
    
    s->recv_buf_size = 65536;
    s->recv_buf = malloc(s->recv_buf_size);
    if (!s->recv_buf) {
        free(s);
        return NULL;
    }
    
    if (store) plank_store_get_identity(store, &s->identity);
    
    return s;
}

plank_link_session_t* plank_link_session_create_outbound(plank_store_t* store,
                                                          int link_id,
                                                          const plank_link_callbacks_t* callbacks) {
    plank_link_session_t* s = session_alloc(store, link_id, callbacks);
    if (s) s->direction = PLANK_DIR_OUTBOUND;
    return s;
}

plank_link_session_t* plank_link_session_create_inbound(plank_store_t* store,
                                                         int socket_fd,
                                                         const plank_link_callbacks_t* callbacks) {
    plank_link_session_t* s = session_alloc(store, 0, callbacks);
    if (s) {
        s->direction = PLANK_DIR_INBOUND;
        s->socket_fd = socket_fd;
        s->state = PLANK_LINK_CONNECTING;
    }
    return s;
}

void plank_link_session_free(plank_link_session_t* session) {
    if (!session) return;
#ifdef HAVE_OPENSSL
    if (session->ssl) { SSL_shutdown(session->ssl); SSL_free(session->ssl); }
    if (session->ssl_ctx) SSL_CTX_free(session->ssl_ctx);
#endif
    if (session->socket_fd >= 0) close(session->socket_fd);
    free(session->recv_buf);
    free(session);
}

plank_link_state_t plank_link_session_state(plank_link_session_t* session) {
    return session ? session->state : PLANK_LINK_IDLE;
}

int plank_link_session_link_id(plank_link_session_t* session) {
    return session ? session->link_id : -1;
}

int plank_link_session_fd(plank_link_session_t* session) {
    return session ? session->socket_fd : -1;
}

const char* plank_link_session_error(plank_link_session_t* session) {
    return session ? session->last_error : "NULL session";
}

/* ============================================================================
 * CONNECTION
 * ============================================================================ */

bool plank_link_session_connect(plank_link_session_t* session,
                                const char* host, int port,
                                int timeout_sec) {
    if (!session || !host || port <= 0) return false;
    (void)timeout_sec;
    
    session->state = PLANK_LINK_CONNECTING;
    
    struct addrinfo hints = {0}, *res;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);
    
    if (getaddrinfo(host, port_str, &hints, &res) != 0) {
        session->state = PLANK_LINK_FAILED;
        return false;
    }
    
    session->socket_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (session->socket_fd < 0) {
        freeaddrinfo(res);
        session->state = PLANK_LINK_FAILED;
        return false;
    }
    
    if (connect(session->socket_fd, res->ai_addr, res->ai_addrlen) < 0) {
        freeaddrinfo(res);
        close(session->socket_fd);
        session->socket_fd = -1;
        session->state = PLANK_LINK_FAILED;
        return false;
    }
    
    freeaddrinfo(res);
    return true;
}

bool plank_link_session_tls_handshake(plank_link_session_t* session) {
    if (!session || session->socket_fd < 0) return false;
    
#ifdef HAVE_OPENSSL
    const SSL_METHOD* method = session->direction == PLANK_DIR_OUTBOUND
                               ? TLS_client_method() : TLS_server_method();
    
    session->ssl_ctx = SSL_CTX_new(method);
    if (!session->ssl_ctx) return false;
    
    SSL_CTX_set_min_proto_version(session->ssl_ctx, TLS1_3_VERSION);
    
    session->ssl = SSL_new(session->ssl_ctx);
    if (!session->ssl) return false;
    
    SSL_set_fd(session->ssl, session->socket_fd);
    
    int ret = session->direction == PLANK_DIR_OUTBOUND
              ? SSL_connect(session->ssl) : SSL_accept(session->ssl);
    
    if (ret != 1) return false;
    
    session->state = PLANK_LINK_TLS_OK;
    return true;
#else
    return false;
#endif
}

bool plank_link_session_get_tls_fingerprint(plank_link_session_t* session,
                                            char* fingerprint_out) {
    (void)session; (void)fingerprint_out;
    return false;
}

/* ============================================================================
 * PROTOCOL HANDSHAKE
 * ============================================================================ */

static bool send_frame(plank_link_session_t* s, plank_frame_type_t type,
                       const uint8_t* payload, size_t len) {
    plank_frame_hdr_t hdr;
    plank_frame_hdr_init(&hdr, type, 0, (uint32_t)len, 0);
    
#ifdef HAVE_OPENSSL
    if (s->ssl) {
        if (SSL_write(s->ssl, &hdr, sizeof(hdr)) != sizeof(hdr)) return false;
        if (len > 0 && SSL_write(s->ssl, payload, (int)len) != (int)len) return false;
    } else
#endif
    {
        if (send(s->socket_fd, &hdr, sizeof(hdr), 0) != sizeof(hdr)) return false;
        if (len > 0 && send(s->socket_fd, payload, len, 0) != (ssize_t)len) return false;
    }
    return true;
}

bool plank_link_session_send_hello(plank_link_session_t* session) {
    if (!session) return false;
    
    plank_crypto_random(session->local_nonce, PLANK_NONCE_SIZE);
    session->local_timestamp = (uint64_t)time(NULL);
    
    cbor_encoder_t enc;
    cbor_encoder_init_dynamic(&enc, 512);
    
    cbor_encode_map(&enc, 5);
    cbor_encode_text(&enc, "version");
    cbor_encode_uint(&enc, PLANK_PROTOCOL_VERSION);
    cbor_encode_text(&enc, "node_id");
    cbor_encode_bytes(&enc, session->identity.node_id, PLANK_NODE_ID_SIZE);
    cbor_encode_text(&enc, "node_addr");
    cbor_encode_text(&enc, session->identity.node_addr);
    cbor_encode_text(&enc, "pubkey");
    cbor_encode_bytes(&enc, session->identity.signing_key_pub, PLANK_PUBKEY_SIZE);
    cbor_encode_text(&enc, "nonce");
    cbor_encode_bytes(&enc, session->local_nonce, PLANK_NONCE_SIZE);
    
    bool ok = send_frame(session, PLANK_FRAME_HELLO, cbor_encoder_data(&enc), cbor_encoder_len(&enc));
    cbor_encoder_free(&enc);
    return ok;
}

bool plank_link_session_handle_hello(plank_link_session_t* session,
                                     const uint8_t* payload, size_t len) {
    if (!session || !payload) return false;
    
    cbor_decoder_t dec;
    cbor_decoder_init(&dec, payload, len);
    
    size_t map_count = cbor_decode_map(&dec);
    for (size_t i = 0; i < map_count; i++) {
        size_t key_len;
        const char* key = cbor_decode_text(&dec, &key_len);
        if (!key) break;
        
        if (strncmp(key, "node_id", key_len) == 0) {
            size_t data_len;
            const uint8_t* data = cbor_decode_bytes(&dec, &data_len);
            if (data && data_len == PLANK_NODE_ID_SIZE)
                memcpy(session->peer_node_id, data, PLANK_NODE_ID_SIZE);
        } else if (strncmp(key, "node_addr", key_len) == 0) {
            cbor_decode_text_copy(&dec, session->peer_node_addr, sizeof(session->peer_node_addr));
        } else if (strncmp(key, "pubkey", key_len) == 0) {
            size_t data_len;
            const uint8_t* data = cbor_decode_bytes(&dec, &data_len);
            if (data && data_len == PLANK_PUBKEY_SIZE)
                memcpy(session->peer_signing_key_pub, data, PLANK_PUBKEY_SIZE);
        } else if (strncmp(key, "nonce", key_len) == 0) {
            size_t data_len;
            const uint8_t* data = cbor_decode_bytes(&dec, &data_len);
            if (data && data_len == PLANK_NONCE_SIZE)
                memcpy(session->remote_nonce, data, PLANK_NONCE_SIZE);
        } else {
            cbor_skip(&dec);
        }
    }
    return true;
}

bool plank_link_session_send_auth_proof(plank_link_session_t* session) {
    if (!session) return false;
    
    uint8_t transcript[256];
    size_t transcript_len;
    
    uint8_t link_id[16] = {0};
    
    if (!plank_crypto_build_auth_transcript(
            session->identity.node_id, session->peer_node_id, link_id,
            session->local_nonce, session->remote_nonce,
            session->local_timestamp, session->remote_timestamp,
            transcript, &transcript_len)) {
        return false;
    }
    
    uint8_t signature[PLANK_SIGNATURE_SIZE];
    if (!plank_crypto_sign_ed25519(transcript, transcript_len,
                                   session->identity.signing_key_priv, signature)) {
        return false;
    }
    
    cbor_encoder_t enc;
    cbor_encoder_init_dynamic(&enc, 128);
    cbor_encode_map(&enc, 1);
    cbor_encode_text(&enc, "sig");
    cbor_encode_bytes(&enc, signature, PLANK_SIGNATURE_SIZE);
    
    bool ok = send_frame(session, PLANK_FRAME_AUTH_PROOF, cbor_encoder_data(&enc), cbor_encoder_len(&enc));
    cbor_encoder_free(&enc);
    
    if (ok) session->state = PLANK_LINK_AUTH_OK;
    return ok;
}

bool plank_link_session_handle_auth_proof(plank_link_session_t* session,
                                          const uint8_t* payload, size_t len) {
    if (!session || !payload) return false;
    
    cbor_decoder_t dec;
    cbor_decoder_init(&dec, payload, len);
    
    uint8_t signature[PLANK_SIGNATURE_SIZE] = {0};
    size_t map_count = cbor_decode_map(&dec);
    
    for (size_t i = 0; i < map_count; i++) {
        size_t key_len;
        const char* key = cbor_decode_text(&dec, &key_len);
        if (!key) break;
        
        if (strncmp(key, "sig", key_len) == 0) {
            size_t sig_len;
            const uint8_t* sig = cbor_decode_bytes(&dec, &sig_len);
            if (sig && sig_len == PLANK_SIGNATURE_SIZE)
                memcpy(signature, sig, PLANK_SIGNATURE_SIZE);
        } else {
            cbor_skip(&dec);
        }
    }
    
    uint8_t transcript[256];
    size_t transcript_len;
    uint8_t link_id[16] = {0};
    
    if (!plank_crypto_build_auth_transcript(
            session->peer_node_id, session->identity.node_id, link_id,
            session->remote_nonce, session->local_nonce,
            session->remote_timestamp, session->local_timestamp,
            transcript, &transcript_len)) {
        return false;
    }
    
    if (!plank_crypto_verify_ed25519(transcript, transcript_len,
                                     session->peer_signing_key_pub, signature)) {
        return false;
    }
    
    session->state = PLANK_LINK_AUTH_OK;
    return true;
}

/* ============================================================================
 * BUNDLE EXCHANGE
 * ============================================================================ */

bool plank_link_session_send_bundle_offer(plank_link_session_t* session,
                                          const uint8_t** bundle_ids,
                                          const uint16_t* bundle_types,
                                          const uint32_t* object_counts,
                                          const uint32_t* sizes,
                                          int count,
                                          uint64_t cursor_low,
                                          uint64_t cursor_high) {
    if (!session || !bundle_ids || count <= 0) return false;
    
    cbor_encoder_t enc;
    cbor_encoder_init_dynamic(&enc, 1024);
    
    cbor_encode_map(&enc, 3);
    cbor_encode_text(&enc, "bundles");
    cbor_encode_array(&enc, (size_t)count);
    for (int i = 0; i < count; i++) {
        cbor_encode_map(&enc, 4);
        cbor_encode_text(&enc, "id");
        cbor_encode_bytes(&enc, bundle_ids[i], PLANK_BUNDLE_ID_SIZE);
        cbor_encode_text(&enc, "type");
        cbor_encode_uint(&enc, bundle_types ? bundle_types[i] : 0);
        cbor_encode_text(&enc, "objects");
        cbor_encode_uint(&enc, object_counts ? object_counts[i] : 0);
        cbor_encode_text(&enc, "size");
        cbor_encode_uint(&enc, sizes ? sizes[i] : 0);
    }
    cbor_encode_text(&enc, "cursor_low");
    cbor_encode_uint(&enc, cursor_low);
    cbor_encode_text(&enc, "cursor_high");
    cbor_encode_uint(&enc, cursor_high);
    
    bool ok = send_frame(session, PLANK_FRAME_BUNDLE_OFFER, cbor_encoder_data(&enc), cbor_encoder_len(&enc));
    cbor_encoder_free(&enc);
    return ok;
}

bool plank_link_session_send_bundle_request(plank_link_session_t* session,
                                            const uint8_t* offer_id,
                                            const uint8_t** bundle_ids,
                                            int count,
                                            int max_items) {
    if (!session) return false;
    (void)offer_id; (void)bundle_ids; (void)count; (void)max_items;
    return send_frame(session, PLANK_FRAME_BUNDLE_REQUEST, NULL, 0);
}

bool plank_link_session_send_bundle(plank_link_session_t* session,
                                    const uint8_t* bundle_id,
                                    const uint8_t* data, size_t len) {
    (void)bundle_id;
    return send_frame(session, PLANK_FRAME_BUNDLE_DATA, data, len);
}

bool plank_link_session_send_bundle_file(plank_link_session_t* session,
                                         const uint8_t* bundle_id,
                                         const char* path) {
    (void)session; (void)bundle_id; (void)path;
    return false;
}

bool plank_link_session_send_receipt(plank_link_session_t* session,
                                     plank_receipt_code_t code,
                                     plank_target_kind_t target_kind,
                                     const uint8_t* target_id,
                                     int accepted, int duplicate,
                                     int rejected, int quarantine,
                                     const char* details) {
    if (!session) return false;
    
    cbor_encoder_t enc;
    cbor_encoder_init_dynamic(&enc, 256);
    
    cbor_encode_map(&enc, 7);
    cbor_encode_text(&enc, "code");
    cbor_encode_uint(&enc, code);
    cbor_encode_text(&enc, "target_kind");
    cbor_encode_uint(&enc, target_kind);
    cbor_encode_text(&enc, "target_id");
    if (target_id) cbor_encode_bytes(&enc, target_id, PLANK_BUNDLE_ID_SIZE);
    else cbor_encode_null(&enc);
    cbor_encode_text(&enc, "accepted");
    cbor_encode_uint(&enc, (uint64_t)accepted);
    cbor_encode_text(&enc, "duplicate");
    cbor_encode_uint(&enc, (uint64_t)duplicate);
    cbor_encode_text(&enc, "rejected");
    cbor_encode_uint(&enc, (uint64_t)rejected);
    cbor_encode_text(&enc, "quarantine");
    cbor_encode_uint(&enc, (uint64_t)quarantine);
    
    if (details && details[0]) {
        cbor_encode_text(&enc, "details");
        cbor_encode_text(&enc, details);
    }
    
    bool ok = send_frame(session, PLANK_FRAME_RECEIPT, cbor_encoder_data(&enc), cbor_encoder_len(&enc));
    cbor_encoder_free(&enc);
    return ok;
}

/* ============================================================================
 * KEEPALIVE
 * ============================================================================ */

bool plank_link_session_send_ping(plank_link_session_t* session) {
    return send_frame(session, PLANK_FRAME_PING, NULL, 0);
}

bool plank_link_session_handle_ping(plank_link_session_t* session,
                                    const uint8_t* payload, size_t len) {
    (void)payload; (void)len;
    return send_frame(session, PLANK_FRAME_PONG, NULL, 0);
}

/* ============================================================================
 * ERROR AND CLOSE
 * ============================================================================ */

bool plank_link_session_send_error(plank_link_session_t* session,
                                   plank_error_code_t code,
                                   bool fatal,
                                   const char* text) {
    cbor_encoder_t enc;
    cbor_encoder_init_dynamic(&enc, 256);
    
    cbor_encode_map(&enc, 3);
    cbor_encode_text(&enc, "code");
    cbor_encode_uint(&enc, code);
    cbor_encode_text(&enc, "fatal");
    cbor_encode_bool(&enc, fatal);
    cbor_encode_text(&enc, "text");
    cbor_encode_text(&enc, text ? text : "");
    
    bool ok = send_frame(session, PLANK_FRAME_ERROR, cbor_encoder_data(&enc), cbor_encoder_len(&enc));
    cbor_encoder_free(&enc);
    return ok;
}

bool plank_link_session_send_close(plank_link_session_t* session,
                                   int reason_code,
                                   const char* text) {
    cbor_encoder_t enc;
    cbor_encoder_init_dynamic(&enc, 128);
    
    cbor_encode_map(&enc, 2);
    cbor_encode_text(&enc, "reason");
    cbor_encode_uint(&enc, (uint64_t)reason_code);
    cbor_encode_text(&enc, "text");
    cbor_encode_text(&enc, text ? text : "");
    
    bool ok = send_frame(session, PLANK_FRAME_CLOSE, cbor_encoder_data(&enc), cbor_encoder_len(&enc));
    cbor_encoder_free(&enc);
    return ok;
}

void plank_link_session_close(plank_link_session_t* session) {
    if (!session) return;
    
    plank_link_session_send_close(session, 0, "Normal close");
    
#ifdef HAVE_OPENSSL
    if (session->ssl) SSL_shutdown(session->ssl);
#endif
    
    if (session->socket_fd >= 0) {
        close(session->socket_fd);
        session->socket_fd = -1;
    }
    
    session->state = PLANK_LINK_IDLE;
}

/* ============================================================================
 * I/O
 * ============================================================================ */

bool plank_link_session_read_frame(plank_link_session_t* session) {
    if (!session || session->socket_fd < 0) return false;
    
    plank_frame_hdr_t hdr;
    ssize_t received;
    
#ifdef HAVE_OPENSSL
    if (session->ssl)
        received = SSL_read(session->ssl, &hdr, sizeof(hdr));
    else
#endif
        received = recv(session->socket_fd, &hdr, sizeof(hdr), MSG_WAITALL);
    
    if (received != sizeof(hdr)) return false;
    if (!plank_frame_hdr_valid_magic(&hdr)) return false;
    
    uint32_t payload_len = plank_frame_hdr_payload_len(&hdr);
    uint8_t* payload = NULL;
    
    if (payload_len > 0) {
        payload = malloc(payload_len);
        if (!payload) return false;
        
#ifdef HAVE_OPENSSL
        if (session->ssl)
            received = SSL_read(session->ssl, payload, (int)payload_len);
        else
#endif
            received = recv(session->socket_fd, payload, payload_len, MSG_WAITALL);
        
        if (received != (ssize_t)payload_len) {
            free(payload);
            return false;
        }
    }
    
    plank_frame_type_t type = plank_frame_hdr_type(&hdr);
    bool ok = true;
    
    switch (type) {
        case PLANK_FRAME_HELLO:
            ok = plank_link_session_handle_hello(session, payload, payload_len);
            break;
        case PLANK_FRAME_AUTH_PROOF:
            ok = plank_link_session_handle_auth_proof(session, payload, payload_len);
            break;
        case PLANK_FRAME_PING:
            ok = plank_link_session_handle_ping(session, payload, payload_len);
            break;
        case PLANK_FRAME_CLOSE:
            session->state = PLANK_LINK_IDLE;
            break;
        case PLANK_FRAME_BUNDLE_DATA:
            if (session->callbacks.on_bundle_received)
                session->callbacks.on_bundle_received(session, NULL, payload, payload_len, session->callbacks.ctx);
            break;
        case PLANK_FRAME_ERROR:
            if (session->callbacks.on_error)
                session->callbacks.on_error(session, PLANK_ERR_INTERNAL, false, "Remote error", session->callbacks.ctx);
            break;
        default:
            break;
    }
    
    free(payload);
    return ok;
}

int plank_link_session_process(plank_link_session_t* session) {
    if (!session || session->socket_fd < 0) return -1;
    
    struct pollfd pfd = { .fd = session->socket_fd, .events = POLLIN };
    int count = 0;
    
    while (poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN)) {
        if (!plank_link_session_read_frame(session)) break;
        count++;
    }
    
    return count;
}
