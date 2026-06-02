/*
 * module.c - Buccaneer module format implementation
 *
 * Part of the Buccaneer language implementation for Mutineer BBS.
 */

#define _POSIX_C_SOURCE 200809L
#include "include/bucc_module.h"
#include "include/bucc_emit.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

static int strcasecmp_local(const char* a, const char* b) {
    while (*a && *b) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb) return ca - cb;
        a++;
        b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

static uint32_t crc32_table[256];
static bool crc32_table_init = false;

static void init_crc32_table(void) {
    if (crc32_table_init) return;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int j = 0; j < 8; j++) {
            c = (c >> 1) ^ ((c & 1) ? 0xEDB88320 : 0);
        }
        crc32_table[i] = c;
    }
    crc32_table_init = true;
}

static uint32_t compute_crc32(const uint8_t* data, size_t len) {
    init_crc32_table();
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc = crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFF;
}

static uint32_t read_u32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint16_t read_u16(const uint8_t* p) {
    return ((uint16_t)p[0] << 8) | (uint16_t)p[1];
}

static void write_u32(uint8_t* p, uint32_t v) {
    p[0] = (v >> 24) & 0xFF;
    p[1] = (v >> 16) & 0xFF;
    p[2] = (v >> 8) & 0xFF;
    p[3] = v & 0xFF;
}

static void write_u16(uint8_t* p, uint16_t v) {
    p[0] = (v >> 8) & 0xFF;
    p[1] = v & 0xFF;
}

