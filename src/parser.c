// ISC License
//
// Copyright (c) 2024-2025 Stephen Seo
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
// `parser.c` is the source file for parsing args.

#include "parser.h"

#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platforms.h"
#include "users.h"
#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||   \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include "data_structures/hash_map.h"
#include "data_structures/linked_list.h"
#include "helpers.h"
#include "parser_internal.h"
#include "version.h"

char *SDSA_NOT_TO_COMPRESS_FILE_EXTS[] = {
  ".simplearchive",
  ".sa",
  ".gz",
  ".xz",
  ".zst",
  ".bz2",
  ".lz",
  ".lz4",
  ".lzma",
  ".lzo",
  ".br",
  ".tgz",
  ".tbz2",
  ".tlz",
  ".txz",
  ".zip",
  ".zipx",
  ".7z",
  ".apk",
  ".jar",
  ".dmg",
  ".ogg",
  ".mp3",
  ".flac",
  ".alac",
  ".aac",
  ".jpg",
  ".jpeg",
  ".mjpeg",
  ".png",
  ".tiff",
  ".webp",
  ".webm",
  ".mp4",
  ".mkv",
  ".av1",
  ".avi",
  ".iso",
  ".img",
  NULL
};

/// Gets the first non "./"-like character in the filename.
size_t simple_archiver_parser_internal_get_first_non_current_idx(
    const char *filename) {
  size_t idx = 0;
  size_t known_good_idx = 0;
  const size_t length = strlen(filename);

  // 0b0001 - checked that idx char is '.'
  // 0b0010 - checked that idx char is '/'
  size_t flags = 0;

  for (; idx < length; ++idx) {
    if ((flags & 3) == 0) {
      if (filename[idx] == 0) {
        return known_good_idx;
      } else if (filename[idx] == '.') {
        flags |= 1;
      } else {
        return idx;
      }
    } else if ((flags & 3) == 1) {
      if (filename[idx] == 0) {
        return known_good_idx;
      } else if (filename[idx] == '/') {
        flags |= 2;
      } else {
        return idx - 1;
      }
    } else if ((flags & 3) == 3) {
      if (filename[idx] == 0) {
        return known_good_idx;
      } else if (filename[idx] == '/') {
        continue;
      } else if (filename[idx] == '.') {
        flags &= 0xFFFFFFFC;
        known_good_idx = idx;
        --idx;
        continue;
      } else {
        break;
      }
    }
  }

  if (filename[idx] == 0) {
    return known_good_idx;
  }

  return idx;
}

void simple_archiver_parser_internal_remove_end_slash(char *filename) {
  for (size_t idx = strlen(filename); idx-- > 0;) {
    if (idx == strlen(filename)) {
      continue;
    } else if (filename[idx] == '/' && idx > 0) {
      filename[idx] = 0;
    } else {
      break;
    }
  }
}

void simple_archiver_internal_free_file_info_fn(void *data) {
  SDArchiverFileInfo *file_info = data;
  if (file_info) {
    if (file_info->filename) {
      free(file_info->filename);
    }
    if (file_info->link_dest) {
      free(file_info->link_dest);
    }
  }

  free(data);
}

int list_get_last_fn(void *data, void *ud) {
  char **last = ud;
  *last = data;
  return 0;
}

int list_remove_same_str_fn(void *data, void *ud) {
  if (strcmp((char *)data, (char *)ud) == 0) {
    return 1;
  }

  return 0;
}

char *simple_archiver_parsed_status_to_str(SDArchiverParsedStatus status) {
  switch (status) {
    case SDAPS_SUCCESS:
      return "Success";
    case SDAPS_NO_USER_CWD:
      return "No user current working directory (-C <dir>)";
    default:
      return "Unknown error";
  }
}

void simple_archiver_print_usage(void) {
  fprintf(stderr, "\nUsage flags:\n");
  fprintf(stderr, "-c : create archive file\n");
  fprintf(stderr, "-x : extract archive file\n");
  fprintf(stderr, "-t : examine archive file\n");
  fprintf(stderr, "-f <filename> : filename to work on\n");
  fprintf(stderr,
          "  Use \"-f -\" to work on stdout when creating archive or stdin "
          "when reading archive\n");
  fprintf(stderr, "  NOTICE: \"-f\" is not affected by \"-C\"!\n");
  fprintf(stderr,
          "-C <dir> : Change current working directory before "
          "archiving/extracting\n");
  fprintf(stderr,
          "--prefix <prefix> | --prefix=<prefix> : set prefix for "
          "archived/extracted paths (\"/\" will be appended to the end if "
          "missing)\n");
  fprintf(stderr,
          "--compressor <full_compress_cmd> | --compressor=<cmd> : requires "
          "--decompressor and cmd must use stdin/stdout\n");
  fprintf(stderr,
          "--decompressor <full_decompress_cmd> | --decompressor=<cmd> : "
          "requires --compressor and cmd must use stdin/stdout\n");
  fprintf(stderr,
          "  Specifying \"--decompressor\" when extracting overrides archive "
          "file's stored decompressor cmd\n");
  fprintf(stderr, "--overwrite-create : allows overwriting an archive file\n");
  fprintf(stderr, "--overwrite-extract : allows overwriting when extracting\n");
  fprintf(stderr,
          "--no-abs-symlink : do not store absolute paths for symlinks\n");
  fprintf(
      stderr,
      "--preserve-symlinks : preserve the symlink's path on archive creation "
      "instead of deriving abs/relative paths, ignores \"--no-abs-symlink\" "
      "(It is not recommended to use this option, as absolute-path-symlinks "
      "may be clobbered on extraction)\n");
  fprintf(stderr,
          "--no-safe-links : keep symlinks that link to outside archive "
          "contents\n");
  fprintf(stderr,
          "--temp-files-dir <dir> | --temp-files-dir=<dir> : where to store "
          "temporary files created when compressing (defaults to same directory"
          " as output file) (this is mutually exclusive with "
          "\"--force-tmpfile\")\n");
  fprintf(stderr,
          "--force-tmpfile : Force the use of \"tmpfile()\" during "
          "compression (this is mutually exclusive with \"--temp-files-dir\")"
          "\n");
  fprintf(stderr,
          "--write-version <version> | --write-version=<version> : Force write "
          "version file format (default 5)\n");
  fprintf(stderr,
          "--chunk-min-size <bytes> | --chunk-min-size=<bytes> : minimum chunk "
          "size (default 268435456 or 256MiB) when using chunks (file formats "
          "v. 1 and up)\n  Note suffixes \"KB, KiB, MB, MiB, GB, and GiB\" are "
          "supported\n  Use like \"32MiB\" without spaces.\n");
  fprintf(stderr,
          "--no-pre-sort-files : do NOT pre-sort files by size (by default "
          "enabled so that the first file is the largest; mutually exclusive "
          "with \"--sort-files-by-name\")\n");
  fprintf(stderr,
          "--sort-files-by-name : pre-sort files by name (mutually exclusive "
          "with \"--no-pre-sort-files\").\n");
  fprintf(stderr,
          "--no-preserve-empty-dirs : do NOT preserve empty dirs (only for file"
          " format 2 and onwards)\n");
  fprintf(stderr,
          "--force-uid <uid> | --force-uid=<uid> : Force set UID on archive "
          "creation/extraction\n");
  fprintf(stderr,
          "  On archive creation, sets UID for all files/dirs in the archive.\n"
          "  On archive extraction, sets UID for all files/dirs only if EUID is"
          " 0.\n");
  fprintf(stderr,
          "--force-user <username> | --force-user=<username> : Force set UID "
          "(same as --force-uid but fetched from username)\n");
  fprintf(stderr,
          "--force-gid <gid> | --force-gid=<gid> : Force set GID on archive "
          "creation/extraction\n");
  fprintf(stderr,
          "--force-group <groupname> | --force-group=<group> : Force set GID "
          "(same as --force-gid but fetched from groupname)\n");
  fprintf(stderr,
          "  On archive creation, sets GID for all files/dirs in the archive.\n"
          "  On archive extraction, sets GID for all files/dirs only if EUID is"
          " 0.\n");
  fprintf(stderr,
          "--extract-prefer-uid : Prefer UID over Username when extracting\n");
  fprintf(stderr,
          "  Note that by default Username is preferred over UID\n");
  fprintf(stderr,
          "--extract-prefer-gid : Prefer GID over Group when extracting\n");
  fprintf(stderr,
          "  Note that by default Group is preferred over GID\n");
  fprintf(stderr,
          "--map-user <UID/Uname>:<UID/Uname> | "
          "--map-user=<UID/Uname>:<UID/Uname> : Maps a UID/Username to "
          "UID/Username\n");
  fprintf(stderr,
          "--map-group <GID/Gname>:<GID/Gname> | "
          "--map-group=<GID/Gname>:<GID/Gname> : Maps a GID/Group to "
          "GID/Group\n");
  fprintf(stderr,
          "--force-file-permissions <3-octal-values> | "
          "--force-file-permissions=<perms> : Force set permissions for files "
          "on archive creation/extraction\n  Must be three octal characters "
          "like \"755\" or \"440\"\n");
  fprintf(stderr,
          "--force-dir-permissions <3-octal-values> | "
          "--force-dir-permissions=<perms> : Force set permissions for "
          "directories on archive creation/extraction\n  Must be three octal "
          "characters like \"755\" or \"440\"\n");
  fprintf(stderr,
          "--force-empty-dir-permissions <3-octal-values> | "
          "--force-empty-dir-permissions=<perms> : Force set EMPTY dir "
          "permissions. Like \"--force-dir-permissions\", but for empty "
          "directories.\n");
  fprintf(stderr,
          "--whitelist-contains-any <text> | --whitelist-contains-any=<text> : "
          "Whitelist entries to contain \"<text>\", specify multiple times to "
          "allow entries that contain any of the specified \"<text>\"s.\n");
  fprintf(stderr,
          "--whitelist-contains-all <text> | --whitelist-contains-all=<text> : "
          "Whitelist entries to contain \"<text>\", specify multiple times to "
          "allow entries that contain all of the specified \"<text>\"s.\n");
  fprintf(stderr,
          "--whitelist-begins-with <text> | --whitelist-begins-with=<text> : "
          "Whitelist entries to start with \"<text>\", specify multiple times "
          "to allow different entries to start with different \"<text>\" "
          "entries.\n");
  fprintf(stderr,
          "--whitelist-ends-with <text> | --whitelist-ends-with=<text> : "
          "Whitelist entries to end with \"<text>\", specify multiple times to "
          "allow different entries to end with different \"<text>\" "
          "entries.\n");
  fprintf(stderr,
          "--blacklist-contains-any <text> | --blacklist-contains-any=<text> : "
          "blacklist entries that contains \"<text>\", specify multiple times "
          "to deny entries that contain any of the specified \"<text>\"s.\n");
  fprintf(stderr,
          "--blacklist-contains-all <text> | --blacklist-contains-all=<text> : "
          "blacklist entries that contains \"<text>\", specify multiple times "
          "to deny entries that contain all of the specified \"<text>\"s.\n");
  fprintf(stderr,
          "--blacklist-begins-with <text> | --blacklist-begins-with=<text> : "
          "blacklist entries that starts with \"<text>\", specify multiple "
          "times to deny multiple entries starting with different \"<text>\" "
          "entries.\n");
  fprintf(stderr,
          "--blacklist-ends-with <text> | --blacklist-ends-with=<text> : "
          "blacklist entries that ends with \"<text>\", specify multiple times "
          "to deny multiple entries ending with different \"<text>\" "
          "entries.\n");
  fprintf(stderr,
          "--wb-case-insensitive : Makes white/black-list checking case "
          "insensitive.\n");
  fprintf(stderr,
          "--print-file-exts-preset : Prints the preset extensions to stderr "
          "and stops simplearchiver.\n");
  fprintf(stderr,
          "--use-file-exts-preset : Adds preset extensions to "
          "collection of file extensions to choose to not compress\n");
  fprintf(stderr,
          "--add-file-ext <ext> | --add-file-ext=<ext> : Add a "
          "extension to choose to not compress (must be like \".thing\")\n");
  fprintf(stderr,
          "--allow-double-dot : Allows positional args to have \"..\"\n");
  fprintf(stderr, "--version : prints version and exits\n");
  fprintf(stderr,
          "-- : specifies remaining arguments are files to archive/extract\n");
  fprintf(
      stderr,
      "If creating archive file, remaining args specify files to archive.\n");
  fprintf(
      stderr,
      "If extracting archive file, remaining args specify files to extract.\n");
  fprintf(stderr,
          "Note that permissions/ownership/remapping is saved when archiving, "
          "but when extracting they are only preserved when extracting as root!"
          "\n");
}

