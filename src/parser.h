// ISC License
//
// Copyright (c) 2024-2026 Stephen Seo
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
// REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
// AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
// INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
// LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
// OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
// PERFORMANCE OF THIS SOFTWARE.
//
// `parser.h` is the header for parsing args.

#ifndef SEODISPARATE_COM_SIMPLE_ARCHIVER_PARSER_H_
#define SEODISPARATE_COM_SIMPLE_ARCHIVER_PARSER_H_

// Standard library includes.
#include <stdint.h>

// Local includes.
#include "data_structures/linked_list.h"
#include "data_structures/hash_map.h"
#include "users.h"

extern char *SDSA_NOT_TO_COMPRESS_FILE_EXTS[];

typedef struct SDA_UGMapping {
  SDArchiverHashMap *UidToUname;
  SDArchiverHashMap *UnameToUid;
  SDArchiverHashMap *UidToUid;
  SDArchiverHashMap *UnameToUname;
  SDArchiverHashMap *GidToGname;
  SDArchiverHashMap *GnameToGid;
  SDArchiverHashMap *GidToGid;
  SDArchiverHashMap *GnameToGname;
} SDA_UGMapping;

typedef struct SDArchiverParsed {
  /// Each bit is a flag.
  /// 0b xxxx xx00 - is creating.
  /// 0b xxxx xx01 - is extracting.
  /// 0b xxxx xx10 - is checking/examining.
  /// 0b xxxx x0xx - Do NOT allow create archive overwrite.
  /// 0b xxxx x1xx - Allow create archive overwrite.
  /// 0b xxxx 1xxx - Allow extract overwrite.
  /// 0b xxx1 xxxx - Create archive to stdout or read archive from stdin.
  /// 0b xx1x xxxx - Do not save absolute paths for symlinks.
  /// 0b x1xx xxxx - Sort files by size before archiving.
  /// 0b 1xxx xxxx - No safe links.
  /// 0b xxxx xxx1 xxxx xxxx - Preserve symlink target.
  /// 0b xxxx xx1x xxxx xxxx - Ignore empty directories if set.
  /// 0b xxxx x1xx xxxx xxxx - Force set UID.
  /// 0b xxxx 1xxx xxxx xxxx - Force set GID.
  /// 0b xxx1 xxxx xxxx xxxx - Force set file permissions.
  /// 0b xx1x xxxx xxxx xxxx - Force set directory permissions.
  /// 0b x1xx xxxx xxxx xxxx - Prefer UID over Username when extracting.
  /// 0b 1xxx xxxx xxxx xxxx - Prefer GID over Group when extracting.
  /// 0b xxxx xxx1 xxxx xxxx xxxx xxxx - Force set empty directory permissions.
  /// 0b xxxx xx1x xxxx xxxx xxxx xxxx - white/black-list checking is
  ///   case-insensitive.
  /// 0b xxxx x1xx xxxx xxxx xxxx xxxx - Force use tmpfile.
  /// 0b xxxx 1xxx xxxx xxxx xxxx xxxx - Sort files by name before archiving.
  /// 0b xxx1 xxxx xxxx xxxx xxxx xxxx - Enable positional args with ".." .
  /// 0b xx1x xxxx xxxx xxxx xxxx xxxx - v6-extract remove empty dirs that are
  ///   not supposed to be empty
  /// 0b x1xx xxxx xxxx xxxx xxxx xxxx - also remove leaf dirs
  /// 0b 1xxx xxxx xxxx xxxx xxxx xxxx - force prefix dir(s) permissions
  uint32_t flags;
  /// Null-terminated string.
  char *filename;
  /// Null-terminated string.
  char *filename_full_abs_path;
  /// Null-terminated string.
  char *compressor;
  /// Null-terminated string.
  char *decompressor;
  /// The key is a positional argument, the value is a SDArchiverFileInfo.
  SDArchiverHashMap *working_files;
  /// The key and value is a directory path (without trailing '/').
  SDArchiverLinkedList *working_dirs;
  /// The key and value are always the positional argument(s).
  SDArchiverHashMap *just_w_files;
  /// Determines where to place temporary files. If NULL, temporary files are
  /// created in the target filename's directory.
  /// No longer refers to string in argv.
  char *temp_dir;
  /// Dir specified by "-C".
  const char *user_cwd;
  /// Currently only 0, 1, 2, 3, 4, and 5 is supported.
  uint32_t write_version;
  /// The minimum size of a chunk in bytes (the last chunk may be less).
  uint64_t minimum_chunk_size;
  uint32_t uid;
  uint32_t gid;
  /// 0b xxxx xxxx xxx1 - user read
  /// 0b xxxx xxxx xx1x - user write
  /// 0b xxxx xxxx x1xx - user execute
  /// 0b xxxx xxxx 1xxx - group read
  /// 0b xxxx xxx1 xxxx - group write
  /// 0b xxxx xx1x xxxx - group execute
  /// 0b xxxx x1xx xxxx - other read
  /// 0b xxxx 1xxx xxxx - other write
  /// 0b xxx1 xxxx xxxx - other execute
  uint_fast16_t file_permissions;
  uint_fast16_t dir_permissions;
  uint_fast16_t empty_dir_permissions;
  uint_fast16_t prefix_dir_permissions;
  UsersInfos users_infos;
  SDA_UGMapping mappings;
  /// Prefix for archived/extracted paths.
  char *prefix;
  SDArchiverLinkedList *whitelist_contains_any;
  SDArchiverLinkedList *whitelist_contains_all;
  SDArchiverLinkedList *whitelist_begins;
  SDArchiverLinkedList *whitelist_ends;
  SDArchiverLinkedList *blacklist_contains_any;
  SDArchiverLinkedList *blacklist_contains_all;
  SDArchiverLinkedList *blacklist_begins;
  SDArchiverLinkedList *blacklist_ends;
  SDArchiverHashMap *not_to_compress_file_extensions;
} SDArchiverParsed;