bucc_module_t* bucc_module_load_memory(const uint8_t* data, size_t len,
                                       const bucc_load_options_t* opts,
                                       bucc_diag_t* diag) {
    if (!data || len < sizeof(bucc_module_header_t)) {
        if (diag) bucc_diag_error(diag, BUCC_SPAN_NONE, BUCC_ERR_SYNTAX,
                                 "module too small");
        return NULL;
    }
    
    if (memcmp(data, BUCC_MAGIC, 4) != 0) {
        if (diag) bucc_diag_error(diag, BUCC_SPAN_NONE, BUCC_ERR_SYNTAX,
                                 "invalid module magic");
        return NULL;
    }
    
    bucc_module_t* mod = calloc(1, sizeof(bucc_module_t));
    if (!mod) return NULL;
    
    mod->refcount = 1;
    
    memcpy(&mod->header.magic, data, 4);
    mod->header.format_major = read_u16(data + 4);
    mod->header.format_minor = read_u16(data + 6);
    mod->header.flags = read_u32(data + 8);
    mod->header.header_size = read_u32(data + 12);
    mod->header.metadata_offset = read_u32(data + 16);
    mod->header.string_pool_offset = read_u32(data + 20);
    mod->header.const_pool_offset = read_u32(data + 24);
    mod->header.symbol_table_offset = read_u32(data + 28);
    mod->header.proc_table_offset = read_u32(data + 32);
    mod->header.dataset_table_offset = read_u32(data + 36);
    mod->header.import_table_offset = read_u32(data + 40);
    mod->header.bytecode_offset = read_u32(data + 44);
    mod->header.debug_map_offset = read_u32(data + 48);
    mod->header.file_length = read_u32(data + 52);
    mod->header.crc32 = read_u32(data + 56);
    
    if (mod->header.format_major != BUCC_FORMAT_MAJOR) {
        if (diag) bucc_diag_error(diag, BUCC_SPAN_NONE, BUCC_ERR_SYNTAX,
                                 "unsupported module version %d.%d",
                                 mod->header.format_major, mod->header.format_minor);
        bucc_module_release(mod);
        return NULL;
    }
    
    if (mod->header.string_pool_offset > 0 && mod->header.string_pool_offset < len) {
        const uint8_t* sp = data + mod->header.string_pool_offset;
        mod->string_count = read_u32(sp);
        sp += 4;
        
        mod->strings = calloc(mod->string_count, sizeof(char*));
        if (mod->strings) {
            for (uint32_t i = 0; i < mod->string_count && sp < data + len; i++) {
                uint16_t slen = read_u16(sp);
                sp += 2;
                if (sp + slen <= data + len) {
                    mod->strings[i] = malloc(slen + 1);
                    if (mod->strings[i]) {
                        memcpy(mod->strings[i], sp, slen);
                        mod->strings[i][slen] = '\0';
                    }
                    sp += slen;
                }
            }
        }
    }
    
    if (mod->header.const_pool_offset > 0 && mod->header.const_pool_offset < len) {
        const uint8_t* cp = data + mod->header.const_pool_offset;
        mod->const_count = read_u32(cp);
        cp += 4;
        
        mod->constants = calloc(mod->const_count, sizeof(bucc_value_t));
        if (mod->constants) {
            for (uint32_t i = 0; i < mod->const_count && cp < data + len; i++) {
                uint8_t type = *cp++;
                switch (type) {
                    case 0:
                        mod->constants[i] = BUCC_NULL_VAL;
                        break;
                    case 1:
                        mod->constants[i] = BUCC_BOOL_VAL(*cp++ != 0);
                        break;
                    case 2: {
                        int64_t val = 0;
                        for (int j = 0; j < 8; j++) {
                            val = (val << 8) | *cp++;
                        }
                        mod->constants[i] = BUCC_I64_VAL(val);
                        break;
                    }
                    case 3: {
                        union { uint64_t u; double d; } conv;
                        conv.u = 0;
                        for (int j = 0; j < 8; j++) {
                            conv.u = (conv.u << 8) | *cp++;
                        }
                        mod->constants[i] = BUCC_F64_VAL(conv.d);
                        break;
                    }
                    default:
                        mod->constants[i] = BUCC_NULL_VAL;
                        break;
                }
            }
        }
    }
    
    if (mod->header.proc_table_offset > 0 && mod->header.proc_table_offset < len) {
        const uint8_t* pt = data + mod->header.proc_table_offset;
        mod->proc_count = read_u32(pt);
        pt += 4;
        
        mod->procedures = calloc(mod->proc_count, sizeof(bucc_module_proc_t));
        if (mod->procedures) {
            for (uint32_t i = 0; i < mod->proc_count && pt + 16 <= data + len; i++) {
                uint32_t name_idx = read_u32(pt);
                mod->procedures[i].id = read_u32(pt + 4);
                mod->procedures[i].code_offset = read_u32(pt + 8);
                mod->procedures[i].code_len = read_u32(pt + 12);
                mod->procedures[i].param_count = read_u16(pt + 16);
                mod->procedures[i].local_count = read_u16(pt + 18);
                mod->procedures[i].is_function = (pt[20] != 0);
                pt += 24;
                
                if (name_idx < mod->string_count) {
                    mod->procedures[i].name = strdup(mod->strings[name_idx]);
                }
            }
        }
    }
    
    if (mod->header.import_table_offset > 0 && mod->header.import_table_offset < len) {
        const uint8_t* it = data + mod->header.import_table_offset;
        mod->import_count = read_u32(it);
        it += 4;
        
        mod->imports = calloc(mod->import_count, sizeof(bucc_module_import_t));
        if (mod->imports) {
            for (uint32_t i = 0; i < mod->import_count && it + 12 <= data + len; i++) {
                uint32_t ns_idx = read_u32(it);
                uint32_t fn_idx = read_u32(it + 4);
                mod->imports[i].id = read_u32(it + 8);
                it += 12;
                
                if (ns_idx < mod->string_count) {
                    mod->imports[i].ns = strdup(mod->strings[ns_idx]);
                }
                if (fn_idx < mod->string_count) {
                    mod->imports[i].fn = strdup(mod->strings[fn_idx]);
                }
            }
        }
    }
    
    if (mod->header.bytecode_offset > 0 && mod->header.bytecode_offset < len) {
        uint32_t bc_len = len - mod->header.bytecode_offset;
        if (mod->header.debug_map_offset > mod->header.bytecode_offset) {
            bc_len = mod->header.debug_map_offset - mod->header.bytecode_offset;
        }
        
        mod->bytecode = malloc(bc_len);
        if (mod->bytecode) {
            memcpy(mod->bytecode, data + mod->header.bytecode_offset, bc_len);
            mod->bytecode_len = bc_len;
        }
    }
    
    (void)opts;
    
    return mod;
}

bucc_module_t* bucc_module_load_file(const char* path,
                                     const bucc_load_options_t* opts,
                                     bucc_diag_t* diag) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        if (diag) bucc_diag_error(diag, BUCC_SPAN_NONE, BUCC_ERR_SYNTAX,
                                 "cannot open file: %s", path);
        return NULL;
    }
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    if (size <= 0) {
        fclose(f);
        if (diag) bucc_diag_error(diag, BUCC_SPAN_NONE, BUCC_ERR_SYNTAX,
                                 "empty file: %s", path);
        return NULL;
    }
    
    uint8_t* data = malloc(size);
    if (!data) {
        fclose(f);
        return NULL;
    }
    
    if (fread(data, 1, size, f) != (size_t)size) {
        free(data);
        fclose(f);
        if (diag) bucc_diag_error(diag, BUCC_SPAN_NONE, BUCC_ERR_SYNTAX,
                                 "read error: %s", path);
        return NULL;
    }
    fclose(f);
    
    bucc_module_t* mod = bucc_module_load_memory(data, size, opts, diag);
    free(data);
    return mod;
}