SDArchiverParsed simple_archiver_create_parsed(void) {
  SDArchiverParsed parsed;

  parsed.flags = 0x40;
  parsed.filename = NULL;
  parsed.filename_full_abs_path = NULL;
  parsed.compressor = NULL;
  parsed.decompressor = NULL;
  parsed.working_files = simple_archiver_hash_map_init();
  parsed.working_dirs = simple_archiver_list_init();
  parsed.just_w_files = simple_archiver_hash_map_init();
  parsed.temp_dir = NULL;
  parsed.user_cwd = NULL;
  parsed.write_version = 6;
  parsed.minimum_chunk_size = 268435456;
  parsed.uid = 0;
  parsed.gid = 0;
  parsed.file_permissions = 0;
  parsed.dir_permissions = 0;
  parsed.empty_dir_permissions = 0;
  parsed.users_infos = simple_archiver_users_get_system_info();
  parsed.mappings.UidToUname = simple_archiver_hash_map_init();
  parsed.mappings.UnameToUid = simple_archiver_hash_map_init();
  parsed.mappings.UidToUid = simple_archiver_hash_map_init();
  parsed.mappings.UnameToUname = simple_archiver_hash_map_init();
  parsed.mappings.GidToGname = simple_archiver_hash_map_init();
  parsed.mappings.GnameToGid = simple_archiver_hash_map_init();
  parsed.mappings.GidToGid = simple_archiver_hash_map_init();
  parsed.mappings.GnameToGname = simple_archiver_hash_map_init();
  parsed.prefix = NULL;
  parsed.whitelist_contains_any = NULL;
  parsed.whitelist_contains_all = NULL;
  parsed.whitelist_begins = NULL;
  parsed.whitelist_ends = NULL;
  parsed.blacklist_contains_any = NULL;
  parsed.blacklist_contains_all = NULL;
  parsed.blacklist_begins = NULL;
  parsed.blacklist_ends = NULL;
  parsed.not_to_compress_file_extensions = simple_archiver_hash_map_init();

  return parsed;
}