typedef struct SDArchiverFileInfo {
  char *filename;
  /// Is NULL if not a symbolic link.
  char *link_dest;
  // xxxx xxx1 - is a directory.
  uint32_t flags;
} SDArchiverFileInfo;

typedef enum SDArchiverParsedStatus {
  SDAPS_SUCCESS,
  SDAPS_NO_USER_CWD,
} SDArchiverParsedStatus;

/// Returned c-string does not need to be free'd.
char *simple_archiver_parsed_status_to_str(SDArchiverParsedStatus status);

void simple_archiver_print_usage(void);

SDArchiverParsed simple_archiver_create_parsed(void);

/// Expects the user to pass a pointer to an SDArchiverParsed.
/// This means the user should have a SDArchiverParsed variable
/// and it should be passed with e.g. "&var".
/// Returns 0 on success.
int simple_archiver_parse_args(int argc, const char **argv,
                               SDArchiverParsed *out);

void simple_archiver_free_parsed(SDArchiverParsed *parsed);

int simple_archiver_handle_map_user_or_group(
  const char *arg,
  SDArchiverHashMap *IDToName,
  SDArchiverHashMap *NameToID,
  SDArchiverHashMap *IDToID,
  SDArchiverHashMap *NameToName);

/// Returns 0 on success. out_user is used if not NULL. out_user may hold NULL
/// if username is not found. On success, `*out_user` must be free'd.
int simple_archiver_get_uid_mapping(SDA_UGMapping mappings,
                                    UsersInfos users_infos,
                                    uint32_t uid,
                                    uint32_t *out_uid,
                                    const char **out_user);

/// Returns 0 on success. out_user is used if not NULL. out_user may hold NULL
/// if username is not found. On success, `*out_user` must be free'd.
int simple_archiver_get_user_mapping(SDA_UGMapping mappings,
                                     UsersInfos users_infos,
                                     const char *user,
                                     uint32_t *out_uid,
                                     const char **out_user);

/// Returns 0 on success. out_group is used if not NULL. out_group may hold
/// NULL if groupname is not found. On success `*out_group` must be free'd.
int simple_archiver_get_gid_mapping(SDA_UGMapping mappings,
                                    UsersInfos users_infos,
                                    uint32_t gid,
                                    uint32_t *out_gid,
                                    const char **out_group);

/// Returns 0 on success. out_group is used if not NULL. out_group may hold
/// NULL if groupname is not found. On success `*out_group` must be free'd.
int simple_archiver_get_group_mapping(SDA_UGMapping mappings,
                                      UsersInfos users_infos,
                                      const char *group,
                                      uint32_t *out_gid,
                                      const char **out_group);
#endif
