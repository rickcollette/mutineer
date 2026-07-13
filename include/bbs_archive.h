#pragma once
#include <stdbool.h>
#include <stddef.h>

bool bbs_archive_create_zip_from_dir(const char* dir, const char* output_path,
                                     char* errbuf, size_t errcap);
bool bbs_archive_extract_to_dir(const char* archive_path, const char* dest_dir,
                                char* errbuf, size_t errcap);
bool bbs_archive_list_to_text(const char* archive_path, char* out, size_t outcap,
                              int max_entries, char* errbuf, size_t errcap);
bool bbs_archive_test(const char* archive_path, char* errbuf, size_t errcap);