int simple_archiver_parse_args(int argc, const char **argv,
                               SDArchiverParsed *out) {
  if (out->filename) {
    free(out->filename);
    out->filename = NULL;
  }
  if (out->filename_full_abs_path) {
    free(out->filename_full_abs_path);
    out->filename_full_abs_path = NULL;
  }
  if (out->compressor) {
    free(out->compressor);
    out->compressor = NULL;
  }
  if (out->decompressor) {
    free(out->decompressor);
    out->decompressor = NULL;
  }

  // Skip program name as it is the first arg usually.
  --argc;
  ++argv;

  int_fast8_t is_remaining_args = 0;

  __attribute((cleanup(simple_archiver_list_free)))
  SDArchiverLinkedList *working_files_list = simple_archiver_list_init();

  while (argc > 0) {
    if (!is_remaining_args) {
      if (strcmp(argv[0], "-h") == 0 || strcmp(argv[0], "--help") == 0) {
        simple_archiver_free_parsed(out);
        simple_archiver_print_usage();
        exit(0);
      } else if (strcmp(argv[0], "-c") == 0) {
        // unset first two bits.
        out->flags &= 0xFFFFFFFC;
      } else if (strcmp(argv[0], "-x") == 0) {
        // unset first two bits.
        out->flags &= 0xFFFFFFFC;
        // set first bit.
        out->flags |= 0x1;
      } else if (strcmp(argv[0], "-t") == 0) {
        // unset first two bits.
        out->flags &= 0xFFFFFFFC;
        // set second bit.
        out->flags |= 0x2;
      } else if (strcmp(argv[0], "-f") == 0) {
        if (argc < 2) {
          fprintf(stderr, "ERROR: -f specified but missing argument!\n");
          simple_archiver_print_usage();
          return 1;
        }
        if (strcmp(argv[1], "-") == 0) {
          out->flags |= 0x10;
          if (out->filename) {
            free(out->filename);
          }
          if (out->filename_full_abs_path) {
            free(out->filename_full_abs_path);
          }
          out->filename = NULL;
          out->filename_full_abs_path = NULL;
        } else {
          out->flags &= 0xFFFFFFEF;
          out->filename = strdup(argv[1]);
          out->filename_full_abs_path =
            simple_archiver_helper_real_path_to_name(argv[1]);
        }
        --argc;
        ++argv;
      } else if (strcmp(argv[0], "-C") == 0) {
        if (argc < 2) {
          fprintf(stderr, "ERROR: -C specified but missing argument!\n");
          simple_archiver_print_usage();
          return 1;
        }
        out->user_cwd = argv[1];
        --argc;
        ++argv;
      } else if (strcmp(argv[0], "--prefix") == 0
                 || strncmp(argv[0], "--prefix=", 9) == 0) {
        int_fast8_t is_separate = strcmp(argv[0], "--prefix") == 0 ? 1 : 0;
        const char *str;
        if (is_separate && argc < 2) {
          fprintf(stderr, "ERROR: --prefix specified but missing prefix!\n");
          simple_archiver_print_usage();
          return 1;
        } else if (is_separate) {
          str = argv[1];
        } else {
          str = argv[0] + 9;
        }
        if (strlen(str) == 0) {
          fprintf(stderr, "ERROR: Argument is an empty string!\n");
          simple_archiver_print_usage();
          return 1;
        }
        SAHelperPrefixValResult prefix_val_result =
          simple_archiver_helper_validate_prefix(str);
        if (prefix_val_result != SAHPrefixVal_OK) {
          fprintf(stderr,
                  "ERROR: Invalid prefix: %s\n",
                  simple_archiver_helper_prefix_result_str(prefix_val_result));
          return 1;
        }
        const unsigned long prefix_length = strlen(str);
        if (str[prefix_length - 1] == '/') {
          out->prefix = strdup(str);
        } else {
          out->prefix = malloc(prefix_length + 2);
          memcpy(out->prefix, str, prefix_length);
          out->prefix[prefix_length] = '/';
          out->prefix[prefix_length + 1] = 0;
        }
        if (is_separate) {
          --argc;
          ++argv;
        }
      } else if (strcmp(argv[0], "--compressor") == 0
                 || strncmp(argv[0], "--compressor=", 13) == 0) {
        int_fast8_t is_separate = strcmp(argv[0], "--compressor") == 0 ? 1 : 0;
        const char *str;
        if (is_separate && argc < 2) {
          fprintf(stderr, "--compressor specfied but missing argument!\n");
          simple_archiver_print_usage();
          return 1;
        } else if (is_separate) {
          str = argv[1];
        } else {
          str = argv[0] + 13;
        }
        if (strlen(str) == 0) {
          fprintf(stderr, "ERROR: Argument is an empty string!\n");
          simple_archiver_print_usage();
          return 1;
        }
        size_t size = strlen(str) + 1;
        out->compressor = malloc(size);
        strncpy(out->compressor, str, size);
        if (is_separate) {
          --argc;
          ++argv;
        }
      } else if (strcmp(argv[0], "--decompressor") == 0
                 || strncmp(argv[0], "--decompressor=", 15) == 0) {
        int_fast8_t is_separate =
          strcmp(argv[0], "--decompressor") == 0 ? 1 : 0;
        const char *str;
        if (is_separate && argc < 2) {
          fprintf(stderr, "--decompressor specfied but missing argument!\n");
          simple_archiver_print_usage();
          return 1;
        } else if (is_separate) {
          str = argv[1];
        } else {
          str = argv[0] + 15;
        }
        if (strlen(str) == 0) {
          fprintf(stderr, "ERROR: Argument is an empty string!\n");
          simple_archiver_print_usage();
          return 1;
        }
        size_t size = strlen(str) + 1;
        out->decompressor = malloc(size);
        strncpy(out->decompressor, str, size);
        if (is_separate) {
          --argc;
          ++argv;
        }
      } else if (strcmp(argv[0], "--overwrite-create") == 0) {
        out->flags |= 0x4;
      } else if (strcmp(argv[0], "--overwrite-extract") == 0) {
        out->flags |= 0x8;
      } else if (strcmp(argv[0], "--no-abs-symlink") == 0) {
        out->flags |= 0x20;
      } else if (strcmp(argv[0], "--preserve-symlinks") == 0) {
        out->flags |= 0x100;
      } else if (strcmp(argv[0], "--no-safe-links") == 0) {
        out->flags |= 0x80;
        fprintf(stderr,
                "NOTICE: Disabling safe-links, symlinks that point to outside "
                "archived files will be preserved!\n");
      } else if (strcmp(argv[0], "--temp-files-dir") == 0
                 || strncmp(argv[0], "--temp-files-dir=", 17) == 0) {
        int_fast8_t is_separate =
          strcmp(argv[0], "--temp-files-dir") == 0 ? 1 : 0;
        const char *str;
        if (is_separate && argc < 2) {
          fprintf(stderr, "ERROR: --temp-files-dir is missing an argument!\n");
          simple_archiver_print_usage();
          return 1;
        } else if (is_separate) {
          str = argv[1];
        } else {
          str = argv[0] + 17;
        }
        if (strlen(str) == 0) {
          fprintf(stderr, "ERROR: Argument is an empty string!\n");
          simple_archiver_print_usage();
          return 1;
        }
        out->temp_dir = simple_archiver_helper_real_path_to_name(str);
        if (is_separate) {
          --argc;
          ++argv;
        }
      } else if (strcmp(argv[0], "--force-tmpfile") == 0) {
        out->flags |= 0x40000;
      } else if (strcmp(argv[0], "--write-version") == 0
                 || strncmp(argv[0], "--write-version=", 16) == 0) {
        int_fast8_t is_separate =
          strcmp(argv[0], "--write-version") == 0 ? 1 : 0;
        const char *str;
        if (is_separate && argc < 2) {
          fprintf(stderr,
                  "ERROR: --write-version expects an integer argument!\n");
          simple_archiver_print_usage();
          return 1;
        } else if (is_separate) {
          str = argv[1];
        } else {
          str = argv[0] + 16;
        }
        if (strlen(str) == 0) {
          fprintf(stderr, "ERROR: Argument is an empty string!\n");
          simple_archiver_print_usage();
          return 1;
        }
        int version = atoi(str);
        if (version < 0) {
          fprintf(stderr, "ERROR: --write-version cannot be negative!\n");
          simple_archiver_print_usage();
          return 1;
        } else if (version > 6) {
          fprintf(stderr,
                  "ERROR: --write-version must be 0, 1, 2, 3, 4, 5, or 6!\n");
          simple_archiver_print_usage();
          return 1;
        }
        out->write_version = (uint32_t)version;
        if (is_separate) {
          --argc;
          ++argv;
        }
      } else if (strcmp(argv[0], "--chunk-min-size") == 0
                 || strncmp(argv[0], "--chunk-min-size=", 17) == 0) {
        int_fast8_t is_separate =
          strcmp(argv[0], "--chunk-min-size") == 0 ? 1 : 0;
        const char *str;
        if (is_separate && argc < 2) {
          fprintf(stderr,
                  "ERROR: --chunk-min-size expects an integer argument!\n");
          simple_archiver_print_usage();
          return 1;
        } else if (is_separate) {
          str = argv[1];
        } else {
          str = argv[0] + 17;
        }
        if (strlen(str) == 0) {
          fprintf(stderr, "ERROR: Argument is an empty string!\n");
          simple_archiver_print_usage();
          return 1;
        }

        out->minimum_chunk_size = 0;
        char suffix_buf[4];
        size_t sb_idx = 0;
        int_fast8_t digits_passed = 0;

        for (const char *c = str; *c != 0; ++c) {
          if (!digits_passed && *c >= '0' && *c <= '9') {
            out->minimum_chunk_size =
              out->minimum_chunk_size * 10 + (uint64_t)(*c - '0');
          } else {
            digits_passed = 1;
            suffix_buf[sb_idx++] = *c;
            if (sb_idx > 3) {
              fprintf(stderr, "ERROR: Invalid arg to --chunk-min-size!\n");
              return 1;
            }
          }
        }
        suffix_buf[sb_idx] = 0;

        if (sb_idx > 0) {
          uint64_t temp = out->minimum_chunk_size;
          if (strcmp(suffix_buf, "KB") == 0) {
            out->minimum_chunk_size *= 1000;
          } else if (strcmp(suffix_buf, "KiB") == 0) {
            out->minimum_chunk_size *= 1024;
          } else if (strcmp(suffix_buf, "MB") == 0) {
            out->minimum_chunk_size *= 1000 * 1000;
          } else if (strcmp(suffix_buf, "MiB") == 0) {
            out->minimum_chunk_size *= 1024 * 1024;
          } else if (strcmp(suffix_buf, "GB") == 0) {
            out->minimum_chunk_size *= 1000 * 1000 * 1000;
          } else if (strcmp(suffix_buf, "GiB") == 0) {
            out->minimum_chunk_size *= 1024 * 1024 * 1024;
          } else {
            fprintf(stderr,
                    "ERROR: Invalid suffix \"%s\"! Expected KB, KiB, MB, MiB, "
                    "GB, or GiB!\n",
                    suffix_buf);
            return 1;
          }

          fprintf(stderr,
                  "Using chunk size %" PRIu64 "%s (%" PRIu64 " Bytes)...\n",
                  temp,
                  suffix_buf,
                  out->minimum_chunk_size);
        } else if (out->minimum_chunk_size != 0) {
          fprintf(stderr,
                  "Using chunk size %" PRIu64 "...\n",
                  out->minimum_chunk_size);
        } else {
          fprintf(stderr, "ERROR: --chunk-min-size cannot be zero!\n");
          simple_archiver_print_usage();
          return 1;
        }

        if (is_separate) {
          --argc;
          ++argv;
        }
      } else if (strcmp(argv[0], "--no-pre-sort-files") == 0) {
        if (out->flags & 0x80000) {
          fprintf(stderr, "ERROR: \"--no-pre-sort-files\" is mutually "
              "exclusive with \"--sort-files-by-name\"!\n");
          return 1;
        }
        out->flags &= 0xFFFFFFBF;
      } else if (strcmp(argv[0], "--sort-files-by-name") == 0) {
        if ((out->flags & 0x40) == 0) {
          fprintf(stderr, "ERROR: \"--sort-files-by-name\" is mutually "
              "exclusive with \"--no-pre-sort-files\"!\n");
          return 1;
        }
        out->flags |= 0x80000;
      } else if (strcmp(argv[0], "--no-preserve-empty-dirs") == 0) {
        out->flags |= 0x200;
      } else if (strcmp(argv[0], "--force-uid") == 0
                 || strncmp(argv[0], "--force-uid=", 12) == 0) {
        int_fast8_t is_separate = strcmp(argv[0], "--force-uid") == 0 ? 1 : 0;
        const char *str;
        if (is_separate && argc < 2) {
          fprintf(stderr, "ERROR: --force-uid expects an integer argument!\n");
          simple_archiver_print_usage();
          return 1;
        } else if (is_separate) {
          str = argv[1];
        } else {
          str = argv[0] + 12;
        }
        if (strlen(str) == 0) {
          fprintf(stderr, "ERROR: Argument is an empty string!\n");
          simple_archiver_print_usage();
          return 1;
        }
        unsigned long long uid = strtoull(str, NULL, 10);
        if (uid == 0 && strcmp(str, "0") != 0) {
          fprintf(stderr, "ERROR: Failed to parse --force-uid <UID>!\n");
          simple_archiver_print_usage();
          return 1;
        } else if (uid > 0xFFFFFFFF) {
          fprintf(stderr,
                  "ERROR: UID Is too large (expecting unsigned 32-bit "
                  "value)!\n");
          simple_archiver_print_usage();
          return 1;
        }
        out->uid = (uint32_t)uid;
        out->flags |= 0x400;
        if (is_separate) {
          --argc;
          ++argv;
        }
      } else if (strcmp(argv[0], "--force-user") == 0
                 || strncmp(argv[0], "--force-user=", 13) == 0) {
        int_fast8_t is_separate = strcmp(argv[0], "--force-user") == 0 ? 1 : 0;
        const char *str;
        if (is_separate && argc < 2) {
          fprintf(stderr, "ERROR: --force-user expects a username!\n");
          simple_archiver_print_usage();
          return 1;
        } else if (is_separate) {
          str = argv[1];
        } else {
          str = argv[0] + 13;
        }
        if (strlen(str) == 0) {
          fprintf(stderr, "ERROR: Argument is an empty string!\n");
          simple_archiver_print_usage();
          return 1;
        }
        uint32_t *uid = simple_archiver_hash_map_get(
          out->users_infos.UnameToUid, str, strlen(str) + 1);
        if (!uid) {
          fprintf(stderr, "ERROR: --force-user got invalid username!\n");
          simple_archiver_print_usage();
          return 1;
        }
        out->uid = *uid;
        out->flags |= 0x400;
        if (is_separate) {
          --argc;
          ++argv;
        }
      } else if (strcmp(argv[0], "--force-gid") == 0
                 || strncmp(argv[0], "--force-gid=", 12) == 0) {
        int_fast8_t is_separate = strcmp(argv[0], "--force-gid") == 0 ? 1 : 0;
        const char *str;
        if (is_separate && argc < 2) {
          fprintf(stderr, "ERROR: --force-gid expects an integer argument!\n");
          simple_archiver_print_usage();
          return 1;
        } else if (is_separate) {
          str = argv[1];
        } else {
          str = argv[0] + 12;
        }
        if (strlen(str) == 0) {
          fprintf(stderr, "ERROR: Argument is an empty string!\n");
          simple_archiver_print_usage();
          return 1;
        }
        unsigned long long gid = strtoull(str, NULL, 10);
        if (gid == 0 && strcmp(str, "0") != 0) {
          fprintf(stderr, "ERROR: Failed to parse --force-gid <GID>!\n");
          simple_archiver_print_usage();
          return 1;
        } else if (gid > 0xFFFFFFFF) {
          fprintf(stderr,
                  "ERROR: GID Is too large (expecting unsigned 32-bit "
                  "value)!\n");
          simple_archiver_print_usage();
          return 1;
        }
        out->gid = (uint32_t)gid;
        out->flags |= 0x800;
        if (is_separate) {
          --argc;
          ++argv;
        }
      } else if (strcmp(argv[0], "--force-group") == 0
                 || strncmp(argv[0], "--force-group=", 14) == 0) {
        int_fast8_t is_separate = strcmp(argv[0], "--force-group") == 0 ? 1 : 0;
        const char *str;
        if (is_separate && argc < 2) {
          fprintf(stderr, "ERROR: --force-group expects a group name!\n");
          simple_archiver_print_usage();
          return 1;
        } else if (is_separate) {
          str = argv[1];
        } else {
          str = argv[0] + 14;
        }
        if (strlen(str) == 0) {
          fprintf(stderr, "ERROR: Argument is an empty string!\n");
          simple_archiver_print_usage();
          return 1;
        }
        uint32_t *gid = simple_archiver_hash_map_get(
          out->users_infos.GnameToGid, str, strlen(str) + 1);
        if (!gid) {
          fprintf(stderr, "ERROR: --force-group got invalid group!\n");
          simple_archiver_print_usage();
          return 1;
        }
        out->gid = *gid;
        out->flags |= 0x800;
        if (is_separate) {
          --argc;
          ++argv;
        }
      } else if (strcmp(argv[0], "--extract-prefer-uid") == 0) {
        out->flags |= 0x4000;
      } else if (strcmp(argv[0], "--extract-prefer-gid") == 0) {
        out->flags |= 0x8000;
      } else if (strcmp(argv[0], "--map-user") == 0
                 || strncmp(argv[0], "--map-user=", 11) == 0) {
        int_fast8_t is_separate = strcmp(argv[0], "--map-user") == 0 ? 1 : 0;
        const char *str;
        if (is_separate && argc < 2) {
          fprintf(stderr, "ERROR: --map-user requires an argument!\n"
                  "  <UID/Username>:<UID/Username>, like \"1000:someuser\" or "
                  "\"myuser:thisuser\" or \"thatuser:1011\" etc.\n");
          simple_archiver_print_usage();
          return 1;
        } else if (is_separate) {
          str = argv[1];
        } else {
          str = argv[0] + 11;
        }
        if (strlen(str) == 0) {
          fprintf(stderr, "ERROR: Argument is an empty string!\n");
          simple_archiver_print_usage();
          return 1;
        }
        if (simple_archiver_handle_map_user_or_group(
            str,
            out->mappings.UidToUname,
            out->mappings.UnameToUid,
            out->mappings.UidToUid,
            out->mappings.UnameToUname) != 0) {
          simple_archiver_print_usage();
          return 1;
        }
        if (is_separate) {
          --argc;
          ++argv;
        }
      } else if (strcmp(argv[0], "--map-group") == 0
                 || strncmp(argv[0], "--map-group=", 12) == 0) {
        int_fast8_t is_separate = strcmp(argv[0], "--map-group") == 0 ? 1 : 0;
        const char *str;
        if (is_separate && argc < 2) {
          fprintf(stderr, "ERROR: --map-group requires an argument!\n"
                  "  <GID/Group>:<GID/Group>, like \"1000:audio\" or "
                  "\"cups:wheel\" or \"users:1011\" etc.\n");
          simple_archiver_print_usage();
          return 1;
        } else if (is_separate) {
          str = argv[1];
        } else {
          str = argv[0] + 12;
        }
        if (strlen(str) == 0) {
          fprintf(stderr, "ERROR: Argument is an empty string!\n");
          simple_archiver_print_usage();
          return 1;
        }
        if (simple_archiver_handle_map_user_or_group(
            str,
            out->mappings.GidToGname,
            out->mappings.GnameToGid,
            out->mappings.GidToGid,
            out->mappings.GnameToGname) != 0) {
          simple_archiver_print_usage();
          return 1;
        }
        if (is_separate) {
          --argc;
          ++argv;
        }
      } else if (strcmp(argv[0], "--force-file-permissions") == 0
                 || strncmp(argv[0], "--force-file-permissions=", 25) == 0) {
        int_fast8_t is_separate =
          strcmp(argv[0], "--force-file-permissions") == 0 ? 1 : 0;
        const char *str;
        if (is_separate
            && (argc < 2
            || strlen(argv[1]) != 3
            || (!(argv[1][0] >= '0' && argv[1][0] <= '7'))
            || (!(argv[1][1] >= '0' && argv[1][1] <= '7'))
            || (!(argv[1][2] >= '0' && argv[1][2] <= '7'))
              )) {
          fprintf(stderr,
                  "ERROR: --force-file-permissions expects 3 octal values "
                  "(e.g. \"755\" or \"440\")!\n");
          simple_archiver_print_usage();
          return 1;
        } else if (is_separate) {
          str = argv[1];
        } else {
          str = argv[0] + 25;
          if (strlen(str) != 3
              || !(str[0] >= '0' && str[0] <= '7')
              || !(str[1] >= '0' && str[1] <= '7')
              || !(str[2] >= '0' && str[2] <= '7')) {
            fprintf(stderr,
                    "ERROR: --force-file-permissions expects 3 octal values "
                    "(e.g. \"755\" or \"440\")!\n");
            simple_archiver_print_usage();
            return 1;
          }
        }
        if (strlen(str) == 0) {
          fprintf(stderr, "ERROR: Argument is an empty string!\n");
          simple_archiver_print_usage();
          return 1;
        }

        uint_fast8_t value = (uint_fast8_t)(str[0] - '0');
        out->file_permissions |= (value & 4) ? 1 : 0;
        out->file_permissions |= (value & 2) ? 2 : 0;
        out->file_permissions |= (value & 1) ? 4 : 0;

        value = (uint_fast8_t)(str[1] - '0');
        out->file_permissions |= (value & 4) ? 8 : 0;
        out->file_permissions |= (value & 2) ? 0x10 : 0;
        out->file_permissions |= (value & 1) ? 0x20 : 0;

        value = (uint_fast8_t)(str[2] - '0');
        out->file_permissions |= (value & 4) ? 0x40 : 0;
        out->file_permissions |= (value & 2) ? 0x80 : 0;
        out->file_permissions |= (value & 1) ? 0x100 : 0;

        out->flags |= 0x1000;

        if (is_separate) {
          --argc;
          ++argv;
        }
      } else if (strcmp(argv[0], "--force-dir-permissions") == 0
                 || strncmp(argv[0], "--force-dir-permissions=", 24) == 0) {
        int_fast8_t is_separate =
          strcmp(argv[0], "--force-dir-permissions") == 0 ? 1 : 0;
        const char *str;
        if (is_separate
            && (argc < 2
            || strlen(argv[1]) != 3
            || (!(argv[1][0] >= '0' && argv[1][0] <= '7'))
            || (!(argv[1][1] >= '0' && argv[1][1] <= '7'))
            || (!(argv[1][2] >= '0' && argv[1][2] <= '7'))
              )) {
          fprintf(stderr,
                  "ERROR: --force-dir-permissions expects 3 octal values "
                  "(e.g. \"755\" or \"440\")!\n");
          simple_archiver_print_usage();
          return 1;
        } else if (is_separate) {
          str = argv[1];
        } else {
          str = argv[0] + 24;
          if (strlen(str) != 3
              || !(str[0] >= '0' && str[0] <= '7')
              || !(str[1] >= '0' && str[1] <= '7')
              || !(str[2] >= '0' && str[2] <= '7')) {
            fprintf(stderr,
                    "ERROR: --force-dir-permissions expects 3 octal values "
                    "(e.g. \"755\" or \"440\")!\n");
            simple_archiver_print_usage();
            return 1;
          }
        }
        if (strlen(str) == 0) {
          fprintf(stderr, "ERROR: Argument is an empty string!\n");
          simple_archiver_print_usage();
          return 1;
        }

        uint_fast8_t value = (uint_fast8_t)(str[0] - '0');
        out->dir_permissions |= (value & 4) ? 1 : 0;
        out->dir_permissions |= (value & 2) ? 2 : 0;
        out->dir_permissions |= (value & 1) ? 4 : 0;

        value = (uint_fast8_t)(str[1] - '0');
        out->dir_permissions |= (value & 4) ? 8 : 0;
        out->dir_permissions |= (value & 2) ? 0x10 : 0;
        out->dir_permissions |= (value & 1) ? 0x20 : 0;

        value = (uint_fast8_t)(str[2] - '0');
        out->dir_permissions |= (value & 4) ? 0x40 : 0;
        out->dir_permissions |= (value & 2) ? 0x80 : 0;
        out->dir_permissions |= (value & 1) ? 0x100 : 0;

        out->flags |= 0x2000;

        if (is_separate) {
          --argc;
          ++argv;
        }
      } else if (strcmp(argv[0], "--force-empty-dir-permissions") == 0
                 || strncmp(argv[0],
                            "--force-empty-dir-permissions=",
                            30) == 0) {
        int_fast8_t is_separate =
          strcmp(argv[0], "--force-empty-dir-permissions") == 0 ? 1 : 0;
        const char *str;
        if (is_separate
            && (argc < 2
            || strlen(argv[1]) != 3
            || (!(argv[1][0] >= '0' && argv[1][0] <= '7'))
            || (!(argv[1][1] >= '0' && argv[1][1] <= '7'))
            || (!(argv[1][2] >= '0' && argv[1][2] <= '7'))
              )) {
          fprintf(stderr,
                  "ERROR: --force-empty-dir-permissions expects 3 octal values"
                  " (e.g. \"755\" or \"440\")!\n");
          simple_archiver_print_usage();
          return 1;
        } else if (is_separate) {
          str = argv[1];
        } else {
          str = argv[0] + 30;
          if (strlen(str) != 3
              || !(str[0] >= '0' && str[0] <= '7')
              || !(str[1] >= '0' && str[1] <= '7')
              || !(str[2] >= '0' && str[2] <= '7')) {
            fprintf(stderr,
                    "ERROR: --force-empty-dir-permissions expects 3 octal "
                    "values (e.g. \"755\" or \"440\")!\n");
            simple_archiver_print_usage();
            return 1;
          }
        }
        if (strlen(str) == 0) {
          fprintf(stderr, "ERROR: Argument is an empty string!\n");
          simple_archiver_print_usage();
          return 1;
        }

        uint_fast8_t value = (uint_fast8_t)(str[0] - '0');
        out->empty_dir_permissions |= (value & 4) ? 1 : 0;
        out->empty_dir_permissions |= (value & 2) ? 2 : 0;
        out->empty_dir_permissions |= (value & 1) ? 4 : 0;

        value = (uint_fast8_t)(str[1] - '0');
        out->empty_dir_permissions |= (value & 4) ? 8 : 0;
        out->empty_dir_permissions |= (value & 2) ? 0x10 : 0;
        out->empty_dir_permissions |= (value & 1) ? 0x20 : 0;

        value = (uint_fast8_t)(str[2] - '0');
        out->empty_dir_permissions |= (value & 4) ? 0x40 : 0;
        out->empty_dir_permissions |= (value & 2) ? 0x80 : 0;
        out->empty_dir_permissions |= (value & 1) ? 0x100 : 0;

        out->flags |= 0x10000;

        if (is_separate) {
          --argc;
          ++argv;
        }
      } else if (strcmp(argv[0], "--whitelist-contains-any") == 0
                 || strncmp(argv[0], "--whitelist-contains-any=", 25) == 0) {
        int_fast8_t is_separate =
          strcmp(argv[0], "--whitelist-contains-any") == 0 ? 1 : 0;
        const char *str;
        if (is_separate && argc < 2) {
          fprintf(stderr,
                  "ERROR: --whitelist-contains-any expects an argument!\n");
          simple_archiver_print_usage();
          return 1;
        } else if (is_separate) {
          str = argv[1];
        } else {
          str = argv[0] + 25;
        }
        if (strlen(str) == 0) {
          fprintf(stderr, "ERROR: Argument is an empty string!\n");
          simple_archiver_print_usage();
          return 1;
        }

        if (!out->whitelist_contains_any) {
          out->whitelist_contains_any = simple_archiver_list_init();
        }
        simple_archiver_list_add(
          out->whitelist_contains_any,
          (void *)str,
          simple_archiver_helper_datastructure_cleanup_nop);

        if (is_separate) {
          --argc;
          ++argv;
        }
      } else if (strcmp(argv[0], "--whitelist-contains-all") == 0
                 || strncmp(argv[0], "--whitelist-contains-all=", 25) == 0) {
        int_fast8_t is_separate =
          strcmp(argv[0], "--whitelist-contains-all") == 0 ? 1 : 0;
        const char *str;
        if (is_separate && argc < 2) {
          fprintf(stderr,
                  "ERROR: --whitelist-contains-all expects an argument!\n");
          simple_archiver_print_usage();
          return 1;
        } else if (is_separate) {
          str = argv[1];
        } else {
          str = argv[0] + 25;
        }
        if (strlen(str) == 0) {
          fprintf(stderr, "ERROR: Argument is an empty string!\n");
          simple_archiver_print_usage();
          return 1;
        }

        if (!out->whitelist_contains_all) {
          out->whitelist_contains_all = simple_archiver_list_init();
        }
        simple_archiver_list_add(
          out->whitelist_contains_all,
          (void *)str,
          simple_archiver_helper_datastructure_cleanup_nop);

        if (is_separate) {
          --argc;
          ++argv;
        }
      } else if (strcmp(argv[0], "--whitelist-begins-with") == 0
                 || strncmp(argv[0], "--whitelist-begins-with=", 24) == 0) {
        int_fast8_t is_separate =
          strcmp(argv[0], "--whitelist-begins-with") == 0 ? 1 : 0;
        const char *str;
        if (is_separate && argc < 2) {
          fprintf(stderr,
                  "ERROR: --whitelist-begins-with expects an argument!\n");
          simple_archiver_print_usage();
          return 1;
        } else if (is_separate) {
          str = argv[1];
        } else {
          str = argv[0] + 24;
        }
        if (strlen(str) == 0) {
          fprintf(stderr, "ERROR: Argument is an empty string!\n");
          simple_archiver_print_usage();
          return 1;
        }

        if (!out->whitelist_begins) {
          out->whitelist_begins = simple_archiver_list_init();
        }
        simple_archiver_list_add(
          out->whitelist_begins,
          (void *)str,
          simple_archiver_helper_datastructure_cleanup_nop);

        if (is_separate) {
          --argc;
          ++argv;
        }
      } else if (strcmp(argv[0], "--whitelist-ends-with") == 0
                 || strncmp(argv[0], "--whitelist-ends-with=", 22) == 0) {
        int_fast8_t is_separate =
          strcmp(argv[0], "--whitelist-ends-with") == 0 ? 1 : 0;
        const char *str;
        if (is_separate && argc < 2) {
          fprintf(stderr,
                  "ERROR: --whitelist-ends-with expects an argument!\n");
          simple_archiver_print_usage();
          return 1;
        } else if (is_separate) {
          str = argv[1];
        } else {
          str = argv[0] + 22;
        }
        if (strlen(str) == 0) {
          fprintf(stderr, "ERROR: Argument is an empty string!\n");
          simple_archiver_print_usage();
          return 1;
        }

        if (!out->whitelist_ends) {
          out->whitelist_ends = simple_archiver_list_init();
        }
        simple_archiver_list_add(
          out->whitelist_ends,
          (void *)str,
          simple_archiver_helper_datastructure_cleanup_nop);

        if (is_separate) {
          --argc;
          ++argv;
        }
      } else if (strcmp(argv[0], "--blacklist-contains-any") == 0
                 || strncmp(argv[0], "--blacklist-contains-any=", 25) == 0) {
        int_fast8_t is_separate =
          strcmp(argv[0], "--blacklist-contains-any") == 0 ? 1 : 0;
        const char *str;
        if (is_separate && argc < 2) {
          fprintf(stderr,
                  "ERROR: --blacklist-contains-any expects an argument!\n");
          simple_archiver_print_usage();
          return 1;
        } else if (is_separate) {
          str = argv[1];
        } else {
          str = argv[0] + 25;
        }
        if (strlen(str) == 0) {
          fprintf(stderr, "ERROR: Argument is an empty string!\n");
          simple_archiver_print_usage();
          return 1;
        }

        if (!out->blacklist_contains_any) {
          out->blacklist_contains_any = simple_archiver_list_init();
        }
        simple_archiver_list_add(
          out->blacklist_contains_any,
          (void *)str,
          simple_archiver_helper_datastructure_cleanup_nop);

        if (is_separate) {
          --argc;
          ++argv;
        }
      } else if (strcmp(argv[0], "--blacklist-contains-all") == 0
                 || strncmp(argv[0], "--blacklist-contains-all=", 25) == 0) {
        int_fast8_t is_separate =
          strcmp(argv[0], "--blacklist-contains-all") == 0 ? 1 : 0;
        const char *str;
        if (is_separate && argc < 2) {
          fprintf(stderr,
                  "ERROR: --blacklist-contains-all expects an argument!\n");
          simple_archiver_print_usage();
          return 1;
        } else if (is_separate) {
          str = argv[1];
        } else {
          str = argv[0] + 25;
        }
        if (strlen(str) == 0) {
          fprintf(stderr, "ERROR: Argument is an empty string!\n");
          simple_archiver_print_usage();
          return 1;
        }

        if (!out->blacklist_contains_all) {
          out->blacklist_contains_all = simple_archiver_list_init();
        }
        simple_archiver_list_add(
          out->blacklist_contains_all,
          (void *)str,
          simple_archiver_helper_datastructure_cleanup_nop);

        if (is_separate) {
          --argc;
          ++argv;
        }
      } else if (strcmp(argv[0], "--blacklist-begins-with") == 0
                 || strncmp(argv[0], "--blacklist-begins-with=", 24) == 0) {
        int_fast8_t is_separate =
          strcmp(argv[0], "--blacklist-begins-with") == 0 ? 1 : 0;
        const char *str;
        if (is_separate && argc < 2) {
          fprintf(stderr,
                  "ERROR: --blacklist-begins-with expects an argument!\n");
          simple_archiver_print_usage();
          return 1;
        } else if (is_separate) {
          str = argv[1];
        } else {
          str = argv[0] + 24;
        }
        if (strlen(str) == 0) {
          fprintf(stderr, "ERROR: Argument is an empty string!\n");
          simple_archiver_print_usage();
          return 1;
        }

        if (!out->blacklist_begins) {
          out->blacklist_begins = simple_archiver_list_init();
        }
        simple_archiver_list_add(
          out->blacklist_begins,
          (void *)str,
          simple_archiver_helper_datastructure_cleanup_nop);

        if (is_separate) {
          --argc;
          ++argv;
        }
      } else if (strcmp(argv[0], "--blacklist-ends-with") == 0
                 || strncmp(argv[0], "--blacklist-ends-with=", 22) == 0) {
        int_fast8_t is_separate =
          strcmp(argv[0], "--blacklist-ends-with") == 0 ? 1 : 0;
        const char *str;
        if (is_separate && argc < 2) {
          fprintf(stderr,
                  "ERROR: --blacklist-ends-with expects an argument!\n");
          simple_archiver_print_usage();
          return 1;
        } else if (is_separate) {
          str = argv[1];
        } else {
          str = argv[0] + 22;
        }
        if (strlen(str) == 0) {
          fprintf(stderr, "ERROR: Argument is an empty string!\n");
          simple_archiver_print_usage();
          return 1;
        }

        if (!out->blacklist_ends) {
          out->blacklist_ends = simple_archiver_list_init();
        }
        simple_archiver_list_add(
          out->blacklist_ends,
          (void *)str,
          simple_archiver_helper_datastructure_cleanup_nop);

        if (is_separate) {
          --argc;
          ++argv;
        }
      } else if (strcmp(argv[0], "--wb-case-insensitive") == 0) {
        out->flags |= 0x20000;
      } else if (strcmp(argv[0], "--print-file-exts-preset") == 0) {
        for (char **ext = SDSA_NOT_TO_COMPRESS_FILE_EXTS;
            *ext != NULL;
            ++ext) {
          fprintf(stderr, "%s ", *ext);
        }
        fprintf(stderr, "\n");
        exit(0);
      } else if (strcmp(argv[0], "--use-file-exts-preset") == 0) {
        for (char **ext = SDSA_NOT_TO_COMPRESS_FILE_EXTS;
            *ext != NULL;
            ++ext) {
          char *str = simple_archiver_helper_to_lower(*ext);
          if (!simple_archiver_hash_map_get(
                out->not_to_compress_file_extensions,
                str,
                strlen(str))) {
            simple_archiver_hash_map_insert(
                out->not_to_compress_file_extensions,
                (void*)1,
                str,
                strlen(str),
                simple_archiver_helper_datastructure_cleanup_nop,
                NULL);
          } else {
            free(str);
          }
        }
      } else if (strcmp(argv[0], "--add-file-ext") == 0
          || strncmp(argv[0], "--add-file-ext=", 15) == 0) {
        int_fast8_t is_separate =
          strcmp(argv[0], "--add-file-ext") == 0 ? 1 : 0;
        const char *str;
        if (is_separate && argc < 2) {
          fprintf(stderr,
                  "ERROR: --add-file-ext expects an argument!\n");
          simple_archiver_print_usage();
          return 1;
        } else if (is_separate) {
          str = argv[1];
        } else {
          str = argv[0] + 15;
        }
        char *to_lower = simple_archiver_helper_to_lower(str);
        if (!simple_archiver_hash_map_get(
              out->not_to_compress_file_extensions,
              to_lower,
              strlen(to_lower))) {
          simple_archiver_hash_map_insert(
              out->not_to_compress_file_extensions,
              (void*)1,
              to_lower,
              strlen(to_lower),
              simple_archiver_helper_datastructure_cleanup_nop,
              NULL);
        } else {
          fprintf(stderr,
                  "WARNING: File extension \"%s\" already added.\n",
                  str);
          free(to_lower);
        }

        if (is_separate) {
          --argc;
          ++argv;
        }
      } else if (strcmp(argv[0], "--allow-double-dot") == 0) {
        out->flags |= 0x100000;
      } else if (strcmp(argv[0], "--version") == 0) {
        fprintf(stderr, "Version: %s\n", SIMPLE_ARCHIVER_VERSION_STR);
        exit(0);
      } else if (argv[0][0] == '-' && argv[0][1] == '-' && argv[0][2] == 0) {
        is_remaining_args = 1;
      } else if (argv[0][0] != '-') {
        is_remaining_args = 1;
        continue;
      } else {
        fprintf(stderr, "ERROR: Got invalid arg \"%s\"!\n", argv[0]);
        simple_archiver_print_usage();
        return 1;
      }
    } else {
      size_t arg_idx =
          simple_archiver_parser_internal_get_first_non_current_idx(argv[0]);
      size_t arg_length = strlen(argv[0] + arg_idx) + 1;
      const char *arg_ptr = argv[0] + arg_idx;

      if ((out->flags & 0x100000) == 0
          && simple_archiver_helper_contains_double_dot_path(arg_ptr)) {
        fprintf(stderr,
                "ERROR: Path contains \"..\"! Use \"--allow-double-dot\" if "
                "this is intended!\n");
        return 1;
      }

      simple_archiver_hash_map_insert(
        out->just_w_files,
        (void*)arg_ptr,
        (void*)arg_ptr,
        arg_length,
        simple_archiver_helper_datastructure_cleanup_nop,
        simple_archiver_helper_datastructure_cleanup_nop);
      simple_archiver_list_add(
        working_files_list,
        (void*)arg_ptr,
        simple_archiver_helper_datastructure_cleanup_nop);
    }

    --argc;
    ++argv;
  }

  if ((out->flags & 0x3) == 0) {
    // Process working_files_list to get mapping of arg -> SDArchiverFileInfo.
    // First change cwd.
    __attribute__((cleanup(
        simple_archiver_helper_cleanup_chdir_back))) char *original_cwd = NULL;
    if (out->user_cwd) {
      original_cwd = realpath(".", NULL);
      if (chdir(out->user_cwd)) {
        fprintf(stderr,
                "ERROR: Failed to change cwd via \"-C %s\"!\n",
                out->user_cwd);
        return 1;
      }
    }
    // Setup data structures.
    __attribute__((cleanup(simple_archiver_hash_map_free)))
    SDArchiverHashMap *hash_map = simple_archiver_hash_map_init();
    int hash_map_sentinel = 1;
    // Work with each file.
    for (SDArchiverLLNode *node = working_files_list->head->next;
         node != working_files_list->tail;
         node = node->next) {
      struct stat st;
      memset(&st, 0, sizeof(struct stat));
      char *file_path = node->data;
      fstatat(AT_FDCWD, file_path, &st, AT_SYMLINK_NOFOLLOW);
      if ((st.st_mode & S_IFMT) == S_IFREG
          || (st.st_mode & S_IFMT) == S_IFLNK) {
        // Is a regular file or a symbolic link.
        size_t len = strlen(file_path) + 1;
        char *filename = malloc(len);
        strncpy(filename, file_path, len);
        if (simple_archiver_hash_map_get(hash_map, filename, len - 1) == NULL) {
          SDArchiverFileInfo *file_info = malloc(sizeof(SDArchiverFileInfo));
          file_info->filename = filename;
          file_info->link_dest = NULL;
          file_info->flags = 0;
          if ((st.st_mode & S_IFMT) == S_IFLNK) {
            // Is a symlink.
            file_info->link_dest = malloc(MAX_SYMBOLIC_LINK_SIZE);
            ssize_t count = readlinkat(AT_FDCWD, filename, file_info->link_dest,
                                       MAX_SYMBOLIC_LINK_SIZE - 1);
            if (count >= (ssize_t)MAX_SYMBOLIC_LINK_SIZE - 1) {
              file_info->link_dest[MAX_SYMBOLIC_LINK_SIZE - 1] = 0;
            } else if (count > 0) {
              file_info->link_dest[count] = 0;
            } else {
              // Failure.
              fprintf(stderr,
                      "WARNING: Could not get link info for file \"%s\"!\n",
                      file_info->filename);
              free(file_info->link_dest);
              free(file_info);
              free(filename);
              continue;
            }
          } else {
            // Is a regular file.
            file_info->link_dest = NULL;
            // Check that the file is readable by opening it. Easier than to
            // check permissions because that would also require checking if the
            // current USER can open the file.
            FILE *readable_file = fopen(file_info->filename, "rb");
            if (!readable_file) {
              // Cannot open file, so it must be unreadable (at least by the
              // current USER).
              fprintf(stderr, "WARNING: \"%s\" is not readable, skipping!\n",
                      file_info->filename);
              free(file_info->link_dest);
              free(file_info);
              free(filename);
              continue;
            } else {
              fclose(readable_file);
              // fprintf(stderr, "DEBUG: \"%s\" is readable.\n",
              // file_info->filename);
            }
          }
          // Store unprocessed filename in map to avoid duplicates.
          simple_archiver_hash_map_insert(
              hash_map, &hash_map_sentinel, strdup(filename), len - 1,
              simple_archiver_helper_datastructure_cleanup_nop,
              NULL);
          // Remove leading "./" entries from files_list.
          size_t idx =
            simple_archiver_parser_internal_get_first_non_current_idx(
              file_info->filename);
          if (idx > 0) {
            size_t len = strlen(file_info->filename) + 1 - idx;
            char *substr = malloc(len);
            strncpy(substr, file_info->filename + idx, len);
            free(file_info->filename);
            file_info->filename = substr;
          }
          // Remove "./" entries inside the file path.
          int_fast8_t slash_found = 0;
          int_fast8_t dot_found = 0;
          for (idx = strlen(file_info->filename); idx-- > 0;) {
            if (file_info->filename[idx] == '/') {
              if (dot_found) {
                char *temp = simple_archiver_helper_cut_substr(
                  file_info->filename, idx + 1, idx + 3);
                free(file_info->filename);
                file_info->filename = temp;
              } else {
                slash_found = 1;
                continue;
              }
            } else if (file_info->filename[idx] == '.' && slash_found) {
              dot_found = 1;
              continue;
            }
            slash_found = 0;
            dot_found = 0;
          }
          // Store the processed file_info against the arg.
          if(simple_archiver_hash_map_insert(
              out->working_files,
              file_info,
              node->data,
              strlen(node->data) + 1,
              simple_archiver_internal_free_file_info_fn,
              simple_archiver_helper_datastructure_cleanup_nop)) {
            fprintf(
              stderr,
              "ERROR: Internal error (%d): duplicate working files \"%s\"!\n",
              __LINE__,
              (char*)node->data);
            return 1;
          }
        } else {
          free(filename);
        }
      } else if ((st.st_mode & S_IFMT) == S_IFDIR) {
        // Is a directory.
        char *dir_path = realpath(file_path, NULL);
        char *temp_user_cwd_realpath = realpath(".", NULL);
        if (strcmp(dir_path, temp_user_cwd_realpath) == 0) {
          free(dir_path);
          free(temp_user_cwd_realpath);
        } else {
          free(dir_path);
          free(temp_user_cwd_realpath);

          simple_archiver_parser_internal_remove_end_slash(file_path);

          if (file_path[0] == '/') {
            dir_path = realpath(file_path, NULL);
            for (size_t idx = 0; dir_path[idx] != 0; ++idx) {
              if (dir_path[idx] == '/' && idx > 0) {
                char *outer = strdup(dir_path);
                outer[idx] = 0;
                simple_archiver_list_add(out->working_dirs, outer, NULL);
              }
            }
            simple_archiver_list_add(out->working_dirs, dir_path, NULL);
          } else {
            dir_path = strdup(file_path);
            simple_archiver_list_add(out->working_dirs, dir_path, NULL);
          }
        }

        __attribute__((cleanup(simple_archiver_list_free)))
        SDArchiverLinkedList *dir_list = simple_archiver_list_init();
        simple_archiver_list_add(
            dir_list, file_path,
            simple_archiver_helper_datastructure_cleanup_nop);
        char *next;
        while (dir_list->count != 0) {
          simple_archiver_list_get(dir_list, list_get_last_fn, &next);
          if (!next) {
            break;
          }
          DIR *dir = opendir(next);
          if (!dir) {
            if (simple_archiver_list_remove(dir_list,
                                            list_remove_same_str_fn,
                                            next) == 0) {
              break;
            }
            continue;
          }
          struct dirent *dir_entry;
          uint_fast8_t is_dir_empty = 1;
          do {
            dir_entry = readdir(dir);
            if (dir_entry) {
              if (strcmp(dir_entry->d_name, ".") == 0 ||
                  strcmp(dir_entry->d_name, "..") == 0) {
                continue;
              }
              is_dir_empty = 0;
              // fprintf(stderr, "dir entry in %s is %s\n", next,
              // dir_entry->d_name);
              size_t combined_size =
                strlen(next) + strlen(dir_entry->d_name) + 2;
              char *combined_path = malloc(combined_size);
              snprintf(combined_path, combined_size, "%s/%s", next,
                       dir_entry->d_name);
              size_t valid_idx =
                simple_archiver_parser_internal_get_first_non_current_idx(
                  combined_path);
              if (valid_idx > 0) {
                char *new_path = malloc(combined_size - valid_idx);
                strncpy(new_path, combined_path + valid_idx,
                        combined_size - valid_idx);
                free(combined_path);
                combined_path = new_path;
                combined_size -= valid_idx;
              }
              memset(&st, 0, sizeof(struct stat));
              fstatat(AT_FDCWD, combined_path, &st, AT_SYMLINK_NOFOLLOW);
              if ((st.st_mode & S_IFMT) == S_IFREG ||
                  (st.st_mode & S_IFMT) == S_IFLNK) {
                // Is a file or a symbolic link.
                if (simple_archiver_hash_map_get(hash_map, combined_path,
                                                 combined_size - 1) == NULL) {
                  SDArchiverFileInfo *file_info =
                      malloc(sizeof(SDArchiverFileInfo));
                  file_info->filename = combined_path;
                  file_info->link_dest = NULL;
                  file_info->flags = 0;
                  if ((st.st_mode & S_IFMT) == S_IFLNK) {
                    // Is a symlink.
                    file_info->link_dest = malloc(MAX_SYMBOLIC_LINK_SIZE);
                    ssize_t count =
                        readlinkat(AT_FDCWD,
                                   combined_path,
                                   file_info->link_dest,
                                   MAX_SYMBOLIC_LINK_SIZE - 1);
                    if (count >= (ssize_t)MAX_SYMBOLIC_LINK_SIZE - 1) {
                      file_info->link_dest[MAX_SYMBOLIC_LINK_SIZE - 1] = 0;
                    } else if (count > 0) {
                      file_info->link_dest[count] = 0;
                    } else {
                      // Failure.
                      free(file_info->link_dest);
                      free(file_info);
                      free(combined_path);
                      continue;
                    }
                  } else {
                    // Is a regular file.
                    file_info->link_dest = NULL;
                    // Check that the file is readable by opening it. Easier
                    // than to check permissions because that would also require
                    // checking if the current USER can open the file.
                    FILE *readable_file = fopen(file_info->filename, "rb");
                    if (!readable_file) {
                      // Cannot open file, so it must be unreadable (at least by
                      // the current USER).
                      fprintf(stderr,
                              "WARNING: \"%s\" is not readable, skipping!\n",
                              file_info->filename);
                      free(file_info->link_dest);
                      free(file_info);
                      free(combined_path);
                      continue;
                    } else {
                      fclose(readable_file);
                      // fprintf(stderr, "DEBUG: \"%s\" is readable.\n",
                      // file_info->filename);
                    }
                  }

                  // Store unprocessed filename in map to avoid duplicates.
                  simple_archiver_hash_map_insert(
                      hash_map, &hash_map_sentinel, strdup(combined_path),
                      combined_size - 1,
                      simple_archiver_helper_datastructure_cleanup_nop,
                      NULL);
                  // Remove leading "./" entries from files_list.
                  size_t idx =
                    simple_archiver_parser_internal_get_first_non_current_idx(
                      file_info->filename);
                  if (idx > 0) {
                    size_t len = strlen(file_info->filename) + 1 - idx;
                    char *substr = malloc(len);
                    strncpy(substr, file_info->filename + idx, len);
                    free(file_info->filename);
                    file_info->filename = substr;
                  }
                  // Remove "./" entries inside the file path.
                  int_fast8_t slash_found = 0;
                  int_fast8_t dot_found = 0;
                  for (idx = strlen(file_info->filename); idx-- > 0;) {
                    if (file_info->filename[idx] == '/') {
                      if (dot_found) {
                        char *temp =
                          simple_archiver_helper_cut_substr(
                            file_info->filename, idx + 1, idx + 3);
                        free(file_info->filename);
                        file_info->filename = temp;
                      } else {
                        slash_found = 1;
                        continue;
                      }
                    } else if (file_info->filename[idx] == '.' && slash_found) {
                      dot_found = 1;
                      continue;
                    }
                    slash_found = 0;
                    dot_found = 0;
                  }
                  // Store the processed file_info against the arg.
                  if(simple_archiver_hash_map_insert(
                      out->working_files,
                      file_info,
                      node->data,
                      strlen(node->data) + 1,
                      simple_archiver_internal_free_file_info_fn,
                      simple_archiver_helper_datastructure_cleanup_nop)) {
                    fprintf(
                      stderr,
                      "ERROR: Internal error (%d): duplicate working files "
                         "\"%s\"!\n",
                      __LINE__,
                      (char*)node->data);
                    return 1;
                  }
                } else {
                  free(combined_path);
                }
              } else if ((st.st_mode & S_IFMT) == S_IFDIR) {
                // Is a directory.
                simple_archiver_list_add(out->working_dirs,
                                         strdup(combined_path),
                                         NULL);
                simple_archiver_list_add_front(dir_list, combined_path, NULL);
              } else {
                fprintf(stderr,
                        "NOTICE: Not a file, symlink, or directory: \"%s\"."
                          " Skipping...\n",
                        combined_path);
                free(combined_path);
              }
            }
          } while (dir_entry != NULL);
          closedir(dir);

          if (is_dir_empty
              && (out->flags & 0x200) == 0
              && out->write_version >= 2) {
            SDArchiverFileInfo *f_info = malloc(sizeof(SDArchiverFileInfo));
            f_info->filename = strdup(next);
            f_info->link_dest = NULL;
            f_info->flags = 1;

            // Remove leading "./" entries from files_list.
            size_t idx =
              simple_archiver_parser_internal_get_first_non_current_idx(
                f_info->filename);
            if (idx > 0) {
              size_t len = strlen(f_info->filename) + 1 - idx;
              char *substr = malloc(len);
              strncpy(substr, f_info->filename + idx, len);
              free(f_info->filename);
              f_info->filename = substr;
            }
            // Remove "./" entries inside the file path.
            int_fast8_t slash_found = 0;
            int_fast8_t dot_found = 0;
            for (idx = strlen(f_info->filename); idx-- > 0;) {
              if (f_info->filename[idx] == '/') {
                if (dot_found) {
                  char *temp =
                    simple_archiver_helper_cut_substr(
                      f_info->filename, idx + 1, idx + 3);
                  free(f_info->filename);
                  f_info->filename = temp;
                } else {
                  slash_found = 1;
                  continue;
                }
              } else if (f_info->filename[idx] == '.' && slash_found) {
                dot_found = 1;
                continue;
              }
              slash_found = 0;
              dot_found = 0;
            }
            // Store the processed file_info against the arg.
            if(simple_archiver_hash_map_insert(
                out->working_files,
                f_info,
                node->data,
                strlen(node->data) + 1,
                simple_archiver_internal_free_file_info_fn,
                simple_archiver_helper_datastructure_cleanup_nop)) {
              fprintf(
                stderr,
                "ERROR: Internal error (%d): duplicate working files \"%s\"!\n",
                __LINE__,
                (char*)node->data);
              return 1;
            }
          }

          if (simple_archiver_list_remove(dir_list, list_remove_same_str_fn,
                                          next) == 0) {
            break;
          }
        }
      } else {
        fprintf(stderr,
                "NOTICE: Not a file, symlink, or directory: \"%s\"."
                  " Skipping...\n",
                file_path);
      }
    }
  }

  return 0;
}