static uint32_t add_string_to_pool(char*** strings, uint32_t* count, uint32_t* cap, const char* str) {
    if (!str) str = "";
    for (uint32_t i = 0; i < *count; i++) {
        if ((*strings)[i] && strcmp((*strings)[i], str) == 0) {
            return i;
        }
    }
    if (*count >= *cap) {
        uint32_t new_cap = *cap == 0 ? 16 : *cap * 2;
        char** new_strings = realloc(*strings, new_cap * sizeof(char*));
        if (!new_strings) return 0;
        *strings = new_strings;
        *cap = new_cap;
    }
    (*strings)[*count] = strdup(str);
    return (*count)++;
}

bool bucc_module_save_file(bucc_module_t* mod, const char* path,
                           bucc_diag_t* diag) {
    if (!mod || !path) return false;
    
    uint32_t string_cap = mod->string_count + mod->import_count * 2 + mod->proc_count + 16;
    char** all_strings = calloc(string_cap, sizeof(char*));
    uint32_t all_string_count = 0;
    
    for (uint32_t i = 0; i < mod->string_count; i++) {
        add_string_to_pool(&all_strings, &all_string_count, &string_cap, 
                          mod->strings[i] ? mod->strings[i] : "");
    }
    
    uint32_t* proc_name_indices = calloc(mod->proc_count > 0 ? mod->proc_count : 1, sizeof(uint32_t));
    for (uint32_t i = 0; i < mod->proc_count; i++) {
        proc_name_indices[i] = add_string_to_pool(&all_strings, &all_string_count, &string_cap,
                                                   mod->procedures[i].name ? mod->procedures[i].name : "");
    }
    
    uint32_t* import_ns_indices = calloc(mod->import_count > 0 ? mod->import_count : 1, sizeof(uint32_t));
    uint32_t* import_fn_indices = calloc(mod->import_count > 0 ? mod->import_count : 1, sizeof(uint32_t));
    for (uint32_t i = 0; i < mod->import_count; i++) {
        import_ns_indices[i] = add_string_to_pool(&all_strings, &all_string_count, &string_cap,
                                                   mod->imports[i].ns ? mod->imports[i].ns : "");
        import_fn_indices[i] = add_string_to_pool(&all_strings, &all_string_count, &string_cap,
                                                   mod->imports[i].fn ? mod->imports[i].fn : "");
    }
    
    size_t string_pool_size = 4;
    for (uint32_t i = 0; i < all_string_count; i++) {
        string_pool_size += 2 + (all_strings[i] ? strlen(all_strings[i]) : 0);
    }
    
    size_t const_pool_size = 4;
    for (uint32_t i = 0; i < mod->const_count; i++) {
        const_pool_size += 1;
        switch (mod->constants[i].kind) {
            case BUCC_VAL_BOOL: const_pool_size += 1; break;
            case BUCC_VAL_I64:
            case BUCC_VAL_F64: const_pool_size += 8; break;
            default: break;
        }
    }
    
    size_t proc_table_size = 4 + mod->proc_count * 24;
    size_t import_table_size = 4 + mod->import_count * 12;
    
    size_t total_size = sizeof(bucc_module_header_t) + string_pool_size + const_pool_size + 
                        proc_table_size + import_table_size + mod->bytecode_len;
    
    uint8_t* data = calloc(1, total_size);
    if (!data) {
        for (uint32_t i = 0; i < all_string_count; i++) free(all_strings[i]);
        free(all_strings);
        free(proc_name_indices);
        free(import_ns_indices);
        free(import_fn_indices);
        return false;
    }
    
    uint8_t* p = data;
    
    memcpy(p, BUCC_MAGIC, 4);
    write_u16(p + 4, BUCC_FORMAT_MAJOR);
    write_u16(p + 6, BUCC_FORMAT_MINOR);
    write_u32(p + 8, mod->header.flags);
    write_u32(p + 12, sizeof(bucc_module_header_t));
    
    size_t offset = sizeof(bucc_module_header_t);
    
    write_u32(p + 16, 0);
    write_u32(p + 20, (uint32_t)offset);
    
    uint8_t* sp = data + offset;
    write_u32(sp, all_string_count);
    sp += 4;
    for (uint32_t i = 0; i < all_string_count; i++) {
        size_t slen = all_strings[i] ? strlen(all_strings[i]) : 0;
        write_u16(sp, (uint16_t)slen);
        sp += 2;
        if (slen > 0) {
            memcpy(sp, all_strings[i], slen);
            sp += slen;
        }
    }
    offset += string_pool_size;
    
    write_u32(p + 24, (uint32_t)offset);
    uint8_t* cp = data + offset;
    write_u32(cp, mod->const_count);
    cp += 4;
    for (uint32_t i = 0; i < mod->const_count; i++) {
        switch (mod->constants[i].kind) {
            case BUCC_VAL_NULL:
                *cp++ = 0;
                break;
            case BUCC_VAL_BOOL:
                *cp++ = 1;
                *cp++ = mod->constants[i].as.b ? 1 : 0;
                break;
            case BUCC_VAL_I64: {
                *cp++ = 2;
                int64_t val = mod->constants[i].as.i64;
                for (int j = 7; j >= 0; j--) {
                    *cp++ = (val >> (j * 8)) & 0xFF;
                }
                break;
            }
            case BUCC_VAL_F64: {
                *cp++ = 3;
                union { uint64_t u; double d; } conv;
                conv.d = mod->constants[i].as.f64;
                for (int j = 7; j >= 0; j--) {
                    *cp++ = (conv.u >> (j * 8)) & 0xFF;
                }
                break;
            }
            default:
                *cp++ = 0;
                break;
        }
    }
    offset += const_pool_size;
    
    write_u32(p + 28, 0);
    write_u32(p + 32, (uint32_t)offset);
    
    uint8_t* pt = data + offset;
    write_u32(pt, mod->proc_count);
    pt += 4;
    for (uint32_t i = 0; i < mod->proc_count; i++) {
        write_u32(pt, proc_name_indices[i]);
        write_u32(pt + 4, mod->procedures[i].id);
        write_u32(pt + 8, mod->procedures[i].code_offset);
        write_u32(pt + 12, mod->procedures[i].code_len);
        write_u16(pt + 16, (uint16_t)mod->procedures[i].param_count);
        write_u16(pt + 18, (uint16_t)mod->procedures[i].local_count);
        pt[20] = mod->procedures[i].is_function ? 1 : 0;
        pt += 24;
    }
    offset += proc_table_size;
    
    write_u32(p + 36, 0);
    write_u32(p + 40, (uint32_t)offset);
    
    uint8_t* it = data + offset;
    write_u32(it, mod->import_count);
    it += 4;
    for (uint32_t i = 0; i < mod->import_count; i++) {
        write_u32(it, import_ns_indices[i]);
        write_u32(it + 4, import_fn_indices[i]);
        write_u32(it + 8, mod->imports[i].id);
        it += 12;
    }
    offset += import_table_size;
    
    write_u32(p + 44, (uint32_t)offset);
    if (mod->bytecode && mod->bytecode_len > 0) {
        memcpy(data + offset, mod->bytecode, mod->bytecode_len);
    }
    offset += mod->bytecode_len;
    
    write_u32(p + 48, 0);
    write_u32(p + 52, (uint32_t)offset);
    
    uint32_t crc = compute_crc32(data, offset);
    write_u32(p + 56, crc);
    
    for (uint32_t i = 0; i < all_string_count; i++) free(all_strings[i]);
    free(all_strings);
    free(proc_name_indices);
    free(import_ns_indices);
    free(import_fn_indices);
    
    FILE* f = fopen(path, "wb");
    if (!f) {
        free(data);
        if (diag) bucc_diag_error(diag, BUCC_SPAN_NONE, BUCC_ERR_SYNTAX,
                                 "cannot create file: %s", path);
        return false;
    }
    
    bool ok = fwrite(data, 1, offset, f) == offset;
    fclose(f);
    free(data);
    
    return ok;
}

