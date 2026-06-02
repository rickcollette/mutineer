#include "bbs_hash.h"
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#ifdef HAVE_ARGON2
  #include <argon2.h>
#endif
#ifdef HAVE_CRYPT
  #include <crypt.h>
#endif

static int b64(const unsigned char* in, size_t inlen, char* out, size_t outlen) {
  static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t o = 0;
  for (size_t i = 0; i < inlen; i += 3) {
    unsigned v = in[i] << 16;
    if (i + 1 < inlen) v |= in[i+1] << 8;
    if (i + 2 < inlen) v |= in[i+2];
    if (o + 4 >= outlen) return -1;
    out[o++] = tbl[(v >> 18) & 0x3F];
    out[o++] = tbl[(v >> 12) & 0x3F];
    out[o++] = (i + 1 < inlen) ? tbl[(v >> 6) & 0x3F] : '=';
    out[o++] = (i + 2 < inlen) ? tbl[v & 0x3F] : '=';
  }
  if (o >= outlen) return -1;
  out[o] = 0;
  return (int)o;
}

static bool make_pbkdf2(const char* password, char* out, int out_cap) {
  unsigned char salt[16];
  if (RAND_bytes(salt, sizeof(salt)) != 1) return false;

  unsigned char dk[32];
  if (PKCS5_PBKDF2_HMAC(password, (int)strlen(password),
                        salt, sizeof(salt),
                        200000,
                        EVP_sha256(),
                        sizeof(dk), dk) != 1) return false;

  unsigned char combo[sizeof(salt) + sizeof(dk)];
  memcpy(combo, salt, sizeof(salt));
  memcpy(combo + sizeof(salt), dk, sizeof(dk));

  if (snprintf(out, (size_t)out_cap, "pbkdf2$") >= out_cap) return false;
  int o = (int)strlen(out);
  return b64(combo, sizeof(combo), out + o, (size_t)(out_cap - o)) > 0;
}

#ifdef HAVE_ARGON2
static bool make_argon2(const char* password, char* out, int out_cap) {
  /* Moderate defaults */
  uint32_t t_cost = 3;
  uint32_t m_cost = 1 << 16; /* 64MB */
  uint32_t parallelism = 1;
  uint8_t salt[16];
  if (RAND_bytes(salt, sizeof(salt)) != 1) return false;
  if (argon2id_hash_encoded(t_cost, m_cost, parallelism,
                            password, strlen(password),
                            salt, sizeof(salt),
                            32, out, (size_t)out_cap) != ARGON2_OK) return false;
  return true;
}
#endif

bool pw_hash_make(const char* password, char* out, int out_cap) {
  if (!password || !out || out_cap <= 0) return false;
#ifdef HAVE_ARGON2
  if (make_argon2(password, out, out_cap)) return true;
#endif
  return make_pbkdf2(password, out, out_cap);
}

static int b64dec(const char* in, unsigned char* out, size_t outcap) {
  /* minimal b64 decode; assumes well-formed input from our encoder */
  int len = (int)strlen(in);
  unsigned char rev[256]; memset(rev, 0x80, sizeof(rev));
  const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  for (int i = 0; tbl[i]; i++) rev[(unsigned char)tbl[i]] = (unsigned char)i;

  size_t o = 0; unsigned v = 0; int bits = 0;
  for (int i = 0; i < len; i++) {
    unsigned char c = in[i];
    if (c == '=') break;
    if (rev[c] == 0x80) continue;
    v = (v << 6) | rev[c];
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      if (o >= outcap) return -1;
      out[o++] = (unsigned char)((v >> bits) & 0xFF);
    }
  }
  return (int)o;
}

static bool verify_pbkdf2(const char* password, const char* encoded) {
  const char* b64part = encoded;
  if (strncmp(encoded, "pbkdf2$", 7) == 0) b64part = encoded + 7;
  unsigned char combo[48];
  int n = b64dec(b64part, combo, sizeof(combo));
  if (n != (int)sizeof(combo)) return false;
  unsigned char dk[32];
  if (PKCS5_PBKDF2_HMAC(password, (int)strlen(password),
                        combo, 16,
                        200000,
                        EVP_sha256(),
                        sizeof(dk), dk) != 1) return false;
  return CRYPTO_memcmp(combo + 16, dk, sizeof(dk)) == 0;
}

bool pw_hash_verify(const char* password, const char* encoded) {
  if (!password || !encoded) return false;
#ifdef HAVE_ARGON2
  if (strncmp(encoded, "argon2", 6) == 0) {
    return argon2id_verify(encoded, password, strlen(password)) == ARGON2_OK;
  }
#endif
#ifdef HAVE_CRYPT
  if (strncmp(encoded, "bcrypt$", 7) == 0) {
    const char* hash = encoded + 7;
    char* res = crypt(password, hash);
    return res && strcmp(res, hash) == 0;
  }
#endif
  return verify_pbkdf2(password, encoded);
}

bool pw_hash_needs_upgrade(const char* encoded) {
  if (!encoded) return true;
#ifdef HAVE_ARGON2
  if (strncmp(encoded, "argon2", 6) == 0) return false;
  return true; /* everything else should upgrade to argon2 */
#else
  return false;
#endif
}