void simple_archiver_free_parsed(SDArchiverParsed *parsed) {
  parsed->flags = 0;
  if (parsed->filename) {
    free(parsed->filename);
    parsed->filename = NULL;
  }
  if (parsed->filename_full_abs_path) {
    free(parsed->filename_full_abs_path);
    parsed->filename_full_abs_path = NULL;
  }
  if (parsed->compressor) {
    free(parsed->compressor);
    parsed->compressor = NULL;
  }
  if (parsed->decompressor) {
    free(parsed->decompressor);
    parsed->decompressor = NULL;
  }
  if (parsed->working_files) {
    simple_archiver_hash_map_free(&parsed->working_files);
  }
  if (parsed->working_dirs) {
    simple_archiver_list_free(&parsed->working_dirs);
  }
  if (parsed->just_w_files) {
    simple_archiver_hash_map_free(&parsed->just_w_files);
  }

  if (parsed->temp_dir) {
    free(parsed->temp_dir);
    parsed->temp_dir = NULL;
  }

  simple_archiver_users_free_users_infos(&parsed->users_infos);

  if (parsed->mappings.UidToUname) {
    simple_archiver_hash_map_free(&parsed->mappings.UidToUname);
  }
  if (parsed->mappings.UnameToUid) {
    simple_archiver_hash_map_free(&parsed->mappings.UnameToUid);
  }
  if (parsed->mappings.UidToUid) {
    simple_archiver_hash_map_free(&parsed->mappings.UidToUid);
  }
  if (parsed->mappings.UnameToUname) {
    simple_archiver_hash_map_free(&parsed->mappings.UnameToUname);
  }
  if (parsed->mappings.GidToGname) {
    simple_archiver_hash_map_free(&parsed->mappings.GidToGname);
  }
  if (parsed->mappings.GnameToGid) {
    simple_archiver_hash_map_free(&parsed->mappings.GnameToGid);
  }
  if (parsed->mappings.GidToGid) {
    simple_archiver_hash_map_free(&parsed->mappings.GidToGid);
  }
  if (parsed->mappings.GnameToGname) {
    simple_archiver_hash_map_free(&parsed->mappings.GnameToGname);
  }

  if (parsed->prefix) {
    free(parsed->prefix);
  }

  if (parsed->whitelist_contains_any) {
    simple_archiver_list_free(&parsed->whitelist_contains_any);
  }
  if (parsed->whitelist_contains_all) {
    simple_archiver_list_free(&parsed->whitelist_contains_all);
  }
  if (parsed->whitelist_begins) {
    simple_archiver_list_free(&parsed->whitelist_begins);
  }
  if (parsed->whitelist_ends) {
    simple_archiver_list_free(&parsed->whitelist_ends);
  }
  if (parsed->blacklist_contains_any) {
    simple_archiver_list_free(&parsed->blacklist_contains_any);
  }
  if (parsed->blacklist_contains_all) {
    simple_archiver_list_free(&parsed->blacklist_contains_all);
  }
  if (parsed->blacklist_begins) {
    simple_archiver_list_free(&parsed->blacklist_begins);
  }
  if (parsed->blacklist_ends) {
    simple_archiver_list_free(&parsed->blacklist_ends);
  }
  if (parsed->not_to_compress_file_extensions) {
    simple_archiver_hash_map_free(&parsed->not_to_compress_file_extensions);
  }
}