void bucc_module_retain(bucc_module_t* mod) {
    if (mod) mod->refcount++;
}

void bucc_module_release(bucc_module_t* mod) {
    if (!mod) return;
    if (--mod->refcount > 0) return;
    
    free(mod->program_name);
    free(mod->program_version);
    
    for (uint32_t i = 0; i < mod->string_count; i++) {
        free(mod->strings[i]);
    }
    free(mod->strings);
    
    for (uint32_t i = 0; i < mod->const_count; i++) {
        bucc_value_release(&mod->constants[i]);
    }
    free(mod->constants);
    
    for (uint32_t i = 0; i < mod->proc_count; i++) {
        free(mod->procedures[i].name);
    }
    free(mod->procedures);
    
    for (uint32_t i = 0; i < mod->import_count; i++) {
        free(mod->imports[i].ns);
        free(mod->imports[i].fn);
        free(mod->imports[i].capability);
    }
    free(mod->imports);
    
    free(mod->bytecode);
    free(mod);
}

bucc_module_proc_t* bucc_module_find_proc(bucc_module_t* mod, const char* name) {
    if (!mod || !name) return NULL;
    for (uint32_t i = 0; i < mod->proc_count; i++) {
        if (mod->procedures[i].name &&
            strcasecmp_local(mod->procedures[i].name, name) == 0) {
            return &mod->procedures[i];
        }
    }
    return NULL;
}

