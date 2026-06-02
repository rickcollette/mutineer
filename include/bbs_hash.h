#pragma once
#include <stdbool.h>

/* Password hashing with upgrade path.
   Supports:
   - pbkdf2$<b64> (legacy)
   - bcrypt$<hash> (if libxcrypt available)
   - argon2$<encoded> (if libargon2 available)
*/

bool pw_hash_make(const char* password, char* out, int out_cap);
bool pw_hash_verify(const char* password, const char* encoded);
bool pw_hash_needs_upgrade(const char* encoded);