int simple_archiver_handle_map_user_or_group(
    const char *arg,
    SDArchiverHashMap *IDToName,
    SDArchiverHashMap *NameToID,
    SDArchiverHashMap *IDToID,
    SDArchiverHashMap *NameToName) {
  const unsigned long arg_len = strlen(arg);

  int32_t colon_idx = -1;
  for (int32_t idx = 0; (unsigned long)idx < arg_len; ++idx) {
    if (arg[idx] == ':') {
      if (colon_idx == -1) {
        colon_idx = idx;
      } else {
        fprintf(stderr,
                "ERROR: Encountered multiple \":\" in --map-user arg!\n");
        return 1;
      }
    }
  }

  if (colon_idx == -1) {
    fprintf(stderr, "ERROR: No \":\" in --map-user arg!\n");
    return 1;
  } else if (colon_idx == 0) {
    fprintf(stderr, "ERROR: Colon in arg before ID/Name!\n");
    return 1;
  } else if ((unsigned long)colon_idx + 1 == arg_len) {
    fprintf(stderr, "ERROR: Colon in arg at end, no end-ID/Name!\n");
    return 1;
  }

  uint_fast8_t first_is_numeric = 1;
  uint_fast8_t last_is_numeric = 1;

  for (uint32_t idx = 0; idx < (uint32_t)colon_idx; ++idx) {
    if (arg[idx] < '0' || arg[idx] > '9') {
      first_is_numeric = 0;
      break;
    }
  }

  for (uint32_t idx = (uint32_t)colon_idx + 1; idx < (uint32_t)arg_len; ++idx) {
    if (arg[idx] < '0' || arg[idx] > '9') {
      last_is_numeric = 0;
      break;
    }
  }

  __attribute__((cleanup(simple_archiver_helper_cleanup_c_string)))
  char *first_buf = malloc((size_t)colon_idx + 1);
  __attribute__((cleanup(simple_archiver_helper_cleanup_c_string)))
  char *last_buf = malloc((size_t)arg_len - (size_t)colon_idx);

  __attribute__((cleanup(simple_archiver_helper_cleanup_uint32)))
  uint32_t *first_id = NULL;
  __attribute__((cleanup(simple_archiver_helper_cleanup_uint32)))
  uint32_t *last_id = NULL;

  memcpy(first_buf, arg, (size_t)colon_idx);
  first_buf[colon_idx] = 0;

  memcpy(last_buf,
         arg + colon_idx + 1,
         (size_t)arg_len - (size_t)colon_idx - 1);
  last_buf[(size_t)arg_len - (size_t)colon_idx - 1] = 0;

  if (first_is_numeric) {
    unsigned long integer = strtoul(first_buf, NULL, 10);
    if (integer > 0xFFFFFFFF) {
      fprintf(stderr, "ERROR: ID integer \"%s\" is too large!\n", first_buf);
      return 1;
    }
    first_id = malloc(sizeof(uint32_t));
    *first_id = (uint32_t)integer;
  }

  if (last_is_numeric) {
    unsigned long integer = strtoul(last_buf, NULL, 10);
    if (integer > 0xFFFFFFFF) {
      fprintf(stderr, "ERROR: ID integer \"%s\" is too large!\n", last_buf);
      return 1;
    }
    last_id = malloc(sizeof(uint32_t));
    *last_id = (uint32_t)integer;
  }

  if (first_is_numeric && last_is_numeric) {
    uint32_t *mapped_id = simple_archiver_hash_map_get(IDToID,
                                                       first_id,
                                                       sizeof(uint32_t));
    if (mapped_id) {
      fprintf(stderr,
              "ERROR: Mapping with key ID \"%" PRIu32 "\" already exists!\n",
              *first_id);
      fprintf(stderr,
              "  Already mapped to ID \"%" PRIu32 "\".\n",
              *mapped_id);
      return 1;
    }
    const char *mapped_name = simple_archiver_hash_map_get(IDToName,
                                                           first_id,
                                                           sizeof(uint32_t));
    if (mapped_name) {
      fprintf(stderr,
              "ERROR: Mapping with key ID \"%" PRIu32 "\" already exists!\n",
              *first_id);
      fprintf(stderr,
              "  Already mapped to name \"%s\".\n",
              mapped_name);
      return 1;
    }
    if (simple_archiver_hash_map_insert(IDToID,
                                        last_id,
                                        first_id,
                                        sizeof(uint32_t),
                                        NULL,
                                        NULL) != 0) {
      // Values are free'd by insert fn on failure.
      last_id = NULL;
      first_id = NULL;
      fprintf(stderr,
              "ERROR: Internal error storing ID to ID mapping \"%s\"!",
              arg);
      return 1;
    }
    // Map takes ownership of values.
    last_id = NULL;
    first_id = NULL;
  } else if (first_is_numeric) {
    uint32_t *mapped_id = simple_archiver_hash_map_get(IDToID,
                                                       first_id,
                                                       sizeof(uint32_t));
    if (mapped_id) {
      fprintf(stderr,
              "ERROR: Mapping with key ID \"%" PRIu32 "\" already exists!\n",
              *first_id);
      fprintf(stderr,
              "  Already mapped to ID \"%" PRIu32 "\".\n",
              *mapped_id);
      return 1;
    }
    const char *mapped_name = simple_archiver_hash_map_get(IDToName,
                                                           first_id,
                                                           sizeof(uint32_t));
    if (mapped_name) {
      fprintf(stderr,
              "ERROR: Mapping with key ID \"%" PRIu32 "\" already exists!\n",
              *first_id);
      fprintf(stderr,
              "  Already mapped to name \"%s\".\n",
              mapped_name);
      return 1;
    }
    if (simple_archiver_hash_map_insert(IDToName,
                                        last_buf,
                                        first_id,
                                        sizeof(uint32_t),
                                        NULL,
                                        NULL) != 0) {
      // Values are free'd by insert fn on failure.
      last_buf = NULL;
      first_id = NULL;
      fprintf(stderr,
              "ERROR: Internal error storing ID to Name mapping \"%s\"!",
              arg);
      return 1;
    }
    // Map takes ownership of values.
    last_buf = NULL;
    first_id = NULL;
  } else if (last_is_numeric) {
    uint32_t *mapped_id = simple_archiver_hash_map_get(NameToID,
                                                       first_buf,
                                                       strlen(first_buf) + 1);
    if (mapped_id) {
      fprintf(stderr,
              "ERROR: Mapping with key name \"%s\" already exists!\n",
              first_buf);
      fprintf(stderr,
              "  Already mapped to ID \"%" PRIu32 "\".\n",
              *mapped_id);
      return 1;
    }
    const char *mapped_name = simple_archiver_hash_map_get(NameToName,
                                                           first_buf,
                                                           strlen(first_buf)
                                                             + 1);
    if (mapped_name) {
      fprintf(stderr,
              "ERROR: Mapping with key name \"%s\" already exists!\n",
              first_buf);
      fprintf(stderr,
              "  Already mapped to name \"%s\".\n",
              mapped_name);
      return 1;
    }
    if (simple_archiver_hash_map_insert(NameToID,
                                        last_id,
                                        first_buf,
                                        strlen(first_buf) + 1,
                                        NULL,
                                        NULL) != 0) {
      // Values are free'd by insert fn on failure.
      last_id = NULL;
      first_buf = NULL;
      fprintf(stderr,
              "ERROR: Internal error storing Name to ID mapping \"%s\"!",
              arg);
      return 1;
    }
    // Map takes ownership of values.
    last_id = NULL;
    first_buf = NULL;
  } else {
    uint32_t *mapped_id = simple_archiver_hash_map_get(NameToID,
                                                       first_buf,
                                                       strlen(first_buf) + 1);
    if (mapped_id) {
      fprintf(stderr,
              "ERROR: Mapping with key name \"%s\" already exists!\n",
              first_buf);
      fprintf(stderr,
              "  Already mapped to ID \"%" PRIu32 "\".\n",
              *mapped_id);
      return 1;
    }
    const char *mapped_name = simple_archiver_hash_map_get(NameToName,
                                                           first_buf,
                                                           strlen(first_buf)
                                                             + 1);
    if (mapped_name) {
      fprintf(stderr,
              "ERROR: Mapping with key name \"%s\" already exists!\n",
              first_buf);
      fprintf(stderr,
              "  Already mapped to name \"%s\".\n",
              mapped_name);
      return 1;
    }
    if (simple_archiver_hash_map_insert(NameToName,
                                        last_buf,
                                        first_buf,
                                        strlen(first_buf) + 1,
                                        NULL,
                                        NULL) != 0) {
      // Values are free'd by insert fn on failure.
      last_buf = NULL;
      first_buf = NULL;
      fprintf(stderr,
              "ERROR: Internal error storing Name to Name mapping \"%s\"!",
              arg);
      return 1;
    }
    // Map takes ownership of values.
    last_buf = NULL;
    first_buf = NULL;
  }

  return 0;
}