bucc_module_proc_t* bucc_module_get_main(bucc_module_t* mod) {
    return bucc_module_find_proc(mod, "Main");
}

const char* bucc_module_get_string(const bucc_module_t* mod, uint32_t index) {
    if (!mod || index >= mod->string_count) return NULL;
    return mod->strings[index];
}

bucc_value_t bucc_module_get_constant(const bucc_module_t* mod, uint32_t index) {
    if (!mod || index >= mod->const_count) return BUCC_NULL_VAL;
    return mod->constants[index];
}

static bool verify_bytecode_bounds(bucc_module_t* mod, bucc_diag_t* diag) {
    if (!mod->bytecode || mod->bytecode_len == 0) return true;
    
    uint32_t ip = 0;
    while (ip < mod->bytecode_len) {
        uint8_t op = mod->bytecode[ip++];
        
        switch (op) {
            case 0x00: case 0x01: case 0x02: case 0x03: case 0x04:
            case 0x0A: case 0x0B: case 0x0C:
            case 0x20: case 0x21: case 0x22: case 0x23: case 0x24:
            case 0x25: case 0x26: case 0x27: case 0x28: case 0x29:
            case 0x2A: case 0x2B: case 0x2C: case 0x2D: case 0x2E: case 0x2F:
            case 0x35: case 0x36: case 0x38:
            case 0x40: case 0x41: case 0x42: case 0x43: case 0x44:
            case 0x45: case 0x46: case 0x47: case 0x48: case 0x49:
            case 0x61: case 0x62: case 0x63:
            case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: case 0x75: case 0x76:
            case 0x7F:
                break;
                
            case 0x05: case 0x06: case 0x07: case 0x08: case 0x09:
            case 0x10: case 0x11: case 0x12: case 0x13: case 0x14:
            case 0x7E:
                if (ip + 2 > mod->bytecode_len) {
                    if (diag) bucc_diag_error(diag, BUCC_SPAN_NONE, BUCC_ERR_SYNTAX,
                                             "bytecode truncated at offset %u", ip - 1);
                    return false;
                }
                ip += 2;
                break;
                
            case 0x33: case 0x34:
            case 0x50: case 0x51:
                if (ip + 3 > mod->bytecode_len) {
                    if (diag) bucc_diag_error(diag, BUCC_SPAN_NONE, BUCC_ERR_SYNTAX,
                                             "bytecode truncated at offset %u", ip - 1);
                    return false;
                }
                ip += 3;
                break;
                
            case 0x30: case 0x31: case 0x32:
            case 0x37:
            case 0x60:
                if (ip + 4 > mod->bytecode_len) {
                    if (diag) bucc_diag_error(diag, BUCC_SPAN_NONE, BUCC_ERR_SYNTAX,
                                             "bytecode truncated at offset %u", ip - 1);
                    return false;
                }
                ip += 4;
                break;
                
            default:
                if (diag) bucc_diag_error(diag, BUCC_SPAN_NONE, BUCC_ERR_SYNTAX,
                                         "unknown opcode 0x%02x at offset %u", op, ip - 1);
                return false;
        }
    }
    
    return true;
}

static bool verify_procedure_offsets(bucc_module_t* mod, bucc_diag_t* diag) {
    for (uint32_t i = 0; i < mod->proc_count; i++) {
        bucc_module_proc_t* proc = &mod->procedures[i];
        
        if (proc->code_offset > mod->bytecode_len) {
            if (diag) bucc_diag_error(diag, BUCC_SPAN_NONE, BUCC_ERR_SYNTAX,
                                     "procedure '%s' has invalid code offset %u (bytecode len %u)",
                                     proc->name ? proc->name : "(unnamed)",
                                     proc->code_offset, mod->bytecode_len);
            return false;
        }
        
        if (proc->code_offset + proc->code_len > mod->bytecode_len) {
            if (diag) bucc_diag_error(diag, BUCC_SPAN_NONE, BUCC_ERR_SYNTAX,
                                     "procedure '%s' code extends beyond bytecode (offset %u + len %u > %u)",
                                     proc->name ? proc->name : "(unnamed)",
                                     proc->code_offset, proc->code_len, mod->bytecode_len);
            return false;
        }
    }
    return true;
}