int simple_archiver_get_uid_mapping(SDA_UGMapping mappings,
                                    UsersInfos users_infos,
                                    uint32_t uid,
                                    uint32_t *out_uid,
                                    const char **out_user) {
  uint32_t *get_uid = simple_archiver_hash_map_get(mappings.UidToUid,
                                                   &uid,
                                                   sizeof(uint32_t));
  const char *get_user;
  if (get_uid) {
    *out_uid = *get_uid;
    if (out_user) {
      if (*out_user) {
        free((void *)*out_user);
      }
      get_user = simple_archiver_hash_map_get(users_infos.UidToUname,
                                              get_uid,
                                              sizeof(uint32_t));
      if (get_user) {
        *out_user = strdup(get_user);
      } else {
        *out_user = NULL;
      }
    }
    return 0;
  }

  get_user = simple_archiver_hash_map_get(mappings.UidToUname,
                                                      &uid,
                                                      sizeof(uint32_t));
  if (get_user) {
    get_uid = simple_archiver_hash_map_get(users_infos.UnameToUid,
                                           get_user,
                                           strlen(get_user) + 1);
    if (get_uid) {
      *out_uid = *get_uid;
      if (out_user) {
        if (*out_user) {
          free((void *)*out_user);
        }
        *out_user = strdup(get_user);
      }
      return 0;
    }
  }

  return 1;
}