static bool verify_import_table(bucc_module_t* mod, bucc_diag_t* diag) {
    for (uint32_t i = 0; i < mod->import_count; i++) {
        bucc_module_import_t* imp = &mod->imports[i];
        
        if (!imp->ns || strlen(imp->ns) == 0) {
            if (diag) bucc_diag_error(diag, BUCC_SPAN_NONE, BUCC_ERR_SYNTAX,
                                     "import %u has empty namespace", i);
            return false;
        }
        
        if (!imp->fn || strlen(imp->fn) == 0) {
            if (diag) bucc_diag_error(diag, BUCC_SPAN_NONE, BUCC_ERR_SYNTAX,
                                     "import %u has empty function name", i);
            return false;
        }
    }
    return true;
}

static bool verify_constant_pool(bucc_module_t* mod, bucc_diag_t* diag) {
    for (uint32_t i = 0; i < mod->const_count; i++) {
        bucc_value_t* c = &mod->constants[i];
        
        switch (c->kind) {
            case BUCC_VAL_NULL:
            case BUCC_VAL_BOOL:
            case BUCC_VAL_I64:
            case BUCC_VAL_F64:
            case BUCC_VAL_DATE:
            case BUCC_VAL_DATETIME:
                break;
                
            case BUCC_VAL_STRING:
            case BUCC_VAL_ARRAY:
            case BUCC_VAL_MAP:
            case BUCC_VAL_ERROR:
                if (diag) bucc_diag_error(diag, BUCC_SPAN_NONE, BUCC_ERR_SYNTAX,
                                         "constant %u has unsupported type %d", i, c->kind);
                return false;
                
            default:
                if (diag) bucc_diag_error(diag, BUCC_SPAN_NONE, BUCC_ERR_SYNTAX,
                                         "constant %u has invalid type %d", i, c->kind);
                return false;
        }
    }
    return true;
}

bool bucc_module_verify(bucc_module_t* mod, bucc_diag_t* diag) {
    if (!mod) return false;
    
    if (memcmp(mod->header.magic, BUCC_MAGIC, 4) != 0) {
        if (diag) bucc_diag_error(diag, BUCC_SPAN_NONE, BUCC_ERR_SYNTAX,
                                 "invalid module magic");
        return false;
    }
    
    if (mod->header.format_major != BUCC_FORMAT_MAJOR) {
        if (diag) bucc_diag_error(diag, BUCC_SPAN_NONE, BUCC_ERR_SYNTAX,
                                 "unsupported module version %u.%u (expected %u.x)",
                                 mod->header.format_major, mod->header.format_minor,
                                 BUCC_FORMAT_MAJOR);
        return false;
    }
    
    if (!bucc_module_get_main(mod)) {
        if (diag) bucc_diag_error(diag, BUCC_SPAN_NONE, BUCC_ERR_MISSING_MAIN,
                                 "module has no Main procedure");
        return false;
    }
    
    if (!verify_procedure_offsets(mod, diag)) {
        return false;
    }
    
    if (!verify_import_table(mod, diag)) {
        return false;
    }
    
    if (!verify_constant_pool(mod, diag)) {
        return false;
    }
    
    if (!verify_bytecode_bounds(mod, diag)) {
        return false;
    }
    
    return true;
}

void bucc_module_disassemble(bucc_module_t* mod, FILE* out) {
    if (!mod || !out) return;
    
    fprintf(out, "; Buccaneer Module Disassembly\n");
    fprintf(out, "; Version: %d.%d\n", mod->header.format_major, mod->header.format_minor);
    fprintf(out, "; Flags: 0x%08X\n", mod->header.flags);
    fprintf(out, "\n");
    
    fprintf(out, "; String Pool (%u entries)\n", mod->string_count);
    for (uint32_t i = 0; i < mod->string_count; i++) {
        fprintf(out, ";   [%u] \"%s\"\n", i, mod->strings[i] ? mod->strings[i] : "");
    }
    fprintf(out, "\n");
    
    fprintf(out, "; Constant Pool (%u entries)\n", mod->const_count);
    for (uint32_t i = 0; i < mod->const_count; i++) {
        fprintf(out, ";   [%u] ", i);
        switch (mod->constants[i].kind) {
            case BUCC_VAL_NULL:   fprintf(out, "NULL\n"); break;
            case BUCC_VAL_BOOL:   fprintf(out, "%s\n", mod->constants[i].as.b ? "TRUE" : "FALSE"); break;
            case BUCC_VAL_I64:    fprintf(out, "%lld\n", (long long)mod->constants[i].as.i64); break;
            case BUCC_VAL_F64:    fprintf(out, "%g\n", mod->constants[i].as.f64); break;
            default:              fprintf(out, "?\n"); break;
        }
    }
    fprintf(out, "\n");
    
    fprintf(out, "; Imports (%u entries)\n", mod->import_count);
    for (uint32_t i = 0; i < mod->import_count; i++) {
        fprintf(out, ";   [%u] %s.%s\n", i,
                mod->imports[i].ns ? mod->imports[i].ns : "?",
                mod->imports[i].fn ? mod->imports[i].fn : "?");
    }
    fprintf(out, "\n");
    
    for (uint32_t p = 0; p < mod->proc_count; p++) {
        bucc_module_proc_t* proc = &mod->procedures[p];
        fprintf(out, "; %s %s (params=%u, locals=%u)\n",
                proc->is_function ? "FUNCTION" : "SUB",
                proc->name ? proc->name : "?",
                proc->param_count, proc->local_count);
        
        uint32_t end = proc->code_offset + proc->code_len;
        if (end > mod->bytecode_len) end = mod->bytecode_len;
        
        for (uint32_t ip = proc->code_offset; ip < end; ) {
            fprintf(out, "%04X: ", ip);
            uint8_t op = mod->bytecode[ip++];
            fprintf(out, "%-16s", bucc_opcode_name(op));
            
            switch (op) {
                case OP_PUSH_I64:
                case OP_PUSH_F64:
                case OP_PUSH_STR:
                case OP_PUSH_DATE:
                case OP_PUSH_DATETIME:
                case OP_LOAD_GLOBAL:
                case OP_STORE_GLOBAL:
                case OP_LOAD_LOCAL:
                case OP_STORE_LOCAL:
                case OP_LOAD_ARG:
                case OP_CALL:
                case OP_CALL_HOST:
                case OP_ARRAY_NEW:
                case OP_MAP_NEW:
                case OP_CHAIN:
                case OP_DISPATCH_CALL:
                    if (ip + 2 <= end) {
                        uint16_t arg = (mod->bytecode[ip] << 8) | mod->bytecode[ip+1];
                        fprintf(out, " %u", arg);
                        ip += 2;
                        if (op == OP_CALL || op == OP_CALL_HOST || op == OP_CHAIN) {
                            if (ip < end) {
                                fprintf(out, ", %u", mod->bytecode[ip++]);
                            }
                        }
                    }
                    break;
                    
                case OP_JMP:
                case OP_JMP_FALSE:
                case OP_JMP_TRUE:
                case OP_TRY_BEGIN:
                    if (ip + 4 <= end) {
                        int32_t rel = (int32_t)((mod->bytecode[ip] << 24) |
                                               (mod->bytecode[ip+1] << 16) |
                                               (mod->bytecode[ip+2] << 8) |
                                               mod->bytecode[ip+3]);
                        fprintf(out, " %+d (-> %04X)", rel, (uint32_t)(ip + 4 + rel));
                        ip += 4;
                    }
                    break;
                
                case OP_DEBUG_LINE:
                case OP_PROF_TICK:
                    if (ip + 2 <= end) {
                        uint16_t arg = (mod->bytecode[ip] << 8) | mod->bytecode[ip+1];
                        fprintf(out, " %u", arg);
                        ip += 2;
                    }
                    break;
                    
                default:
                    break;
            }
            fprintf(out, "\n");
        }
        fprintf(out, "\n");
    }
}

bucc_module_t* bucc_module_load(const char* path) {
    return bucc_module_load_file(path, NULL, NULL);
}

bool bucc_module_save(bucc_module_t* mod, const char* path) {
    return bucc_module_save_file(mod, path, NULL);
}

void bucc_module_free(bucc_module_t* mod) {
    bucc_module_release(mod);
}

void bucc_module_disasm(bucc_module_t* mod, FILE* out) {
    bucc_module_disassemble(mod, out);
}

bucc_debug_map_t* bucc_debug_map_new(const char* source_file) {
    bucc_debug_map_t* map = calloc(1, sizeof(bucc_debug_map_t));
    if (!map) return NULL;
    
    if (source_file) {
        map->source_file = strdup(source_file);
    }
    map->entries = NULL;
    map->entry_count = 0;
    
    return map;
}