int simple_archiver_get_user_mapping(SDA_UGMapping mappings,
                                     UsersInfos users_infos,
                                     const char *user,
                                     uint32_t *out_uid,
                                     const char **out_user) {
  uint32_t *get_uid = simple_archiver_hash_map_get(mappings.UnameToUid,
                                                   user,
                                                   strlen(user) + 1);
  char *get_user;
  if (get_uid) {
    *out_uid = *get_uid;
    if (out_user) {
      if (*out_user) {
        free((void *)*out_user);
      }
      get_user = simple_archiver_hash_map_get(users_infos.UidToUname,
                                              get_uid,
                                              sizeof(uint32_t));
      if (get_user) {
        *out_user = strdup(get_user);
      } else {
        *out_user = NULL;
      }
    }
    return 0;
  }

  get_user = simple_archiver_hash_map_get(mappings.UnameToUname,
                                          user,
                                          strlen(user) + 1);
  if (get_user) {
    get_uid = simple_archiver_hash_map_get(users_infos.UnameToUid,
                                           get_user,
                                           strlen(get_user) + 1);
    if (get_uid) {
      *out_uid = *get_uid;
      if (out_user) {
        if (*out_user) {
          free((void *)*out_user);
        }
        *out_user = strdup(get_user);
      }
      return 0;
    }
  }

  return 1;
}