void bucc_debug_map_free(bucc_debug_map_t* map) {
    if (!map) return;
    
    free(map->source_file);
    free(map->entries);
    free(map);
}

void bucc_debug_map_add_entry(bucc_debug_map_t* map, uint32_t offset, uint32_t line, uint32_t col) {
    if (!map) return;
    
    uint32_t new_count = map->entry_count + 1;
    bucc_debug_line_entry_t* new_entries = realloc(map->entries, 
                                                    new_count * sizeof(bucc_debug_line_entry_t));
    if (!new_entries) return;
    
    map->entries = new_entries;
    map->entries[map->entry_count].bytecode_offset = offset;
    map->entries[map->entry_count].source_line = line;
    map->entries[map->entry_count].source_column = col;
    map->entry_count = new_count;
}

#define BMAP_MAGIC "BMAP"
#define BMAP_VERSION 1

bool bucc_debug_map_save(bucc_debug_map_t* map, const char* path) {
    if (!map || !path) return false;
    
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    
    fwrite(BMAP_MAGIC, 1, 4, f);
    
    uint32_t version = BMAP_VERSION;
    fwrite(&version, sizeof(uint32_t), 1, f);
    
    uint32_t source_len = map->source_file ? (uint32_t)strlen(map->source_file) : 0;
    fwrite(&source_len, sizeof(uint32_t), 1, f);
    if (source_len > 0) {
        fwrite(map->source_file, 1, source_len, f);
    }
    
    fwrite(&map->entry_count, sizeof(uint32_t), 1, f);
    
    for (uint32_t i = 0; i < map->entry_count; i++) {
        fwrite(&map->entries[i].bytecode_offset, sizeof(uint32_t), 1, f);
        fwrite(&map->entries[i].source_line, sizeof(uint32_t), 1, f);
        fwrite(&map->entries[i].source_column, sizeof(uint32_t), 1, f);
    }
    
    fclose(f);
    return true;
}

bucc_debug_map_t* bucc_debug_map_load(const char* path) {
    if (!path) return NULL;
    
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    
    char magic[4];
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, BMAP_MAGIC, 4) != 0) {
        fclose(f);
        return NULL;
    }
    
    uint32_t version;
    if (fread(&version, sizeof(uint32_t), 1, f) != 1 || version != BMAP_VERSION) {
        fclose(f);
        return NULL;
    }
    
    uint32_t source_len;
    if (fread(&source_len, sizeof(uint32_t), 1, f) != 1) {
        fclose(f);
        return NULL;
    }
    
    char* source_file = NULL;
    if (source_len > 0) {
        source_file = malloc(source_len + 1);
        if (!source_file || fread(source_file, 1, source_len, f) != source_len) {
            free(source_file);
            fclose(f);
            return NULL;
        }
        source_file[source_len] = '\0';
    }
    
    uint32_t entry_count;
    if (fread(&entry_count, sizeof(uint32_t), 1, f) != 1) {
        free(source_file);
        fclose(f);
        return NULL;
    }
    
    bucc_debug_map_t* map = calloc(1, sizeof(bucc_debug_map_t));
    if (!map) {
        free(source_file);
        fclose(f);
        return NULL;
    }
    
    map->source_file = source_file;
    map->entry_count = entry_count;
    
    if (entry_count > 0) {
        map->entries = malloc(entry_count * sizeof(bucc_debug_line_entry_t));
        if (!map->entries) {
            bucc_debug_map_free(map);
            fclose(f);
            return NULL;
        }
        
        for (uint32_t i = 0; i < entry_count; i++) {
            if (fread(&map->entries[i].bytecode_offset, sizeof(uint32_t), 1, f) != 1 ||
                fread(&map->entries[i].source_line, sizeof(uint32_t), 1, f) != 1 ||
                fread(&map->entries[i].source_column, sizeof(uint32_t), 1, f) != 1) {
                bucc_debug_map_free(map);
                fclose(f);
                return NULL;
            }
        }
    }
    
    fclose(f);
    return map;
}

int bucc_debug_map_get_line(bucc_debug_map_t* map, uint32_t offset) {
    if (!map || !map->entries || map->entry_count == 0) return -1;
    
    int result = -1;
    for (uint32_t i = 0; i < map->entry_count; i++) {
        if (map->entries[i].bytecode_offset <= offset) {
            result = (int)map->entries[i].source_line;
        } else {
            break;
        }
    }
    
    return result;
}