int simple_archiver_get_gid_mapping(SDA_UGMapping mappings,
                                    UsersInfos users_infos,
                                    uint32_t gid,
                                    uint32_t *out_gid,
                                    const char **out_group) {
  uint32_t *get_gid = simple_archiver_hash_map_get(mappings.GidToGid,
                                                   &gid,
                                                   sizeof(uint32_t));
  char *get_group;
  if (get_gid) {
    *out_gid = *get_gid;
    if (out_group) {
      if (*out_group) {
        free((void *)*out_group);
      }
      get_group = simple_archiver_hash_map_get(users_infos.GidToGname,
                                               get_gid,
                                               sizeof(uint32_t));
      if (get_group) {
        *out_group = strdup(get_group);
      } else {
        *out_group = NULL;
      }
    }
    return 0;
  }

  get_group = simple_archiver_hash_map_get(mappings.GidToGname,
                                           &gid,
                                           sizeof(uint32_t));
  if (get_group) {
    get_gid = simple_archiver_hash_map_get(users_infos.GnameToGid,
                                           get_group,
                                           strlen(get_group) + 1);
    if (get_gid) {
      *out_gid = *get_gid;
      if (out_group) {
        if (*out_group) {
          free((void *)*out_group);
        }
        *out_group = strdup(get_group);
      }
      return 0;
    }
  }

  return 1;
}

int simple_archiver_get_group_mapping(SDA_UGMapping mappings,
                                      UsersInfos users_infos,
                                      const char *group,
                                      uint32_t *out_gid,
                                      const char **out_group) {
  uint32_t *get_gid = simple_archiver_hash_map_get(mappings.GnameToGid,
                                                   group,
                                                   strlen(group) + 1);
  char *get_group;
  if (get_gid) {
    *out_gid = *get_gid;
    if (out_group) {
      if (*out_group) {
        free((void *)*out_group);
      }
      get_group = simple_archiver_hash_map_get(users_infos.GidToGname,
                                               get_gid,
                                               sizeof(uint32_t));
      if (get_group) {
        *out_group = strdup(get_group);
      } else {
        *out_group = NULL;
      }
    }
    return 0;
  }

  get_group = simple_archiver_hash_map_get(mappings.GnameToGname,
                                           group,
                                           strlen(group) + 1);
  if (get_group) {
    get_gid = simple_archiver_hash_map_get(users_infos.GnameToGid,
                                           get_group,
                                           strlen(get_group) + 1);
    if (get_gid) {
      *out_gid = *get_gid;
      if (out_group) {
        if (*out_group) {
          free((void *)*out_group);
        }
        *out_group = strdup(get_group);
      }
      return 0;
    }
  }

  return 1;
}
