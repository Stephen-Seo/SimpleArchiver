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
// `archiver.c` is the source for an interface to creating an archive file.

#include "archiver.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "platforms.h"
#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <signal.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#endif

#include "data_structures/priority_heap.h"
#include "helpers.h"
#include "users.h"

#define TEMP_FILENAME_CMP "%s%ssimple_archiver_compressed_%zu.tmp"
#define FILE_COUNTS_OUTPUT_FORMAT_STR_0 \
  "\nFile %%%zu" PRIu32 " of %%%zu" PRIu32 ".\n"
#define FILE_COUNTS_OUTPUT_FORMAT_STR_1 "[%%%zuzu/%%%zuzu]\n"

#define SIMPLE_ARCHIVER_BUFFER_SIZE (1024 * 32)

#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
volatile int is_sig_pipe_occurred = 0;
volatile int is_sig_int_occurred = 0;

void handle_sig_pipe(int sig) {
  if (sig == SIGPIPE) {
    is_sig_pipe_occurred = 1;
  }
}

void handle_sig_int(int sig) {
  if (sig == SIGINT) {
    is_sig_int_occurred = 1;
  }
}

const struct timespec nonblock_sleep = {.tv_sec = 0, .tv_nsec = 1000000};
#endif

typedef struct SDArchiverInternalToWrite {
  void *buf;
  uint64_t size;
} SDArchiverInternalToWrite;

typedef struct SDArchiverInternalFileInfo {
  char *filename;
  uint8_t bit_flags[4];
  uint32_t uid;
  uint32_t gid;
  char *username;
  char *groupname;
  uint64_t file_size;
  /// xxxx xxx1 - is invalid.
  /// xxxx xx1x - white/black-list allowed.
  int_fast8_t other_flags;
} SDArchiverInternalFileInfo;

void free_internal_to_write(void *data) {
  SDArchiverInternalToWrite *to_write = data;
  free(to_write->buf);
  free(data);
}

int write_list_datas_fn(void *data, void *ud) {
  SDArchiverInternalToWrite *to_write = data;
  FILE *out_f = ud;

  fwrite(to_write->buf, 1, to_write->size, out_f);
  return 0;
}

void cleanup_temp_filename_delete(void ***ptrs_array) {
#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
  if (ptrs_array && *ptrs_array) {
    if ((*ptrs_array)[1]) {
      simple_archiver_helper_cleanup_FILE((FILE **)(*ptrs_array)[1]);
    }
    if ((*ptrs_array)[0]) {
      unlink((char *)((*ptrs_array)[0]));
    }
    free(*ptrs_array);
    *ptrs_array = NULL;
  }
#endif
}

void cleanup_overwrite_filename_delete_simple(char **filename) {
  if (filename && *filename) {
    if ((*filename)[0] != 0) {
#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
      unlink(*filename);
#endif
    }
    free(*filename);
    *filename = NULL;
  }
}

int write_files_fn_file_v0(void *data, void *ud) {
  if (is_sig_int_occurred) {
    return 1;
  }

  const SDArchiverFileInfo *file_info = data;
  SDArchiverState *state = ud;

  __attribute__((cleanup(simple_archiver_list_free)))
  SDArchiverLinkedList *to_write = simple_archiver_list_init();
  SDArchiverInternalToWrite *temp_to_write;

  if (!file_info->filename) {
    // Invalid entry, no filename.
    return 1;
  } else if (!state->out_f) {
    // Invalid out-ptr.
    return 1;
  } else if (!file_info->link_dest) {
    // Regular file, not a symbolic link.
    if (state->parsed->compressor && state->parsed->decompressor) {
      // De/compressor specified.

#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
      // Use temp file to store compressed data.
      char temp_filename[512];
      size_t idx = 0;
      size_t temp_dir_end = strlen(state->parsed->temp_dir);
      snprintf(temp_filename, 512, TEMP_FILENAME_CMP, state->parsed->temp_dir,
               state->parsed->temp_dir[temp_dir_end - 1] == '/' ? "" : "/",
               idx);
      do {
        FILE *test_fd = fopen(temp_filename, "rb");
        if (test_fd) {
          // File exists.
          fclose(test_fd);
          snprintf(temp_filename, 512, TEMP_FILENAME_CMP,
                   state->parsed->temp_dir,
                   state->parsed->temp_dir[temp_dir_end - 1] == '/' ? "" : "/",
                   ++idx);
        } else if (idx > 0xFFFF) {
          // Sanity check.
          return 1;
        } else {
          break;
        }
      } while (1);
      __attribute__((cleanup(simple_archiver_helper_cleanup_FILE)))
      FILE *file_fd = fopen(file_info->filename, "rb");
      if (!file_fd) {
        // Unable to open file for compressing and archiving.
        return 1;
      }
      __attribute__((cleanup(simple_archiver_helper_cleanup_FILE)))
      FILE *tmp_fd = fopen(temp_filename, "wb");
      __attribute__((cleanup(cleanup_temp_filename_delete))) void **ptrs_array =
          malloc(sizeof(void *) * 2);
      ptrs_array[0] = NULL;
      ptrs_array[1] = NULL;
      if (!tmp_fd) {
        tmp_fd = tmpfile();
        if (!tmp_fd) {
          fprintf(stderr,
                  "ERROR: Unable to create temp file for compressing!\n");
          return 1;
        }
      } else {
        ptrs_array[0] = temp_filename;
        ptrs_array[1] = &tmp_fd;
      }

      // Handle SIGPIPE.
      signal(SIGPIPE, handle_sig_pipe);

      int pipe_into_cmd[2];
      int pipe_outof_cmd[2];
      pid_t compressor_pid;

      if (pipe(pipe_into_cmd) != 0) {
        // Unable to create pipes.
        return 1;
      } else if (pipe(pipe_outof_cmd) != 0) {
        // Unable to create second set of pipes.
        close(pipe_into_cmd[0]);
        close(pipe_into_cmd[1]);
        return 1;
      } else if (fcntl(pipe_into_cmd[1], F_SETFL, O_NONBLOCK) != 0) {
        // Unable to set non-blocking on into-write-pipe.
        close(pipe_into_cmd[0]);
        close(pipe_into_cmd[1]);
        close(pipe_outof_cmd[0]);
        close(pipe_outof_cmd[1]);
        return 1;
      } else if (fcntl(pipe_outof_cmd[0], F_SETFL, O_NONBLOCK) != 0) {
        // Unable to set non-blocking on outof-read-pipe.
        close(pipe_into_cmd[0]);
        close(pipe_into_cmd[1]);
        close(pipe_outof_cmd[0]);
        close(pipe_outof_cmd[1]);
        return 1;
      } else if (simple_archiver_de_compress(pipe_into_cmd, pipe_outof_cmd,
                                             state->parsed->compressor,
                                             &compressor_pid) != 0) {
        // Failed to spawn compressor.
        close(pipe_into_cmd[1]);
        close(pipe_outof_cmd[0]);
        fprintf(stderr,
                "WARNING: Failed to start compressor cmd! Invalid cmd?\n");
        return 1;
      }

      // Close unnecessary pipe fds on this end of the transfer.
      close(pipe_into_cmd[0]);
      close(pipe_outof_cmd[1]);

      int compressor_status;
      int compressor_return_val;
      int compressor_ret = waitpid(compressor_pid, &compressor_status, WNOHANG);
      if (compressor_ret == compressor_pid) {
        // Status is available.
        if (WIFEXITED(compressor_status)) {
          compressor_return_val = WEXITSTATUS(compressor_status);
          fprintf(stderr,
                  "WARNING: Exec failed (exec exit code %d)! Invalid "
                  "compressor cmd?\n",
                  compressor_return_val);
          return 1;
        }
      } else if (compressor_ret == 0) {
        // Probably still running, continue on.
      } else {
        // Error.
        fprintf(stderr,
                "WARNING: Exec failed (exec exit code unknown)! Invalid "
                "compressor cmd?\n");
        return 1;
      }

      // Write file to pipe, and read from other pipe.
      char write_buf[SIMPLE_ARCHIVER_BUFFER_SIZE];
      char read_buf[SIMPLE_ARCHIVER_BUFFER_SIZE];
      int_fast8_t write_again = 0;
      int_fast8_t write_done = 0;
      int_fast8_t read_done = 0;
      size_t write_count;
      size_t read_count;
      ssize_t ret;
      while (!write_done || !read_done) {
        if (is_sig_int_occurred) {
          if (pipe_into_cmd[1] >= 0) {
            close(pipe_into_cmd[1]);
            pipe_into_cmd[1] = -1;
          }
          if (pipe_outof_cmd[0] >= 0) {
            close(pipe_outof_cmd[0]);
            pipe_outof_cmd[0] = -1;
          }
          return 1;
        }
        if (is_sig_pipe_occurred) {
          fprintf(stderr,
                  "WARNING: Failed to write to compressor (SIGPIPE)! Invalid "
                  "compressor cmd?\n");
          return 1;
        }

        // Read from file.
        if (!write_done) {
          if (!write_again) {
            write_count =
                fread(write_buf, 1, SIMPLE_ARCHIVER_BUFFER_SIZE, file_fd);
          }
          if (write_count > 0) {
            ret = write(pipe_into_cmd[1], write_buf, write_count);
            if (ret == -1) {
              if (errno == EAGAIN || errno == EWOULDBLOCK) {
                nanosleep(&nonblock_sleep, NULL);
                write_again = 1;
              } else {
                // Error during write.
                fprintf(stderr,
                        "WARNING: Failed to write to compressor! Invalid "
                        "compressor cmd?\n");
                return 1;
              }
            } else if ((size_t)ret != write_count) {
              // Error during write.
              fprintf(stderr,
                      "WARNING: Failed to write to compressor! Invalid "
                      "compressor cmd?\n");
              return 1;
            } else {
              write_again = 0;
              // fprintf(stderr, "Written %zd bytes to comp.\n", ret);
            }
          } else {
            if (feof(file_fd)) {
              simple_archiver_helper_cleanup_FILE(&file_fd);
              write_done = 1;
              close(pipe_into_cmd[1]);
              pipe_into_cmd[1] = -1;
              // fprintf(stderr, "write_done\n");
            } else if (ferror(file_fd)) {
              // Error during read file.
              fprintf(
                  stderr,
                  "WARNING: Failed to write to compressor (failed to read)!\n");
              return 1;
            }
          }
        }

        // Read from compressor.
        if (!read_done) {
          ret = read(pipe_outof_cmd[0], read_buf, SIMPLE_ARCHIVER_BUFFER_SIZE);
          if (ret > 0) {
            read_count = fwrite(read_buf, 1, (size_t)ret, tmp_fd);
            if (read_count != (size_t)ret) {
              // Write to tmp_fd error.
              fprintf(stderr,
                      "WARNING: Failed to read from compressor! Invalid "
                      "compressor cmd?\n");
              return 1;
            } else {
              // fprintf(stderr, "Written %zd bytes to tmp_fd.\n", read_count);
            }
          } else if (ret == 0) {
            read_done = 1;
            simple_archiver_helper_cleanup_FILE(&tmp_fd);
            close(pipe_outof_cmd[0]);
            pipe_outof_cmd[0] = -1;
            // fprintf(stderr, "read_done\n");
          } else if (ret == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
              nanosleep(&nonblock_sleep, NULL);
            } else {
              // Read error.
              fprintf(stderr,
                      "WARNING: Failed to read from compressor! Invalid "
                      "compressor cmd?\n");
              return 1;
            }
          } else {
            // Nop. Technically this branch should be unreachable.
          }
        }
      }

      if (is_sig_pipe_occurred) {
        fprintf(stderr,
                "WARNING: Failed to write to compressor (SIGPIPE)! Invalid "
                "compressor cmd?\n");
        return 1;
      } else if (is_sig_int_occurred) {
        return 1;
      }

      waitpid(compressor_pid, NULL, 0);

      uint16_t u16;
      uint64_t u64;

      size_t temp_size = strlen(file_info->filename);
      if (state->parsed->prefix) {
        temp_size += strlen(state->parsed->prefix);
      }
      if (temp_size > 0xFFFF) {
        fprintf(stderr, "ERROR: Filename size is too large to store!\n");
        return 1;
      }
      u16 = (uint16_t)temp_size;

      // Write filename length.
      simple_archiver_helper_16_bit_be(&u16);
      temp_to_write = malloc(sizeof(SDArchiverInternalToWrite));
      temp_to_write->buf = malloc(2);
      temp_to_write->size = 2;
      memcpy(temp_to_write->buf, &u16, 2);
      simple_archiver_list_add(to_write, temp_to_write, free_internal_to_write);

      // Write filename.
      simple_archiver_helper_16_bit_be(&u16);
      temp_to_write = malloc(sizeof(SDArchiverInternalToWrite));
      temp_to_write->buf = malloc(u16 + 1);
      temp_to_write->size = u16 + 1;
      if (state->parsed->prefix) {
        const size_t prefix_length = strlen(state->parsed->prefix);
        const size_t filename_length = strlen(file_info->filename);
        memcpy(temp_to_write->buf, state->parsed->prefix, prefix_length);
        memcpy((uint8_t*)temp_to_write->buf + prefix_length,
               file_info->filename,
               filename_length + 1);
      } else {
        memcpy(temp_to_write->buf, file_info->filename, u16 + 1);
      }
      simple_archiver_list_add(to_write, temp_to_write, free_internal_to_write);

      // Write flags.
      temp_to_write = malloc(sizeof(SDArchiverInternalToWrite));
      temp_to_write->buf = malloc(4);
      temp_to_write->size = 4;
      for (size_t idx = 0; idx < temp_to_write->size; ++idx) {
        ((uint8_t *)temp_to_write->buf)[idx] = 0;
      }

      // Get file stats.
      struct stat stat_buf;
      memset(&stat_buf, 0, sizeof(struct stat));
      int stat_fd = open(file_info->filename, O_RDONLY);
      if (stat_fd == -1) {
        // Error.
        return 1;
      }
      int stat_status = fstat(stat_fd, &stat_buf);
      close(stat_fd);
      if (stat_status != 0) {
        // Error.
        return 1;
      }

      if (state->parsed->flags & 0x1000) {
        ((uint8_t *)temp_to_write->buf)[0] |=
          (state->parsed->file_permissions & 0x7F) << 1;
        ((uint8_t *)temp_to_write->buf)[1] |=
          (state->parsed->file_permissions & 0x18) >> 7;
      } else {
        if ((stat_buf.st_mode & S_IRUSR) != 0) {
          ((uint8_t *)temp_to_write->buf)[0] |= 0x2;
        }
        if ((stat_buf.st_mode & S_IWUSR) != 0) {
          ((uint8_t *)temp_to_write->buf)[0] |= 0x4;
        }
        if ((stat_buf.st_mode & S_IXUSR) != 0) {
          ((uint8_t *)temp_to_write->buf)[0] |= 0x8;
        }
        if ((stat_buf.st_mode & S_IRGRP) != 0) {
          ((uint8_t *)temp_to_write->buf)[0] |= 0x10;
        }
        if ((stat_buf.st_mode & S_IWGRP) != 0) {
          ((uint8_t *)temp_to_write->buf)[0] |= 0x20;
        }
        if ((stat_buf.st_mode & S_IXGRP) != 0) {
          ((uint8_t *)temp_to_write->buf)[0] |= 0x40;
        }
        if ((stat_buf.st_mode & S_IROTH) != 0) {
          ((uint8_t *)temp_to_write->buf)[0] |= 0x80;
        }
        if ((stat_buf.st_mode & S_IWOTH) != 0) {
          ((uint8_t *)temp_to_write->buf)[1] |= 0x1;
        }
        if ((stat_buf.st_mode & S_IXOTH) != 0) {
          ((uint8_t *)temp_to_write->buf)[1] |= 0x2;
        }
      }

      simple_archiver_list_add(to_write, temp_to_write, free_internal_to_write);

      // Get compressed file length.
      // Compressed file should be at "temp_filename".
      tmp_fd = fopen(temp_filename, "rb");

      long end;
      if (fseek(tmp_fd, 0, SEEK_END) != 0) {
        // Error seeking.
        return 1;
      }
      end = ftell(tmp_fd);
      if (end == -1L) {
        // Error getting end position.
        return 1;
      } else if (fseek(tmp_fd, 0, SEEK_SET) != 0) {
        // Error seeking.
        return 1;
      }

      // Write file length.
      u64 = (uint64_t)end;
      simple_archiver_helper_64_bit_be(&u64);
      temp_to_write = malloc(sizeof(SDArchiverInternalToWrite));
      temp_to_write->buf = malloc(8);
      temp_to_write->size = 8;
      memcpy(temp_to_write->buf, &u64, 8);
      simple_archiver_list_add(to_write, temp_to_write, free_internal_to_write);

      // Write all previuosly set data before writing file.
      simple_archiver_list_get(to_write, write_list_datas_fn, state->out_f);

      simple_archiver_list_free(&to_write);

      // Write file.
      fprintf(stderr, "Writing compressed file: %s\n", file_info->filename);
      do {
        write_count = fread(write_buf, 1, SIMPLE_ARCHIVER_BUFFER_SIZE, tmp_fd);
        if (write_count == SIMPLE_ARCHIVER_BUFFER_SIZE) {
          fwrite(write_buf, 1, SIMPLE_ARCHIVER_BUFFER_SIZE, state->out_f);
        } else if (write_count > 0) {
          fwrite(write_buf, 1, write_count, state->out_f);
        }
        if (feof(tmp_fd)) {
          break;
        } else if (ferror(tmp_fd)) {
          // Error.
          break;
        }
      } while (1);

      // Cleanup.
      simple_archiver_helper_cleanup_FILE(&tmp_fd);
#endif
    } else {
      uint16_t u16;
      uint64_t u64;

      size_t temp_size = strlen(file_info->filename);
      if (state->parsed->prefix) {
        temp_size += strlen(state->parsed->prefix);
      }
      if (temp_size > 0xFFFF) {
        fprintf(stderr, "ERROR: Filename is too large to store!\n");
        return 1;
      }
      u16 = (uint16_t)temp_size;

      // Write filename length.
      simple_archiver_helper_16_bit_be(&u16);
      temp_to_write = malloc(sizeof(SDArchiverInternalToWrite));
      temp_to_write->buf = malloc(2);
      temp_to_write->size = 2;
      memcpy(temp_to_write->buf, &u16, 2);
      simple_archiver_list_add(to_write, temp_to_write, free_internal_to_write);

      // Write filename.
      simple_archiver_helper_16_bit_be(&u16);
      temp_to_write = malloc(sizeof(SDArchiverInternalToWrite));
      temp_to_write->buf = malloc(u16 + 1);
      temp_to_write->size = u16 + 1;
      if (state->parsed->prefix) {
        const size_t prefix_length = strlen(state->parsed->prefix);
        const size_t filename_length = strlen(file_info->filename);
        memcpy(temp_to_write->buf, state->parsed->prefix, prefix_length);
        memcpy((uint8_t*)temp_to_write->buf + prefix_length,
               file_info->filename,
               filename_length + 1);
      } else {
        memcpy(temp_to_write->buf, file_info->filename, u16 + 1);
      }
      simple_archiver_list_add(to_write, temp_to_write, free_internal_to_write);

      // Write flags.
      temp_to_write = malloc(sizeof(SDArchiverInternalToWrite));
      temp_to_write->buf = malloc(4);
      temp_to_write->size = 4;
      for (size_t idx = 0; idx < temp_to_write->size; ++idx) {
        ((uint8_t *)temp_to_write->buf)[idx] = 0;
      }

#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
      // Get file stats.
      struct stat stat_buf;
      memset(&stat_buf, 0, sizeof(struct stat));
      int stat_fd = open(file_info->filename, O_RDONLY);
      if (stat_fd == -1) {
        // Error.
        fprintf(stderr, "ERROR: Failed to get stat of \"%s\"!\n",
                file_info->filename);
        return 1;
      }
      int stat_status = fstat(stat_fd, &stat_buf);
      close(stat_fd);
      if (stat_status != 0) {
        // Error.
        return 1;
      }

      if ((stat_buf.st_mode & S_IRUSR) != 0) {
        ((uint8_t *)temp_to_write->buf)[0] |= 0x2;
      }
      if ((stat_buf.st_mode & S_IWUSR) != 0) {
        ((uint8_t *)temp_to_write->buf)[0] |= 0x4;
      }
      if ((stat_buf.st_mode & S_IXUSR) != 0) {
        ((uint8_t *)temp_to_write->buf)[0] |= 0x8;
      }
      if ((stat_buf.st_mode & S_IRGRP) != 0) {
        ((uint8_t *)temp_to_write->buf)[0] |= 0x10;
      }
      if ((stat_buf.st_mode & S_IWGRP) != 0) {
        ((uint8_t *)temp_to_write->buf)[0] |= 0x20;
      }
      if ((stat_buf.st_mode & S_IXGRP) != 0) {
        ((uint8_t *)temp_to_write->buf)[0] |= 0x40;
      }
      if ((stat_buf.st_mode & S_IROTH) != 0) {
        ((uint8_t *)temp_to_write->buf)[0] |= 0x80;
      }
      if ((stat_buf.st_mode & S_IWOTH) != 0) {
        ((uint8_t *)temp_to_write->buf)[1] |= 0x1;
      }
      if ((stat_buf.st_mode & S_IXOTH) != 0) {
        ((uint8_t *)temp_to_write->buf)[1] |= 0x2;
      }
#else
      // Unsupported platform. Just set the permission bits for user.
      ((uint8_t *)temp_to_write->buf)[0] |= 0xE;
#endif

      if (state->parsed->flags & 0x1000) {
        ((uint8_t *)temp_to_write->buf)[0] =
          (uint8_t)((state->parsed->file_permissions & 0x7F) << 1);

        ((uint8_t *)temp_to_write->buf)[1] &= 0xC;
        ((uint8_t *)temp_to_write->buf)[1] |=
          (uint8_t)((state->parsed->file_permissions & 0x18) >> 7);
      }

      simple_archiver_list_add(to_write, temp_to_write, free_internal_to_write);

      // Write file length.
      __attribute__((cleanup(simple_archiver_helper_cleanup_FILE))) FILE *fd =
          fopen(file_info->filename, "rb");
      if (!fd) {
        // Error.
        return 1;
      } else if (fseek(fd, 0, SEEK_END) != 0) {
        // Error.
        return 1;
      }
      long end = ftell(fd);
      if (end == -1L) {
        // Error.
        return 1;
      }
      u64 = (uint64_t)end;
      simple_archiver_helper_64_bit_be(&u64);
      temp_to_write = malloc(sizeof(SDArchiverInternalToWrite));
      temp_to_write->buf = malloc(8);
      temp_to_write->size = 8;
      memcpy(temp_to_write->buf, &u64, 8);
      simple_archiver_list_add(to_write, temp_to_write, free_internal_to_write);

      if (fseek(fd, 0, SEEK_SET) != 0) {
        // Error.
        return 1;
      }

      // Write all previuosly set data before writing file.
      simple_archiver_list_get(to_write, write_list_datas_fn, state->out_f);

      simple_archiver_list_free(&to_write);

      if (is_sig_int_occurred) {
        return 1;
      }

      // Write file.
      fprintf(stderr, "Writing file: %s\n", file_info->filename);
      char buf[SIMPLE_ARCHIVER_BUFFER_SIZE];
      size_t ret;
      do {
        ret = fread(buf, 1, SIMPLE_ARCHIVER_BUFFER_SIZE, fd);
        if (ret == SIMPLE_ARCHIVER_BUFFER_SIZE) {
          fwrite(buf, 1, SIMPLE_ARCHIVER_BUFFER_SIZE, state->out_f);
        } else if (ret > 0) {
          fwrite(buf, 1, ret, state->out_f);
        }
        if (feof(fd)) {
          break;
        } else if (ferror(fd)) {
          // Error.
          break;
        }
      } while (1);
    }
  } else {
    // A symblic link.
    uint16_t u16;

    size_t temp_size = strlen(file_info->filename);
    if (state->parsed->prefix) {
      temp_size += strlen(state->parsed->prefix);
    }
    if (temp_size > 0xFFFF) {
      fprintf(stderr, "ERROR: Filename is too large to store!\n");
      return 1;
    }
    u16 = (uint16_t)temp_size;

    // Write filename length.
    simple_archiver_helper_16_bit_be(&u16);
    temp_to_write = malloc(sizeof(SDArchiverInternalToWrite));
    temp_to_write->buf = malloc(2);
    temp_to_write->size = 2;
    memcpy(temp_to_write->buf, &u16, 2);
    simple_archiver_list_add(to_write, temp_to_write, free_internal_to_write);

    // Write filename.
    simple_archiver_helper_16_bit_be(&u16);
    temp_to_write = malloc(sizeof(SDArchiverInternalToWrite));
    temp_to_write->buf = malloc(u16 + 1);
    temp_to_write->size = u16 + 1;
    if (state->parsed->prefix) {
      const size_t prefix_length = strlen(state->parsed->prefix);
      const size_t filename_length = strlen(file_info->filename);
      memcpy(temp_to_write->buf, state->parsed->prefix, prefix_length);
      memcpy((uint8_t*)temp_to_write->buf + prefix_length,
             file_info->filename,
             filename_length + 1);
    } else {
      memcpy(temp_to_write->buf, file_info->filename, u16 + 1);
    }
    simple_archiver_list_add(to_write, temp_to_write, free_internal_to_write);

    // Write flags.
    temp_to_write = malloc(sizeof(SDArchiverInternalToWrite));
    temp_to_write->buf = malloc(4);
    temp_to_write->size = 4;
    for (size_t idx = 0; idx < temp_to_write->size; ++idx) {
      ((uint8_t *)temp_to_write->buf)[idx] = 0;
    }

    // Set "is symbolic link" flag.
    ((uint8_t *)temp_to_write->buf)[0] = 1;

#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
    // Get file stats.
    struct stat stat_buf;
    memset(&stat_buf, 0, sizeof(struct stat));
    int stat_status =
        fstatat(AT_FDCWD, file_info->filename, &stat_buf, AT_SYMLINK_NOFOLLOW);
    if (stat_status != 0) {
      // Error.
      return 1;
    }

    if ((stat_buf.st_mode & S_IRUSR) != 0) {
      ((uint8_t *)temp_to_write->buf)[0] |= 0x2;
    }
    if ((stat_buf.st_mode & S_IWUSR) != 0) {
      ((uint8_t *)temp_to_write->buf)[0] |= 0x4;
    }
    if ((stat_buf.st_mode & S_IXUSR) != 0) {
      ((uint8_t *)temp_to_write->buf)[0] |= 0x8;
    }
    if ((stat_buf.st_mode & S_IRGRP) != 0) {
      ((uint8_t *)temp_to_write->buf)[0] |= 0x10;
    }
    if ((stat_buf.st_mode & S_IWGRP) != 0) {
      ((uint8_t *)temp_to_write->buf)[0] |= 0x20;
    }
    if ((stat_buf.st_mode & S_IXGRP) != 0) {
      ((uint8_t *)temp_to_write->buf)[0] |= 0x40;
    }
    if ((stat_buf.st_mode & S_IROTH) != 0) {
      ((uint8_t *)temp_to_write->buf)[0] |= 0x80;
    }
    if ((stat_buf.st_mode & S_IWOTH) != 0) {
      ((uint8_t *)temp_to_write->buf)[1] |= 0x1;
    }
    if ((stat_buf.st_mode & S_IXOTH) != 0) {
      ((uint8_t *)temp_to_write->buf)[1] |= 0x2;
    }
#else
    // Unsupported platform. Just set the permission bits for user.
    ((uint8_t *)temp_to_write->buf)[0] |= 0xE;
#endif

    // Need to get abs_path for checking/setting a flag before storing flags.
    // Get absolute path.
    __attribute__((cleanup(
        simple_archiver_helper_cleanup_malloced))) void *abs_path = NULL;
    __attribute__((cleanup(
        simple_archiver_helper_cleanup_malloced))) void *rel_path = NULL;

    if ((state->parsed->flags & 0x100) != 0) {
      // Preserve symlink target.
      char *path_buf = malloc(1024);
      ssize_t ret = readlink(file_info->filename, path_buf, 1023);
      if (ret == -1) {
        fprintf(stderr, "WARNING: Failed to get symlink's target!\n");
        free(path_buf);
        ((uint8_t *)temp_to_write->buf)[1] |= 0x8;
      } else {
        path_buf[ret] = 0;
        if (path_buf[0] == '/') {
          abs_path = path_buf;
          ((uint8_t *)temp_to_write->buf)[1] |= 0x4;
        } else {
          rel_path = path_buf;
        }
      }
    } else {
      abs_path = realpath(file_info->filename, NULL);
      if (abs_path) {
        // Get relative path.
        // First get absolute path of link.
        __attribute__((cleanup(
            simple_archiver_helper_cleanup_malloced))) void *link_abs_path =
            simple_archiver_helper_real_path_to_name(file_info->filename);
        if (!link_abs_path) {
          fprintf(stderr, "WARNING: Failed to get absolute path of link!\n");
        } else {
          // fprintf(stderr, "DEBUG: abs_path: %s\nDEBUG: link_abs_path: %s\n",
          //                 (char*)abs_path, (char*)link_abs_path);

          rel_path = simple_archiver_filenames_to_relative_path(link_abs_path,
                                                                abs_path);
        }
      }
    }

    // Check if absolute path refers to one of the filenames.
    if (abs_path && (state->parsed->flags & 0x20) == 0 &&
        (state->parsed->flags & 0x100) == 0 &&
        !simple_archiver_hash_map_get(state->map, abs_path,
                                      strlen(abs_path) + 1)) {
      // Is not a filename being archived.
      ((uint8_t *)temp_to_write->buf)[1] |= 0x10;
      if ((state->parsed->flags & 0x80) != 0) {
        // No safe links, set preference to absolute path.
        fprintf(
            stderr,
            "NOTICE: abs_path exists, \"--no-abs-symlink\" not specified, "
            "and link refers to file NOT in archive; preferring abs_path.\n");
        ((uint8_t *)temp_to_write->buf)[1] |= 0x4;
      } else {
        // Safe links, do not store symlink!
        fprintf(stderr,
                "WARNING: Symlink \"%s\" points to outside archive contents, "
                "will not be stored! (Use \"--no-safe-links\" to disable this "
                "behavior)\n",
                file_info->filename);
        ((uint8_t *)temp_to_write->buf)[1] |= 0x8;
      }
    } else if ((state->parsed->flags & 0x100) != 0 &&
               (state->parsed->flags & 0x80) == 0 &&
               (((uint8_t *)temp_to_write->buf)[1] & 0x8) == 0) {
      __attribute__((cleanup(
          simple_archiver_helper_cleanup_c_string))) char *resolved_path = NULL;
      if (abs_path || rel_path) {
        resolved_path = realpath(file_info->filename, NULL);
        if (!resolved_path) {
          fprintf(stderr,
                  "WARNING: Symlink \"%s\" is invalid, will not be stored!  "
                  "(Use \"--no-safe-links\" to disable this behavior)\n",
                  file_info->filename);
          ((uint8_t *)temp_to_write->buf)[1] |= 0x8;
        } else if (!simple_archiver_hash_map_get(state->map, resolved_path,
                                                 strlen(resolved_path) + 1)) {
          fprintf(stderr,
                  "WARNING: Symlink \"%s\" points to outside archive contents, "
                  "will not be stored! (Use \"--no-safe-links\" to disable "
                  "this behavior)\n",
                  file_info->filename);
          ((uint8_t *)temp_to_write->buf)[1] |= 0x18;
        }
      } else {
        fprintf(stderr,
                "WARNING: Unable to get target path from symlink \"%s\"!\n",
                file_info->filename);
        ((uint8_t *)temp_to_write->buf)[1] |= 0x8;
      }
    }

    if (!abs_path && !rel_path) {
      // No valid paths, set as invalid.
      fprintf(stderr,
              "WARNING: Could not get valid abs/rel path for symlink \"%s\" "
              "(invalid symlink)!\n",
              file_info->filename);
      ((uint8_t *)temp_to_write->buf)[1] |= 0x8;
    }

    // Store the 4 byte bit-flags for file.
    simple_archiver_list_add(to_write, temp_to_write, free_internal_to_write);

    if ((((uint8_t *)temp_to_write->buf)[1] & 0x8) != 0) {
      // Skipped symlink.
      simple_archiver_list_get(to_write, write_list_datas_fn, state->out_f);
      simple_archiver_list_free(&to_write);
      char format_str[64];
      snprintf(format_str, 64, FILE_COUNTS_OUTPUT_FORMAT_STR_1, state->digits,
               state->digits);
      fprintf(stderr, format_str, ++(state->count), state->max);
      return 0;
    }

    // Store the absolute and relative paths.
    if (!abs_path) {
      if ((state->parsed->flags & 0x100) == 0) {
        fprintf(stderr,
                "WARNING: Failed to get absolute path of link destination!\n");
      }
      temp_to_write = malloc(sizeof(SDArchiverInternalToWrite));
      temp_to_write->buf = malloc(2);
      temp_to_write->size = 2;
      memset(temp_to_write->buf, 0, 2);
      simple_archiver_list_add(to_write, temp_to_write, free_internal_to_write);
    } else if ((state->parsed->flags & 0x20) == 0) {
      if (state->parsed->prefix) {
        char *abs_path_cached = abs_path;
        abs_path = simple_archiver_helper_insert_prefix_in_link_path(
          state->parsed->prefix, file_info->filename, abs_path_cached);
        free(abs_path_cached);
      }
      // Write absolute path length.
      size_t temp_size = strlen(abs_path);
      if (temp_size > 0xFFFF) {
        fprintf(stderr, "ERROR: Absolute path name is too large!\n");
        return 1;
      }
      u16 = (uint16_t)temp_size;
      simple_archiver_helper_16_bit_be(&u16);

      temp_to_write = malloc(sizeof(SDArchiverInternalToWrite));
      temp_to_write->buf = malloc(2);
      temp_to_write->size = 2;
      memcpy(temp_to_write->buf, &u16, 2);
      simple_archiver_list_add(to_write, temp_to_write, free_internal_to_write);

      // Write absolute path.
      simple_archiver_helper_16_bit_be(&u16);
      temp_to_write = malloc(sizeof(SDArchiverInternalToWrite));
      temp_to_write->buf = malloc(u16 + 1);
      temp_to_write->size = u16 + 1;
      strncpy(temp_to_write->buf, abs_path, u16 + 1);
      simple_archiver_list_add(to_write, temp_to_write, free_internal_to_write);
    } else {
      fprintf(stderr,
              "NOTICE: Not saving absolute path since \"--no-abs-symlink\" "
              "was specified.\n");
      temp_to_write = malloc(sizeof(SDArchiverInternalToWrite));
      temp_to_write->buf = malloc(2);
      temp_to_write->size = 2;
      memset(temp_to_write->buf, 0, 2);
      simple_archiver_list_add(to_write, temp_to_write, free_internal_to_write);
    }

    if (rel_path) {
      if (state->parsed->prefix) {
        char *rel_path_cached = rel_path;
        rel_path = simple_archiver_helper_insert_prefix_in_link_path(
          state->parsed->prefix, file_info->filename, rel_path_cached);
        free(rel_path_cached);
      }
      // Write relative path length.
      size_t temp_size = strlen(rel_path);
      if (temp_size > 0xFFFF) {
        fprintf(stderr, "ERROR: Relative path name is too large!\n");
        return 1;
      }
      u16 = (uint16_t)temp_size;
      simple_archiver_helper_16_bit_be(&u16);
      temp_to_write = malloc(sizeof(SDArchiverInternalToWrite));
      temp_to_write->buf = malloc(2);
      temp_to_write->size = 2;
      memcpy(temp_to_write->buf, &u16, 2);
      simple_archiver_list_add(to_write, temp_to_write, free_internal_to_write);

      // Write relative path.
      simple_archiver_helper_16_bit_be(&u16);
      temp_to_write = malloc(sizeof(SDArchiverInternalToWrite));
      temp_to_write->buf = malloc(u16 + 1);
      temp_to_write->size = u16 + 1;
      strncpy(temp_to_write->buf, rel_path, u16 + 1);
      simple_archiver_list_add(to_write, temp_to_write, free_internal_to_write);
    } else {
      temp_to_write = malloc(sizeof(SDArchiverInternalToWrite));
      temp_to_write->buf = malloc(2);
      temp_to_write->size = 2;
      memset(temp_to_write->buf, 0, 2);
      simple_archiver_list_add(to_write, temp_to_write, free_internal_to_write);
    }

    // Write all previously set data.
    fprintf(stderr, "Writing symlink info: %s\n", file_info->filename);
    if ((state->parsed->flags & 0x20) == 0) {
      if (abs_path) {
        fprintf(stderr, "  abs path: %s\n", (char *)abs_path);
      } else {
        fprintf(stderr, "  abs path is NOT set\n");
      }
    }
    if (rel_path) {
      fprintf(stderr, "  rel path: %s\n", (char *)rel_path);
    } else {
      fprintf(stderr, "  rel path is NOT set\n");
    }
    simple_archiver_list_get(to_write, write_list_datas_fn, state->out_f);
    simple_archiver_list_free(&to_write);
  }

  char format_str[64];
  snprintf(format_str, 64, FILE_COUNTS_OUTPUT_FORMAT_STR_1, state->digits,
           state->digits);
  fprintf(stderr, format_str, ++(state->count), state->max);

  if (is_sig_int_occurred) {
    return 1;
  }
  return 0;
}

int filenames_to_abs_map_fn(void *data, void *ud) {
  SDArchiverFileInfo *file_info = data;
  void **ptr_array = ud;
  SDArchiverHashMap *abs_filenames = ptr_array[0];
  const char *user_cwd = ptr_array[1];
  __attribute__((cleanup(
      simple_archiver_helper_cleanup_chdir_back))) char *original_cwd = NULL;
  if (user_cwd) {
#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
    original_cwd = realpath(".", NULL);
    if (chdir(user_cwd)) {
      return 1;
    }
#endif
  }

  // Get combined full path to file.
  char *fullpath =
    simple_archiver_helper_real_path_to_name(file_info->filename);
  if (!fullpath) {
    return 1;
  }

  simple_archiver_hash_map_insert(
      abs_filenames, fullpath, fullpath, strlen(fullpath) + 1,
      simple_archiver_helper_datastructure_cleanup_nop, NULL);

  // Try putting all parent dirs up to current working directory.
  // First get absolute path to current working directory.
  __attribute__((cleanup(
      simple_archiver_helper_cleanup_malloced))) void *cwd_dirname = NULL;
#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
  cwd_dirname = realpath(".", NULL);
#endif
  if (!cwd_dirname) {
    return 1;
  }
  // fprintf(stderr, "cwd_dirname: %s\n", (char*)cwd_dirname);

  // Use copy of fullpath to avoid clobbering it.
  __attribute__((
      cleanup(simple_archiver_helper_cleanup_malloced))) void *fullpath_copy =
      malloc(strlen(fullpath) + 1);
  strncpy(fullpath_copy, fullpath, strlen(fullpath) + 1);

  // Get dirnames.
  char *prev = fullpath_copy;
  char *fullpath_dirname;
  while (1) {
    fullpath_dirname = dirname(prev);
    if (!fullpath_dirname || strlen(fullpath_dirname) <= strlen(cwd_dirname)) {
      break;
    } else {
      // Make and store copy of fullpath_dirname.
      char *fullpath_dirname_copy = malloc(strlen(fullpath_dirname) + 1);
      strncpy(fullpath_dirname_copy, fullpath_dirname,
              strlen(fullpath_dirname) + 1);
      if (!simple_archiver_hash_map_get(abs_filenames, fullpath_dirname_copy,
                                        strlen(fullpath_dirname_copy) + 1)) {
        simple_archiver_hash_map_insert(
            abs_filenames, fullpath_dirname_copy, fullpath_dirname_copy,
            strlen(fullpath_dirname_copy) + 1,
            simple_archiver_helper_datastructure_cleanup_nop, NULL);
      } else {
        free(fullpath_dirname_copy);
      }
    }
    prev = fullpath_dirname;
  }

  return 0;
}

int read_buf_full_from_fd(FILE *fd, char *read_buf, const size_t read_buf_size,
                          const size_t amount_total, char *dst_buf) {
  size_t amount = amount_total;
  while (amount != 0) {
    if (amount >= read_buf_size) {
      if (fread(read_buf, 1, read_buf_size, fd) != read_buf_size) {
        return SDAS_INVALID_FILE;
      }
      if (dst_buf) {
        memcpy(dst_buf + (amount_total - amount), read_buf, read_buf_size);
      }
      amount -= read_buf_size;
    } else {
      if (fread(read_buf, 1, amount, fd) != amount) {
        return SDAS_INVALID_FILE;
      }
      if (dst_buf) {
        memcpy(dst_buf + (amount_total - amount), read_buf, amount);
      }
      amount = 0;
    }
  }

  return SDAS_SUCCESS;
}

int read_fd_to_out_fd(FILE *in_fd, FILE *out_fd, char *read_buf,
                      const size_t read_buf_size, const size_t amount_total) {
  size_t amount = amount_total;
  while (amount != 0) {
    if (amount >= read_buf_size) {
      if (fread(read_buf, 1, read_buf_size, in_fd) != read_buf_size) {
        return SDAS_INVALID_FILE;
      } else if (fwrite(read_buf, 1, read_buf_size, out_fd) != read_buf_size) {
        return SDAS_FAILED_TO_WRITE;
      }
      amount -= read_buf_size;
    } else {
      if (fread(read_buf, 1, amount, in_fd) != amount) {
        return SDAS_INVALID_FILE;
      } else if (fwrite(read_buf, 1, amount, out_fd) != amount) {
        return SDAS_FAILED_TO_WRITE;
      }
      amount = 0;
    }
  }
  return SDAS_SUCCESS;
}

int try_write_to_decomp(int *to_dec_pipe, uint64_t *chunk_remaining, FILE *in_f,
                        char *buf, const size_t buf_size, char *hold_buf,
                        ssize_t *has_hold) {
  if (*to_dec_pipe >= 0) {
    if (*chunk_remaining > 0) {
      if (*chunk_remaining > buf_size) {
        if (*has_hold < 0) {
          size_t fread_ret = fread(buf, 1, SIMPLE_ARCHIVER_BUFFER_SIZE, in_f);
          if (fread_ret == 0) {
            goto TRY_WRITE_TO_DECOMP_END;
          } else {
            ssize_t write_ret = write(*to_dec_pipe, buf, fread_ret);
            if (write_ret < 0) {
              if (errno == EAGAIN || errno == EWOULDBLOCK) {
                *has_hold = (ssize_t)fread_ret;
                memcpy(hold_buf, buf, fread_ret);
                return SDAS_SUCCESS;
              } else {
                return SDAS_INTERNAL_ERROR;
              }
            } else if (write_ret == 0) {
              return SDAS_INTERNAL_ERROR;
            } else if ((size_t)write_ret < fread_ret) {
              *chunk_remaining -= (size_t)write_ret;
              *has_hold = (ssize_t)fread_ret - write_ret;
              memcpy(hold_buf, buf + write_ret, (size_t)*has_hold);
              return SDAS_SUCCESS;
            } else {
              *chunk_remaining -= (size_t)write_ret;
            }
          }
        } else {
          ssize_t write_ret = write(*to_dec_pipe, hold_buf, (size_t)*has_hold);
          if (write_ret < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
              return SDAS_SUCCESS;
            } else {
              return SDAS_INTERNAL_ERROR;
            }
          } else if (write_ret == 0) {
            return SDAS_INTERNAL_ERROR;
          } else if (write_ret < *has_hold) {
            *chunk_remaining -= (size_t)write_ret;
            memcpy(buf, hold_buf + write_ret, (size_t)(*has_hold - write_ret));
            memcpy(hold_buf, buf, (size_t)(*has_hold - write_ret));
            *has_hold = *has_hold - write_ret;
            return SDAS_SUCCESS;
          } else {
            *chunk_remaining -= (size_t)*has_hold;
            *has_hold = -1;
          }
        }
      } else {
        if (*has_hold < 0) {
          size_t fread_ret = fread(buf, 1, *chunk_remaining, in_f);
          if (fread_ret == 0) {
            goto TRY_WRITE_TO_DECOMP_END;
          } else {
            ssize_t write_ret = write(*to_dec_pipe, buf, fread_ret);
            if (write_ret < 0) {
              if (errno == EAGAIN || errno == EWOULDBLOCK) {
                *has_hold = (int)fread_ret;
                memcpy(hold_buf, buf, fread_ret);
                return SDAS_SUCCESS;
              } else {
                return SDAS_INTERNAL_ERROR;
              }
            } else if (write_ret == 0) {
              return SDAS_INTERNAL_ERROR;
            } else if ((size_t)write_ret < fread_ret) {
              *chunk_remaining -= (size_t)write_ret;
              *has_hold = (ssize_t)fread_ret - write_ret;
              memcpy(hold_buf, buf + write_ret, (size_t)*has_hold);
              return SDAS_SUCCESS;
            } else if ((size_t)write_ret <= *chunk_remaining) {
              *chunk_remaining -= (size_t)write_ret;
            } else {
              return SDAS_INTERNAL_ERROR;
            }
          }
        } else {
          ssize_t write_ret = write(*to_dec_pipe, hold_buf, (size_t)*has_hold);
          if (write_ret < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
              return SDAS_SUCCESS;
            } else {
              return SDAS_INTERNAL_ERROR;
            }
          } else if (write_ret == 0) {
            return SDAS_INTERNAL_ERROR;
          } else if (write_ret < *has_hold) {
            *chunk_remaining -= (size_t)write_ret;
            memcpy(buf, hold_buf + write_ret, (size_t)(*has_hold - write_ret));
            memcpy(hold_buf, buf, (size_t)(*has_hold - write_ret));
            *has_hold = *has_hold - write_ret;
            return SDAS_SUCCESS;
          } else {
            *chunk_remaining -= (size_t)*has_hold;
            *has_hold = -1;
          }
        }
      }
    }
  }

TRY_WRITE_TO_DECOMP_END:
  if (*to_dec_pipe >= 0 && *chunk_remaining == 0 && *has_hold < 0) {
    close(*to_dec_pipe);
    *to_dec_pipe = -1;
  }

  return SDAS_SUCCESS;
}

/// Returns SDAS_SUCCESS on success.
int read_decomp_to_out_file(const char *out_filename, int in_pipe,
                            char *read_buf, const size_t read_buf_size,
                            const uint64_t file_size, int *to_dec_pipe,
                            uint64_t *chunk_remaining, FILE *in_f,
                            char *hold_buf, ssize_t *has_hold) {
  __attribute__((cleanup(simple_archiver_helper_cleanup_FILE))) FILE *out_fd =
      NULL;
  if (out_filename) {
    out_fd = fopen(out_filename, "wb");
    if (!out_fd) {
      fprintf(stderr, "ERROR Failed to open \"%s\" for writing!\n",
              out_filename);
      return SDAS_INTERNAL_ERROR;
    }
  }

  uint64_t written_amt = 0;
  ssize_t read_ret;
  size_t fwrite_ret;
  while (written_amt < file_size) {
    if (is_sig_pipe_occurred) {
      fprintf(stderr, "ERROR: SIGPIPE while decompressing!\n");
      return SDAS_INTERNAL_ERROR;
    }
    int ret = try_write_to_decomp(to_dec_pipe, chunk_remaining, in_f, read_buf,
                                  read_buf_size, hold_buf, has_hold);
    if (ret != SDAS_SUCCESS) {
      return ret;
    } else if (is_sig_pipe_occurred) {
      fprintf(stderr, "ERROR: SIGPIPE while decompressing!\n");
      return SDAS_INTERNAL_ERROR;
    } else if (file_size - written_amt >= read_buf_size) {
      read_ret = read(in_pipe, read_buf, read_buf_size);
      if (read_ret > 0) {
        if (out_fd) {
          fwrite_ret = fwrite(read_buf, 1, (size_t)read_ret, out_fd);
          if (fwrite_ret == (size_t)read_ret) {
            written_amt += fwrite_ret;
          } else if (ferror(out_fd)) {
            fprintf(stderr, "ERROR Failed to write decompressed data!\n");
            return SDAS_INTERNAL_ERROR;
          } else {
            fprintf(
                stderr,
                "ERROR Failed to write decompressed data (invalid state)!\n");
            return SDAS_INTERNAL_ERROR;
          }
        } else {
          written_amt += (size_t)read_ret;
        }
      } else if (read_ret == 0) {
        // EOF.
        if (written_amt < file_size) {
          fprintf(stderr,
                  "ERROR Decompressed EOF while file needs more bytes!\n");
          return SDAS_INTERNAL_ERROR;
        } else {
          break;
        }
      } else {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          // Non-blocking read from pipe.
          nanosleep(&nonblock_sleep, NULL);
          continue;
        } else {
          // Error.
          fprintf(stderr, "ERROR Failed to read from decompressor! (%zu)\n",
                  read_ret);
          return SDAS_INTERNAL_ERROR;
        }
      }
    } else {
      read_ret = read(in_pipe, read_buf, file_size - written_amt);
      if (read_ret > 0) {
        if (out_fd) {
          fwrite_ret = fwrite(read_buf, 1, (size_t)read_ret, out_fd);
          if (fwrite_ret == (size_t)read_ret) {
            written_amt += fwrite_ret;
          } else if (ferror(out_fd)) {
            fprintf(stderr, "ERROR Failed to write decompressed data!\n");
            return SDAS_INTERNAL_ERROR;
          } else {
            fprintf(
                stderr,
                "ERROR Failed to write decompressed data (invalid state)!\n");
            return SDAS_INTERNAL_ERROR;
          }
        } else {
          written_amt += (size_t)read_ret;
        }
      } else if (read_ret == 0) {
        // EOF.
        if (written_amt < file_size) {
          fprintf(stderr,
                  "ERROR Decompressed EOF while file needs more bytes!\n");
          return SDAS_INTERNAL_ERROR;
        } else {
          break;
        }
      } else {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          // Non-blocking read from pipe.
          nanosleep(&nonblock_sleep, NULL);
          continue;
        } else {
          // Error.
          fprintf(stderr, "ERROR Failed to read from decompressor! (%d)\n",
                  errno);
          return SDAS_INTERNAL_ERROR;
        }
      }
    }
  }

  return written_amt == file_size ? SDAS_SUCCESS : SDAS_INTERNAL_ERROR;
}

void free_internal_file_info(void *data) {
  SDArchiverInternalFileInfo *file_info = data;
  if (file_info) {
    if (file_info->filename) {
      free(file_info->filename);
    }
    if (file_info->username) {
      free(file_info->username);
    }
    if (file_info->groupname) {
      free(file_info->groupname);
    }
    free(file_info);
  }
}

void cleanup_internal_file_info(SDArchiverInternalFileInfo **file_info) {
  if (file_info && *file_info) {
    if ((*file_info)->filename) {
      free((*file_info)->filename);
    }
    if ((*file_info)->username) {
      free((*file_info)->username);
    }
    if ((*file_info)->groupname) {
      free((*file_info)->groupname);
    }
    free(*file_info);
    *file_info = NULL;
  }
}

#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
mode_t permissions_from_bits_v1_symlink(const uint8_t flags[2],
                                        uint_fast8_t print) {
  mode_t permissions = 0;

  if ((flags[0] & 2) != 0) {
    permissions |= S_IRUSR;
    if (print) {
      fprintf(stderr, "r");
    }
  } else if (print) {
    fprintf(stderr, "-");
  }
  if ((flags[0] & 4) != 0) {
    permissions |= S_IWUSR;
    if (print) {
      fprintf(stderr, "w");
    }
  } else if (print) {
    fprintf(stderr, "-");
  }
  if ((flags[0] & 8) != 0) {
    permissions |= S_IXUSR;
    if (print) {
      fprintf(stderr, "x");
    }
  } else if (print) {
    fprintf(stderr, "-");
  }
  if ((flags[0] & 0x10) != 0) {
    permissions |= S_IRGRP;
    if (print) {
      fprintf(stderr, "r");
    }
  } else if (print) {
    fprintf(stderr, "-");
  }
  if ((flags[0] & 0x20) != 0) {
    permissions |= S_IWGRP;
    if (print) {
      fprintf(stderr, "w");
    }
  } else if (print) {
    fprintf(stderr, "-");
  }
  if ((flags[0] & 0x40) != 0) {
    permissions |= S_IXGRP;
    if (print) {
      fprintf(stderr, "x");
    }
  } else if (print) {
    fprintf(stderr, "-");
  }
  if ((flags[0] & 0x80) != 0) {
    permissions |= S_IROTH;
    if (print) {
      fprintf(stderr, "r");
    }
  } else if (print) {
    fprintf(stderr, "-");
  }
  if ((flags[1] & 1) != 0) {
    permissions |= S_IWOTH;
    if (print) {
      fprintf(stderr, "w");
    }
  } else if (print) {
    fprintf(stderr, "-");
  }
  if ((flags[1] & 2) != 0) {
    permissions |= S_IXOTH;
    if (print) {
      fprintf(stderr, "x");
    }
  } else if (print) {
    fprintf(stderr, "-");
  }

  return permissions;
}

mode_t permissions_from_bits_version_1(const uint8_t flags[4],
                                       uint_fast8_t print) {
  mode_t permissions = 0;

  if ((flags[0] & 1) != 0) {
    permissions |= S_IRUSR;
    if (print) {
      fprintf(stderr, "r");
    }
  } else if (print) {
    fprintf(stderr, "-");
  }
  if ((flags[0] & 2) != 0) {
    permissions |= S_IWUSR;
    if (print) {
      fprintf(stderr, "w");
    }
  } else if (print) {
    fprintf(stderr, "-");
  }
  if ((flags[0] & 4) != 0) {
    permissions |= S_IXUSR;
    if (print) {
      fprintf(stderr, "x");
    }
  } else if (print) {
    fprintf(stderr, "-");
  }
  if ((flags[0] & 8) != 0) {
    permissions |= S_IRGRP;
    if (print) {
      fprintf(stderr, "r");
    }
  } else if (print) {
    fprintf(stderr, "-");
  }
  if ((flags[0] & 0x10) != 0) {
    permissions |= S_IWGRP;
    if (print) {
      fprintf(stderr, "w");
    }
  } else if (print) {
    fprintf(stderr, "-");
  }
  if ((flags[0] & 0x20) != 0) {
    permissions |= S_IXGRP;
    if (print) {
      fprintf(stderr, "x");
    }
  } else if (print) {
    fprintf(stderr, "-");
  }
  if ((flags[0] & 0x40) != 0) {
    permissions |= S_IROTH;
    if (print) {
      fprintf(stderr, "r");
    }
  } else if (print) {
    fprintf(stderr, "-");
  }
  if ((flags[0] & 0x80) != 0) {
    permissions |= S_IWOTH;
    if (print) {
      fprintf(stderr, "w");
    }
  } else if (print) {
    fprintf(stderr, "-");
  }
  if ((flags[1] & 1) != 0) {
    permissions |= S_IXOTH;
    if (print) {
      fprintf(stderr, "x");
    }
  } else if (print) {
    fprintf(stderr, "-");
  }

  return permissions;
}

void print_permissions(mode_t permissions) {
  if ((permissions & S_IRUSR)) {
    fprintf(stderr, "r");
  } else {
    fprintf(stderr, "-");
  }
  if ((permissions & S_IWUSR)) {
    fprintf(stderr, "w");
  } else {
    fprintf(stderr, "-");
  }
  if ((permissions & S_IXUSR)) {
    fprintf(stderr, "x");
  } else {
    fprintf(stderr, "-");
  }
  if ((permissions & S_IRGRP)) {
    fprintf(stderr, "r");
  } else {
    fprintf(stderr, "-");
  }
  if ((permissions & S_IWGRP)) {
    fprintf(stderr, "w");
  } else {
    fprintf(stderr, "-");
  }
  if ((permissions & S_IXGRP)) {
    fprintf(stderr, "x");
  } else {
    fprintf(stderr, "-");
  }
  if ((permissions & S_IROTH)) {
    fprintf(stderr, "r");
  } else {
    fprintf(stderr, "-");
  }
  if ((permissions & S_IWOTH)) {
    fprintf(stderr, "w");
  } else {
    fprintf(stderr, "-");
  }
  if ((permissions & S_IXOTH)) {
    fprintf(stderr, "x");
  } else {
    fprintf(stderr, "-");
  }
}

#endif

void simple_archiver_internal_cleanup_int_fd(int *fd) {
  if (fd && *fd >= 0) {
    close(*fd);
    *fd = -1;
  }
}

#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
void simple_archiver_internal_cleanup_decomp_pid(pid_t *decomp_pid) {
  if (decomp_pid && *decomp_pid >= 0) {
    int decompressor_status;
    int decompressor_return_val;
    int retries = 0;
    int decompressor_ret;
  CHECK_DECOMPRESSER:
    decompressor_ret = waitpid(*decomp_pid, &decompressor_status, 0);
    if (decompressor_ret == *decomp_pid) {
      // Status is available.
      decompressor_return_val = WIFEXITED(decompressor_status);
      if (decompressor_return_val && WEXITSTATUS(decompressor_status)) {
        fprintf(stderr,
                "WARNING: Exec failed (exec exit code %d)! Invalid "
                "decompressor cmd?\n",
                decompressor_return_val);
      }
    } else if (decompressor_ret == 0) {
      // Probably still running.
      ++retries;
      if (retries > 5) {
        fprintf(stderr, "WARNING Decompressor process not stopped!\n");
        return;
      }
      sleep(5);
      goto CHECK_DECOMPRESSER;
    } else {
      // Error.
      fprintf(stderr,
              "WARNING: Exec failed (exec exit code unknown)! Invalid "
              "decompressor cmd?\n");
    }
    *decomp_pid = -1;
  }
}
#endif

int symlinks_and_files_from_files(void *data, void *ud) {
  SDArchiverFileInfo *file_info = data;
  void **ptr_array = ud;
  SDArchiverLinkedList *symlinks_list = ptr_array[0];
  SDArchiverLinkedList *files_list = ptr_array[1];
  const char *user_cwd = ptr_array[2];
  SDArchiverPHeap *pheap = ptr_array[3];
  SDArchiverLinkedList *dirs_list = ptr_array[4];
  const SDArchiverState *state = ptr_array[5];
  uint64_t *from_files_count = ptr_array[6];

  if (file_info->filename) {
    // Check white/black lists.
    if (!simple_archiver_helper_string_allowed_lists(
        file_info->filename,
        state->parsed->whitelist_contains,
        state->parsed->whitelist_begins,
        state->parsed->whitelist_ends,
        state->parsed->blacklist_contains,
        state->parsed->blacklist_begins,
        state->parsed->blacklist_ends)) {
      return 0;
    }

    if (file_info->link_dest) {
      // Is a symbolic link.
      simple_archiver_list_add(
          symlinks_list, file_info->filename,
          simple_archiver_helper_datastructure_cleanup_nop);
    } else if (dirs_list && (file_info->flags & 1) != 0) {
      // Is a directory.
      simple_archiver_list_add(
          dirs_list, file_info->filename,
          simple_archiver_helper_datastructure_cleanup_nop);
    } else {
      // Is a file.
      SDArchiverInternalFileInfo *file_info_struct =
          malloc(sizeof(SDArchiverInternalFileInfo));
      file_info_struct->filename = strdup(file_info->filename);
      file_info_struct->bit_flags[0] = 0xFF;
      file_info_struct->bit_flags[1] = 1;
      file_info_struct->bit_flags[2] = 0;
      file_info_struct->bit_flags[3] = 0;
      file_info_struct->uid = 0;
      file_info_struct->gid = 0;
      file_info_struct->username = NULL;
      file_info_struct->groupname = NULL;
      file_info_struct->file_size = 0;
#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
      __attribute__((cleanup(
          simple_archiver_helper_cleanup_chdir_back))) char *original_cwd =
          NULL;
      if (user_cwd) {
        original_cwd = realpath(".", NULL);
        if (chdir(user_cwd)) {
          free(file_info_struct);
          return 1;
        }
      }
      struct stat stat_buf;
      memset(&stat_buf, 0, sizeof(struct stat));
      int stat_status = fstatat(AT_FDCWD, file_info_struct->filename, &stat_buf,
                                AT_SYMLINK_NOFOLLOW);
      if (stat_status != 0) {
        free(file_info_struct);
        return 1;
      }
      file_info_struct->bit_flags[0] = 0;
      file_info_struct->bit_flags[1] &= 0xFE;
      if ((stat_buf.st_mode & S_IRUSR) != 0) {
        file_info_struct->bit_flags[0] |= 1;
      }
      if ((stat_buf.st_mode & S_IWUSR) != 0) {
        file_info_struct->bit_flags[0] |= 2;
      }
      if ((stat_buf.st_mode & S_IXUSR) != 0) {
        file_info_struct->bit_flags[0] |= 4;
      }
      if ((stat_buf.st_mode & S_IRGRP) != 0) {
        file_info_struct->bit_flags[0] |= 8;
      }
      if ((stat_buf.st_mode & S_IWGRP) != 0) {
        file_info_struct->bit_flags[0] |= 0x10;
      }
      if ((stat_buf.st_mode & S_IXGRP) != 0) {
        file_info_struct->bit_flags[0] |= 0x20;
      }
      if ((stat_buf.st_mode & S_IROTH) != 0) {
        file_info_struct->bit_flags[0] |= 0x40;
      }
      if ((stat_buf.st_mode & S_IWOTH) != 0) {
        file_info_struct->bit_flags[0] |= 0x80;
      }
      if ((stat_buf.st_mode & S_IXOTH) != 0) {
        file_info_struct->bit_flags[1] |= 1;
      }
      file_info_struct->uid = stat_buf.st_uid;
      file_info_struct->gid = stat_buf.st_gid;
#endif
      if (state->parsed->flags & 0x1000) {
        file_info_struct->bit_flags[0] = 0;
        file_info_struct->bit_flags[1] &= 0xFE;

        file_info_struct->bit_flags[0] |=
          state->parsed->file_permissions & 0xFF;
        file_info_struct->bit_flags[1] |=
          (state->parsed->file_permissions & 0x100) >> 8;
      }
      if (state->parsed->flags & 0x400) {
        file_info_struct->uid = state->parsed->uid;
      }
      if (state->parsed->flags & 0x800) {
        file_info_struct->gid = state->parsed->gid;
      }
      __attribute__((cleanup(simple_archiver_helper_cleanup_FILE))) FILE *fd =
          fopen(file_info_struct->filename, "rb");
      if (!fd) {
        free(file_info_struct);
        return 1;
      }
      if (fseek(fd, 0, SEEK_END) < 0) {
        free(file_info_struct);
        return 1;
      }
      long ftell_ret = ftell(fd);
      if (ftell_ret < 0) {
        free(file_info_struct);
        return 1;
      }
      file_info_struct->file_size = (uint64_t)ftell_ret;
      if (pheap) {
        simple_archiver_priority_heap_insert(
            pheap, (int64_t)file_info_struct->file_size, file_info_struct,
            free_internal_file_info);
      } else {
        simple_archiver_list_add(files_list, file_info_struct,
                                 free_internal_file_info);
      }
    }
  }

  ++(*from_files_count);
  return 0;
}

int files_to_chunk_count(void *data, void *ud) {
  SDArchiverInternalFileInfo *file_info_struct = data;
  void **ptrs = ud;
  const uint64_t *chunk_size = ptrs[0];
  uint64_t *current_size = ptrs[1];
  uint64_t *current_count = ptrs[2];
  SDArchiverLinkedList *chunk_counts = ptrs[3];

  ++(*current_count);

  // Get file size.
  *current_size += file_info_struct->file_size;

  // Check size.
  if (*current_size >= *chunk_size) {
    uint64_t *count = malloc(sizeof(uint64_t));
    *count = *current_count;
    simple_archiver_list_add(chunk_counts, count, NULL);
    *current_count = 0;
    *current_size = 0;
  }

  return 0;
}

int greater_fn(int64_t a, int64_t b) { return a > b; }

void simple_archiver_internal_paths_to_files_map(SDArchiverHashMap *files_map,
                                                 const char *filename) {
  simple_archiver_hash_map_insert(
      files_map, (void *)1, strdup((const char *)filename),
      strlen((const char *)filename) + 1,
      simple_archiver_helper_datastructure_cleanup_nop, NULL);
  __attribute__((
      cleanup(simple_archiver_helper_cleanup_c_string))) char *filename_copy =
      strdup(filename);
  char *filename_dirname = dirname(filename_copy);

  while (strcmp(filename_dirname, ".") != 0) {
    if (!simple_archiver_hash_map_get(files_map, filename_dirname,
                                      strlen(filename_dirname) + 1)) {
      simple_archiver_hash_map_insert(
          files_map, (void *)1, strdup(filename_dirname),
          strlen(filename_dirname) + 1,
          simple_archiver_helper_datastructure_cleanup_nop, NULL);
    }
    filename_dirname = dirname(filename_dirname);
  }
}

int internal_write_dir_entries_v2_v3(void *data, void *ud) {
  const char *dir = data;
  void **ptrs = ud;
  FILE *out_f = ptrs[0];
  const SDArchiverState *state = ptrs[1];

  fprintf(stderr, "  %s\n", dir);

  const size_t prefix_length = state->parsed->prefix
                               ? strlen(state->parsed->prefix)
                               : 0;

  const size_t dir_name_length = strlen(dir);
  size_t total_name_length = dir_name_length + prefix_length;
  if (total_name_length >= 0xFFFF) {
    fprintf(stderr, "ERROR: Dirname \"%s\" is too long!\n", dir);
    return 1;
  }

  uint16_t u16 = (uint16_t)total_name_length;

  simple_archiver_helper_16_bit_be(&u16);
  if (fwrite(&u16, 2, 1, out_f) != 1) {
    fprintf(stderr, "ERROR: Failed to write dirname length for \"%s\"!\n", dir);
    return 1;
  }

  if (state->parsed->prefix) {
    if (fwrite(state->parsed->prefix, 1, prefix_length, out_f)
        != prefix_length) {
      fprintf(stderr,
              "ERROR: Failed to write prefix part of dirname \"%s\"!\n",
              dir);
      return 1;
    } else if (fwrite(dir, 1, dir_name_length + 1, out_f)
               != dir_name_length + 1) {
      fprintf(stderr,
              "ERROR: Failed to write (after prefix) dirname for \"%s\"!\n",
              dir);
      return 1;
    }
  } else if (fwrite(dir, 1, dir_name_length + 1, out_f)
             != dir_name_length + 1) {
    fprintf(stderr, "ERROR: Failed to write dirname for \"%s\"!\n", dir);
    return 1;
  }

  struct stat stat_buf;
  memset(&stat_buf, 0, sizeof(struct stat));
  int stat_fd = open(dir, O_RDONLY | O_DIRECTORY);
  if (stat_fd == -1) {
    fprintf(stderr, "ERROR: Failed to get stat of \"%s\"!\n", dir);
    return 1;
  }
  int ret = fstat(stat_fd, &stat_buf);
  close(stat_fd);
  if (ret != 0) {
    fprintf(stderr, "ERROR: Failed to fstat \"%s\"!\n", dir);
    return 1;
  }

  uint8_t u8 = 0;

  if (state && state->parsed->flags & 0x10000) {
    u8 = state->parsed->empty_dir_permissions & 0xFF;
  } else {
    if ((stat_buf.st_mode & S_IRUSR) != 0) {
      u8 |= 1;
    }
    if ((stat_buf.st_mode & S_IWUSR) != 0) {
      u8 |= 2;
    }
    if ((stat_buf.st_mode & S_IXUSR) != 0) {
      u8 |= 4;
    }
    if ((stat_buf.st_mode & S_IRGRP) != 0) {
      u8 |= 8;
    }
    if ((stat_buf.st_mode & S_IWGRP) != 0) {
      u8 |= 0x10;
    }
    if ((stat_buf.st_mode & S_IXGRP) != 0) {
      u8 |= 0x20;
    }
    if ((stat_buf.st_mode & S_IROTH) != 0) {
      u8 |= 0x40;
    }
    if ((stat_buf.st_mode & S_IWOTH) != 0) {
      u8 |= 0x80;
    }
  }

  if(fwrite(&u8, 1, 1, out_f) != 1) {
    fprintf(
      stderr,
      "ERROR: Failed to write permission bits (byte 0) for \"%s\"!\n", dir);
    return 1;
  }

  u8 = 0;
  if (state && state->parsed->flags & 0x10000) {
    u8 = (state->parsed->empty_dir_permissions & 0x100) >> 8;
  } else {
    if ((stat_buf.st_mode & S_IXOTH) != 0) {
      u8 |= 1;
    }
  }

  if (fwrite(&u8, 1, 1, out_f) != 1) {
    fprintf(
      stderr,
      "ERROR: Failed to write permission bits (byte 1) for \"%s\"!\n", dir);
    return 1;
  }

  uint32_t u32 = stat_buf.st_uid;
  if (state->parsed->flags & 0x400) {
    u32 = state->parsed->uid;
  } else {
    uint32_t mapped_uid;
    if (simple_archiver_get_uid_mapping(state->parsed->mappings,
                                        state->parsed->users_infos,
                                        u32,
                                        &mapped_uid,
                                        NULL) == 0) {
      //fprintf(stderr,
      //        "NOTICE: Mapped UID %" PRIu32 " to %" PRIu32 "\n",
      //        u32,
      //        mapped_uid);
      u32 = mapped_uid;
    }
  }

  simple_archiver_helper_32_bit_be(&u32);
  if (fwrite(&u32, 4, 1, out_f) != 1) {
    fprintf(stderr, "ERROR: Failed to write UID for \"%s\"!\n", dir);
    return 1;
  }

  u32 = stat_buf.st_gid;
  if (state->parsed->flags & 0x800) {
    u32 = state->parsed->gid;
  } else {
    uint32_t mapped_gid;
    if (simple_archiver_get_gid_mapping(state->parsed->mappings,
                                        state->parsed->users_infos,
                                        u32,
                                        &mapped_gid,
                                        NULL) == 0) {
      //fprintf(stderr,
      //        "NOTICE: Mapped GID %" PRIu32 " to %" PRIu32 "\n",
      //        u32,
      //        mapped_gid);
      u32 = mapped_gid;
    }
  }
  simple_archiver_helper_32_bit_be(&u32);
  if (fwrite(&u32, 4, 1, out_f) != 1) {
    fprintf(stderr, "ERROR: Failed to write GID for \"%s\"!\n", dir);
    return 1;
  }

  if (state->parsed->write_version == 3) {
    u32 = stat_buf.st_uid;
    if (state->parsed->flags & 0x400) {
      u32 = state->parsed->uid;
    }
    __attribute__((cleanup(simple_archiver_helper_cleanup_c_string)))
    char *to_cleanup_user = NULL;
    const char *username = simple_archiver_hash_map_get(
      state->parsed->users_infos.UidToUname, &u32, sizeof(uint32_t));
    if (username) {
      if ((state->parsed->flags & 0x400) == 0) {
        uint32_t out_uid;
        const char *mapped_user = NULL;
        if (simple_archiver_get_user_mapping(state->parsed->mappings,
                                             state->parsed->users_infos,
                                             username,
                                             &out_uid,
                                             &mapped_user) == 0
            && mapped_user) {
          //fprintf(stderr,
          //        "NOTICE: Mapped User %s to %s\n",
          //        username,
          //        mapped_user);
          username = mapped_user;
          to_cleanup_user = (char *)mapped_user;
        }
      }
      unsigned long length = strlen(username);
      if (length > 0xFFFF) {
        fprintf(stderr, "ERROR: Username is too long for dir \"%s\"!\n", dir);
        return 1;
      }
      u16 = (uint16_t)length;
      simple_archiver_helper_16_bit_be(&u16);
      if (fwrite(&u16, 2, 1, out_f) != 1) {
        fprintf(
          stderr,
          "ERROR: Failed to write username length for dir \"%s\"!\n", dir);
        return 1;
      } else if (fwrite(username, 1, length + 1, out_f) != length + 1) {
        fprintf(
          stderr, "ERROR: Failed to write username for dir \"%s\"!\n", dir);
        return 1;
      }
    } else {
      u16 = 0;
      if (fwrite(&u16, 2, 1, out_f) != 1) {
        fprintf(stderr,
                "ERROR: Failed to write 0 bytes for username for dir \"%s\"\n!",
                dir);
        return 1;
      }
    }

    u32 = stat_buf.st_gid;
    if (state->parsed->flags & 0x800) {
      u32 = state->parsed->gid;
    }
    __attribute__((cleanup(simple_archiver_helper_cleanup_c_string)))
    char *to_cleanup_group = NULL;
    const char *groupname = simple_archiver_hash_map_get(
      state->parsed->users_infos.GidToGname, &u32, sizeof(uint32_t));
    if (groupname) {
      if ((state->parsed->flags & 0x800) == 0) {
        uint32_t out_gid;
        const char *mapped_group = NULL;
        if (simple_archiver_get_group_mapping(state->parsed->mappings,
                                              state->parsed->users_infos,
                                              groupname,
                                              &out_gid,
                                              &mapped_group) == 0
            && mapped_group) {
          //fprintf(stderr,
          //        "NOTICE: Mapped Group %s to %s\n",
          //        groupname,
          //        mapped_group);
          groupname = mapped_group;
          to_cleanup_group = (char *)mapped_group;
        }
      }
      unsigned long length = strlen(groupname);
      if (length > 0xFFFF) {
        fprintf(stderr, "ERROR: Groupname is too long for dir \"%s\"!\n", dir);
        return 1;
      }
      u16 = (uint16_t)length;
      simple_archiver_helper_16_bit_be(&u16);
      if (fwrite(&u16, 2, 1, out_f) != 1) {
        fprintf(
          stderr,
          "ERROR: Failed to write Groupname length for dir \"%s\"!\n", dir);
        return 1;
      } else if (fwrite(groupname, 1, length + 1, out_f) != length + 1) {
        fprintf(
          stderr, "ERROR: Failed to write Groupname for dir \"%s\"!\n", dir);
        return 1;
      }
    } else {
      u16 = 0;
      if (fwrite(&u16, 2, 1, out_f) != 1) {
        fprintf(
          stderr,
          "ERROR: Failed to write 0 bytes for Groupname for dir \"%s\"\n!",
          dir);
        return 1;
      }
    }
  }

  return 0;
}

mode_t simple_archiver_internal_permissions_to_mode_t(uint_fast16_t permissions)
{
  return
      ((permissions & 1) ? S_IRUSR : 0)
    | ((permissions & 2) ? S_IWUSR : 0)
    | ((permissions & 4) ? S_IXUSR : 0)
    | ((permissions & 8) ? S_IRGRP : 0)
    | ((permissions & 0x10) ? S_IWGRP : 0)
    | ((permissions & 0x20) ? S_IXGRP : 0)
    | ((permissions & 0x40) ? S_IROTH : 0)
    | ((permissions & 0x80) ? S_IWOTH : 0)
    | ((permissions & 0x100) ? S_IXOTH : 0);
}

mode_t simple_archiver_internal_bits_to_mode_t(const uint8_t perms[restrict 2])
{
  return ((perms[0] & 1) ? S_IRUSR : 0)
       | ((perms[0] & 2) ? S_IWUSR : 0)
       | ((perms[0] & 4) ? S_IXUSR : 0)
       | ((perms[0] & 8) ? S_IRGRP : 0)
       | ((perms[0] & 0x10) ? S_IWGRP : 0)
       | ((perms[0] & 0x20) ? S_IXGRP : 0)
       | ((perms[0] & 0x40) ? S_IROTH : 0)
       | ((perms[0] & 0x80) ? S_IWOTH : 0)
       | ((perms[1] & 1)    ? S_IXOTH : 0);
}

char *simple_archiver_error_to_string(enum SDArchiverStateReturns error) {
  switch (error) {
    case SDAS_SUCCESS:
      return "SUCCESS";
    case SDAS_HEADER_ALREADY_WRITTEN:
      return "Header already written";
    case SDAS_FAILED_TO_WRITE:
      return "Failed to write";
    case SDAS_NO_COMPRESSOR:
      return "Compressor cmd is missing";
    case SDAS_NO_DECOMPRESSOR:
      return "Decompressor cmd is missing";
    case SDAS_INVALID_PARSED_STATE:
      return "Invalid parsed struct";
    case SDAS_INVALID_FILE:
      return "Invalid file";
    case SDAS_INTERNAL_ERROR:
      return "Internal error";
    case SDAS_FAILED_TO_CREATE_MAP:
      return "Failed to create set of filenames (internal error)";
    case SDAS_FAILED_TO_EXTRACT_SYMLINK:
      return "Failed to extract symlink (internal error)";
    case SDAS_FAILED_TO_CHANGE_CWD:
      return "Failed to change current working directory";
    case SDAS_INVALID_WRITE_VERSION:
      return "Unsupported write version file format";
    case SDAS_SIGINT:
      return "Interrupt signal SIGINT recieved";
    case SDAS_TOO_MANY_DIRS:
      return "Too many directories (limit is 2^32)";
    default:
      return "Unknown error";
  }
}

SDArchiverState *simple_archiver_init_state(const SDArchiverParsed *parsed) {
  if (!parsed) {
    return NULL;
  }

  SDArchiverState *state = malloc(sizeof(SDArchiverState));
  state->flags = 0;
  state->parsed = parsed;
  state->out_f = NULL;
  state->map = NULL;
  state->count = 0;
  state->max = 0;
  state->digits = 10;

  return state;
}

void simple_archiver_free_state(SDArchiverState **state) {
  if (state && *state) {
    free(*state);
    *state = NULL;
  }
}

int simple_archiver_write_all(FILE *out_f, SDArchiverState *state,
                              const SDArchiverLinkedList *filenames) {
#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
  signal(SIGINT, handle_sig_int);
#endif
  switch (state->parsed->write_version) {
    case 0:
      return simple_archiver_write_v0(out_f, state, filenames);
    case 1:
      return simple_archiver_write_v1(out_f, state, filenames);
    case 2:
      return simple_archiver_write_v2(out_f, state, filenames);
    case 3:
      return simple_archiver_write_v3(out_f, state, filenames);
    default:
      fprintf(stderr, "ERROR: Unsupported write version %" PRIu32 "!\n",
              state->parsed->write_version);
      return SDAS_INVALID_WRITE_VERSION;
  }
}

int simple_archiver_write_v0(FILE *out_f, SDArchiverState *state,
                             const SDArchiverLinkedList *filenames) {
  fprintf(stderr, "Writing archive of file format 0\n");
  // First create a "set" of absolute paths to given filenames.
  __attribute__((cleanup(simple_archiver_hash_map_free)))
  SDArchiverHashMap *abs_filenames = simple_archiver_hash_map_init();
  void **ptr_array = malloc(sizeof(void *) * 2);
  ptr_array[0] = abs_filenames;
  ptr_array[1] = (void *)state->parsed->user_cwd;
  if (simple_archiver_list_get(filenames, filenames_to_abs_map_fn, ptr_array)) {
    free(ptr_array);
    return SDAS_FAILED_TO_CREATE_MAP;
  }
  free(ptr_array);

  if (fwrite("SIMPLE_ARCHIVE_VER", 1, 18, out_f) != 18) {
    return SDAS_FAILED_TO_WRITE;
  }

  uint16_t u16 = 0;

  // No need to convert to big-endian for version 0.
  // simple_archiver_helper_16_bit_be(&u16);

  if (fwrite(&u16, 2, 1, out_f) != 1) {
    return SDAS_FAILED_TO_WRITE;
  }

  if (state->parsed->compressor && !state->parsed->decompressor) {
    return SDAS_NO_DECOMPRESSOR;
  } else if (!state->parsed->compressor && state->parsed->decompressor) {
    return SDAS_NO_COMPRESSOR;
  } else if (state->parsed->compressor && state->parsed->decompressor) {
    // Write the four flag bytes with first bit set.
    uint8_t c = 1;
    if (fwrite(&c, 1, 1, out_f) != 1) {
      return SDAS_FAILED_TO_WRITE;
    }
    c = 0;
    for (size_t i = 0; i < 3; ++i) {
      if (fwrite(&c, 1, 1, out_f) != 1) {
        return SDAS_FAILED_TO_WRITE;
      }
    }

    // De/compressor bytes.
    size_t temp_size = strlen(state->parsed->compressor);
    if (temp_size > 0xFFFF) {
      fprintf(stderr, "ERROR: Compressor cmd string is too large!\n");
      return SDAS_NO_COMPRESSOR;
    }
    u16 = (uint16_t)temp_size;
    // To big-endian.
    simple_archiver_helper_16_bit_be(&u16);
    // Write the size in big-endian.
    if (fwrite(&u16, 2, 1, out_f) != 1) {
      return SDAS_FAILED_TO_WRITE;
    }
    // From big-endian.
    simple_archiver_helper_16_bit_be(&u16);
    // Write the compressor cmd including the NULL at the end of the string.
    if (fwrite(state->parsed->compressor, 1, u16 + 1, out_f) !=
        (size_t)u16 + 1) {
      return SDAS_FAILED_TO_WRITE;
    }

    temp_size = strlen(state->parsed->decompressor);
    if (temp_size > 0xFFFF) {
      fprintf(stderr, "ERROR: Decompressor cmd string is too large!\n");
      return SDAS_NO_DECOMPRESSOR;
    }
    u16 = (uint16_t)temp_size;
    // To big-endian.
    simple_archiver_helper_16_bit_be(&u16);
    // Write the size in big-endian.
    if (fwrite(&u16, 2, 1, out_f) != 1) {
      return SDAS_FAILED_TO_WRITE;
    }
    // From big-endian.
    simple_archiver_helper_16_bit_be(&u16);
    // Write the decompressor cmd including the NULL at the end of the string.
    if (fwrite(state->parsed->decompressor, 1, u16 + 1, out_f) !=
        (size_t)u16 + 1) {
      return SDAS_FAILED_TO_WRITE;
    }
  } else {
    // Write the four flag bytes with first bit NOT set.
    uint8_t c = 0;
    for (size_t i = 0; i < 4; ++i) {
      if (fwrite(&c, 1, 1, out_f) != 1) {
        return SDAS_FAILED_TO_WRITE;
      }
    }
  }

  // Write file count.
  {
    if (filenames->count > 0xFFFFFFFF) {
      fprintf(stderr, "ERROR: Filenames count is too large!\n");
      return SDAS_INTERNAL_ERROR;
    }
    uint32_t u32 = (uint32_t)filenames->count;
    simple_archiver_helper_32_bit_be(&u32);
    if (fwrite(&u32, 1, 4, out_f) != 4) {
      return SDAS_FAILED_TO_WRITE;
    }
  }

  if (is_sig_int_occurred) {
    return SDAS_SIGINT;
  }

  // Iterate over files in list to write.
  state->count = 0;
  state->max = filenames->count;
  state->out_f = out_f;
  state->map = abs_filenames;
  state->digits = simple_archiver_helper_num_digits(state->max);
  fprintf(stderr, "Begin archiving...\n");
  __attribute__((cleanup(
      simple_archiver_helper_cleanup_chdir_back))) char *original_cwd = NULL;
  if (state->parsed->user_cwd) {
#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
    original_cwd = realpath(".", NULL);
    if (chdir(state->parsed->user_cwd)) {
      return 1;
    }
#endif
  }
  char format_str[64];
  snprintf(format_str, 64, FILE_COUNTS_OUTPUT_FORMAT_STR_1, state->digits,
           state->digits);
  fprintf(stderr, format_str, state->count, state->max);
  if (simple_archiver_list_get(filenames, write_files_fn_file_v0, state)) {
    if (is_sig_int_occurred) {
      return SDAS_SIGINT;
    }
    // Error occurred.
    fprintf(stderr, "Error ocurred writing file(s) to archive.\n");
    return SDAS_FAILED_TO_WRITE;
  }
  state->out_f = NULL;

  fprintf(stderr, "End archiving.\n");
  return SDAS_SUCCESS;
}

int simple_archiver_write_v1(FILE *out_f, SDArchiverState *state,
                             const SDArchiverLinkedList *filenames) {
  fprintf(stderr, "Writing archive of file format 1\n");
  // First create a "set" of absolute paths to given filenames.
  __attribute__((cleanup(simple_archiver_hash_map_free)))
  SDArchiverHashMap *abs_filenames = simple_archiver_hash_map_init();
  void **ptr_array = malloc(sizeof(void *) * 2);
  ptr_array[0] = abs_filenames;
  ptr_array[1] = (void *)state->parsed->user_cwd;
  if (simple_archiver_list_get(filenames, filenames_to_abs_map_fn, ptr_array)) {
    free(ptr_array);
    return SDAS_FAILED_TO_CREATE_MAP;
  }
  free(ptr_array);

  // Get a list of symlinks and a list of files.
  __attribute__((cleanup(simple_archiver_list_free)))
  SDArchiverLinkedList *symlinks_list = simple_archiver_list_init();
  __attribute__((cleanup(simple_archiver_list_free)))
  SDArchiverLinkedList *files_list = simple_archiver_list_init();
  __attribute__((cleanup(simple_archiver_priority_heap_free)))
  SDArchiverPHeap *files_pheap =
      (state->parsed->flags & 0x40)
          ? simple_archiver_priority_heap_init_less_fn(greater_fn)
          : NULL;
  uint64_t from_files_count = 0;

  ptr_array = malloc(sizeof(void *) * 7);
  ptr_array[0] = symlinks_list;
  ptr_array[1] = files_list;
  ptr_array[2] = (void *)state->parsed->user_cwd;
  ptr_array[3] = files_pheap;
  ptr_array[4] = NULL;
  ptr_array[5] = state;
  ptr_array[6] = &from_files_count;

  if (simple_archiver_list_get(filenames, symlinks_and_files_from_files,
                               ptr_array)) {
    free(ptr_array);
    return SDAS_INTERNAL_ERROR;
  }
  free(ptr_array);

  if (files_pheap) {
    while (files_pheap->size > 0) {
      simple_archiver_list_add(files_list,
                               simple_archiver_priority_heap_pop(files_pheap),
                               free_internal_file_info);
    }
    simple_archiver_priority_heap_free(&files_pheap);
  }

  if (symlinks_list->count + files_list->count != from_files_count) {
    fprintf(stderr,
            "ERROR: Count mismatch between files and symlinks and files from "
            "parser!\n");
    return SDAS_INTERNAL_ERROR;
  }

  if (fwrite("SIMPLE_ARCHIVE_VER", 1, 18, out_f) != 18) {
    return SDAS_FAILED_TO_WRITE;
  }

  char buf[SIMPLE_ARCHIVER_BUFFER_SIZE];
  uint16_t u16 = 1;

  simple_archiver_helper_16_bit_be(&u16);

  if (fwrite(&u16, 2, 1, out_f) != 1) {
    return SDAS_FAILED_TO_WRITE;
  }

  if (state->parsed->compressor && !state->parsed->decompressor) {
    return SDAS_NO_DECOMPRESSOR;
  } else if (!state->parsed->compressor && state->parsed->decompressor) {
    return SDAS_NO_COMPRESSOR;
  } else if (state->parsed->compressor && state->parsed->decompressor) {
    // 4 bytes flags, using de/compressor.
    memset(buf, 0, 4);
    buf[0] |= 1;
    if (fwrite(buf, 1, 4, out_f) != 4) {
      return SDAS_FAILED_TO_WRITE;
    }

    size_t len = strlen(state->parsed->compressor);
    if (len >= 0xFFFF) {
      fprintf(stderr, "ERROR: Compressor cmd is too long!\n");
      return SDAS_INVALID_PARSED_STATE;
    }

    u16 = (uint16_t)len;
    simple_archiver_helper_16_bit_be(&u16);
    if (fwrite(&u16, 1, 2, out_f) != 2) {
      return SDAS_FAILED_TO_WRITE;
    }
    simple_archiver_helper_16_bit_be(&u16);

    if (fwrite(state->parsed->compressor, 1, u16 + 1, out_f) !=
        (size_t)u16 + 1) {
      return SDAS_FAILED_TO_WRITE;
    }

    len = strlen(state->parsed->decompressor);
    if (len >= 0xFFFF) {
      fprintf(stderr, "ERROR: Decompressor cmd is too long!\n");
      return SDAS_INVALID_PARSED_STATE;
    }

    u16 = (uint16_t)len;
    simple_archiver_helper_16_bit_be(&u16);
    if (fwrite(&u16, 1, 2, out_f) != 2) {
      return SDAS_FAILED_TO_WRITE;
    }
    simple_archiver_helper_16_bit_be(&u16);

    if (fwrite(state->parsed->decompressor, 1, u16 + 1, out_f) !=
        (size_t)u16 + 1) {
      return SDAS_FAILED_TO_WRITE;
    }
  } else {
    // 4 bytes flags, not using de/compressor.
    memset(buf, 0, 4);
    if (fwrite(buf, 1, 4, out_f) != 4) {
      return SDAS_FAILED_TO_WRITE;
    }
  }

  if (symlinks_list->count > 0xFFFFFFFF) {
    fprintf(stderr, "ERROR: Too many symlinks!\n");
    return SDAS_INVALID_PARSED_STATE;
  }

  uint32_t u32 = (uint32_t)symlinks_list->count;
  simple_archiver_helper_32_bit_be(&u32);
  if (fwrite(&u32, 4, 1, out_f) != 1) {
    return SDAS_FAILED_TO_WRITE;
  }
  simple_archiver_helper_32_bit_be(&u32);

  // Change cwd if user specified.
  __attribute__((cleanup(
      simple_archiver_helper_cleanup_chdir_back))) char *original_cwd = NULL;
  if (state->parsed->user_cwd) {
#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
    original_cwd = realpath(".", NULL);
    if (chdir(state->parsed->user_cwd)) {
      return SDAS_INTERNAL_ERROR;
    }
#endif
  }

  const size_t prefix_length = state->parsed->prefix
                               ? strlen(state->parsed->prefix)
                               : 0;
  {
    const SDArchiverLLNode *node = symlinks_list->head;
    for (u32 = 0;
         u32 < (uint32_t)symlinks_list->count && node != symlinks_list->tail;) {
      node = node->next;
      ++u32;
      memset(buf, 0, 2);

      uint_fast8_t is_invalid = 0;

#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
      __attribute__((cleanup(
          simple_archiver_helper_cleanup_malloced))) void *abs_path = NULL;
      __attribute__((cleanup(
          simple_archiver_helper_cleanup_malloced))) void *rel_path = NULL;
      if ((state->parsed->flags & 0x100) != 0) {
        // Preserve symlink target.
        char *path_buf = malloc(1024);
        ssize_t ret = readlink(node->data, path_buf, 1023);
        if (ret == -1) {
          fprintf(stderr, "WARNING: Failed to get symlink's target!\n");
          free(path_buf);
          is_invalid = 1;
        } else {
          path_buf[ret] = 0;
          if (path_buf[0] == '/') {
            abs_path = path_buf;
            buf[0] |= 1;
          } else {
            rel_path = path_buf;
          }
        }
      } else {
        abs_path = realpath(node->data, NULL);
        // Check if symlink points to thing to be stored into archive.
        if (abs_path) {
          __attribute__((cleanup(
              simple_archiver_helper_cleanup_malloced))) void *link_abs_path =
              simple_archiver_helper_real_path_to_name(node->data);
          if (!link_abs_path) {
            fprintf(stderr, "WARNING: Failed to get absolute path to link!\n");
          } else {
            rel_path = simple_archiver_filenames_to_relative_path(link_abs_path,
                                                                  abs_path);
          }
        }
      }

      if (abs_path && (state->parsed->flags & 0x20) == 0 &&
          (state->parsed->flags & 0x100) == 0 &&
          !simple_archiver_hash_map_get(abs_filenames, abs_path,
                                        strlen(abs_path) + 1)) {
        // Is not a filename being archived.
        buf[1] |= 0x8;
        if ((state->parsed->flags & 0x80) == 0) {
          // Not a "safe link", mark invalid and continue.
          is_invalid = 1;
          fprintf(stderr,
                  "WARNING: \"%s\" points to outside of archived files (or is "
                  "invalid) and \"--no-safe-links\" not specified, will not "
                  "store abs/rel-links to this entry!\n",
                  (const char *)node->data);
        } else {
          // Safe links disabled, set preference to absolute path.
          buf[0] |= 1;
        }
      } else if ((state->parsed->flags & 0x100) != 0 &&
                 (state->parsed->flags & 0x80) == 0 && !is_invalid) {
        __attribute__((cleanup(
            simple_archiver_helper_cleanup_c_string))) char *target_realpath =
            realpath(node->data, NULL);
        if (!target_realpath) {
          fprintf(
              stderr,
              "WARNING: \"%s\" is an invalid symlink and \"--no-safe-links\" "
              "not specified, will skip this symlink!\n",
              (const char *)node->data);
          is_invalid = 1;
        } else if (!simple_archiver_hash_map_get(abs_filenames, target_realpath,
                                                 strlen(target_realpath) + 1)) {
          fprintf(
              stderr,
              "WARNING: \"%s\" points to outside of archived files and "
              "\"--no-safe-links\" not specified, will skip this symlink!\n",
              (const char *)node->data);
          is_invalid = 1;
        }
      }

      if (!abs_path && !rel_path) {
        // No valid paths, mark as invalid.
        fprintf(stderr,
                "WARNING: \"%s\" is an invalid symlink, will not store rel/abs "
                "link paths!\n",
                (const char *)node->data);
        is_invalid = 1;
      }

      // Get symlink stats for permissions.
      struct stat stat_buf;
      memset(&stat_buf, 0, sizeof(struct stat));
      int stat_status =
          fstatat(AT_FDCWD, node->data, &stat_buf, AT_SYMLINK_NOFOLLOW);
      if (stat_status != 0) {
        return SDAS_INTERNAL_ERROR;
      }

      if ((stat_buf.st_mode & S_IRUSR) != 0) {
        buf[0] |= 2;
      }
      if ((stat_buf.st_mode & S_IWUSR) != 0) {
        buf[0] |= 4;
      }
      if ((stat_buf.st_mode & S_IXUSR) != 0) {
        buf[0] |= 8;
      }
      if ((stat_buf.st_mode & S_IRGRP) != 0) {
        buf[0] |= 0x10;
      }
      if ((stat_buf.st_mode & S_IWGRP) != 0) {
        buf[0] |= 0x20;
      }
      if ((stat_buf.st_mode & S_IXGRP) != 0) {
        buf[0] |= 0x40;
      }
      if ((stat_buf.st_mode & S_IROTH) != 0) {
        buf[0] |= (char)0x80;
      }
      if ((stat_buf.st_mode & S_IWOTH) != 0) {
        buf[1] |= 1;
      }
      if ((stat_buf.st_mode & S_IXOTH) != 0) {
        buf[1] |= 2;
      }
#else
      buf[0] = 0xFE;
      buf[1] = 0xB;
#endif

      if (is_invalid) {
        buf[1] |= 4;
      }

      if (fwrite(buf, 1, 2, out_f) != 2) {
        return SDAS_FAILED_TO_WRITE;
      }

      const size_t link_length = strlen(node->data);
      size_t len = link_length;
      if (state->parsed->prefix) {
        len += prefix_length;
      }
      if (len >= 0xFFFF) {
        fprintf(stderr, "ERROR: Link name is too long!\n");
        return SDAS_INVALID_PARSED_STATE;
      }

      u16 = (uint16_t)len;
      simple_archiver_helper_16_bit_be(&u16);
      if (fwrite(&u16, 2, 1, out_f) != 1) {
        return SDAS_FAILED_TO_WRITE;
      }
      simple_archiver_helper_16_bit_be(&u16);
      if (state->parsed->prefix) {
        size_t fwrite_ret = fwrite(state->parsed->prefix,
                                   1,
                                   prefix_length,
                                   out_f);
        fwrite_ret += fwrite(node->data, 1, link_length + 1, out_f);
        if (fwrite_ret != (size_t)u16 + 1) {
          return SDAS_FAILED_TO_WRITE;
        }
      } else if (fwrite(node->data, 1, u16 + 1, out_f) != (size_t)u16 + 1) {
        return SDAS_FAILED_TO_WRITE;
      }

      if (abs_path && (state->parsed->flags & 0x20) == 0 && !is_invalid) {
        if (state->parsed->prefix) {
          __attribute__((cleanup(simple_archiver_helper_cleanup_c_string)))
          char *abs_path_prefixed =
            simple_archiver_helper_insert_prefix_in_link_path(
              state->parsed->prefix, node->data, abs_path);
          if (!abs_path_prefixed) {
            fprintf(stderr,
                    "ERROR: Failed to add prefix to abs symlink!\n");
            return SDAS_INTERNAL_ERROR;
          }
          const size_t abs_path_pref_length = strlen(abs_path_prefixed);
          if (abs_path_pref_length >= 0xFFFF) {
            fprintf(stderr,
                    "ERROR: Symlink destination absolute path with prefix is "
                    "too long!\n");
            return SDAS_INVALID_PARSED_STATE;
          }

          u16 = (uint16_t)abs_path_pref_length;
          simple_archiver_helper_16_bit_be(&u16);
          if (fwrite(&u16, 2, 1, out_f) != 1) {
            return SDAS_FAILED_TO_WRITE;
          }
          simple_archiver_helper_16_bit_be(&u16);
          if (fwrite(abs_path_prefixed, 1, u16 + 1, out_f) != (size_t)u16 + 1) {
            return SDAS_FAILED_TO_WRITE;
          }
        } else {
          const size_t abs_path_length = strlen(abs_path);
          if (abs_path_length >= 0xFFFF) {
            fprintf(stderr,
                    "ERROR: Symlink destination absolute path is too long!\n");
            return SDAS_INVALID_PARSED_STATE;
          }

          u16 = (uint16_t)abs_path_length;
          simple_archiver_helper_16_bit_be(&u16);
          if (fwrite(&u16, 2, 1, out_f) != 1) {
            return SDAS_FAILED_TO_WRITE;
          }
          simple_archiver_helper_16_bit_be(&u16);
          if (fwrite(abs_path, 1, u16 + 1, out_f) != (size_t)u16 + 1) {
            return SDAS_FAILED_TO_WRITE;
          }
        }
      } else {
        u16 = 0;
        if (fwrite(&u16, 2, 1, out_f) != 1) {
          return SDAS_FAILED_TO_WRITE;
        }
      }

      if (rel_path && !is_invalid) {
        if (state->parsed->prefix) {
          __attribute__((cleanup(simple_archiver_helper_cleanup_c_string)))
          char *rel_path_prefixed =
            simple_archiver_helper_insert_prefix_in_link_path(
              state->parsed->prefix, node->data, rel_path);
          if (!rel_path_prefixed) {
            fprintf(stderr,
                    "ERROR: Failed to add prefix to relative symlink!\n");
            return SDAS_INTERNAL_ERROR;
          }
          const size_t rel_path_pref_length = strlen(rel_path_prefixed);
          if (rel_path_pref_length >= 0xFFFF) {
            fprintf(stderr,
                    "ERROR: Symlink destination relative path with prefix is "
                    "too long!\n");
            return SDAS_INVALID_PARSED_STATE;
          }

          u16 = (uint16_t)rel_path_pref_length;
          simple_archiver_helper_16_bit_be(&u16);
          if (fwrite(&u16, 2, 1, out_f) != 1) {
            return SDAS_FAILED_TO_WRITE;
          }
          simple_archiver_helper_16_bit_be(&u16);
          if (fwrite(rel_path_prefixed, 1, u16 + 1, out_f) != (size_t)u16 + 1) {
            return SDAS_FAILED_TO_WRITE;
          }
        } else {
          len = strlen(rel_path);
          if (len >= 0xFFFF) {
            fprintf(stderr,
                    "ERROR: Symlink destination relative path is too long!\n");
            return SDAS_INVALID_PARSED_STATE;
          }

          u16 = (uint16_t)len;
          simple_archiver_helper_16_bit_be(&u16);
          if (fwrite(&u16, 2, 1, out_f) != 1) {
            return SDAS_FAILED_TO_WRITE;
          }
          simple_archiver_helper_16_bit_be(&u16);
          if (fwrite(rel_path, 1, u16 + 1, out_f) != (size_t)u16 + 1) {
            return SDAS_FAILED_TO_WRITE;
          }
        }
      } else {
        u16 = 0;
        if (fwrite(&u16, 2, 1, out_f) != 1) {
          return SDAS_FAILED_TO_WRITE;
        }
      }
    }
    if (u32 != (uint32_t)symlinks_list->count) {
      fprintf(stderr,
              "ERROR: Iterated through %" PRIu32 " symlinks out of %zu total!"
                "\n",
              u32, symlinks_list->count);
      return SDAS_INTERNAL_ERROR;
    }
  }

  if (is_sig_int_occurred) {
    return SDAS_SIGINT;
  }

  __attribute__((cleanup(simple_archiver_list_free)))
  SDArchiverLinkedList *chunk_counts = simple_archiver_list_init();

  {
    uint64_t current_size = 0;
    uint64_t current_count = 0;
    void **ptrs = malloc(sizeof(void *) * 4);
    ptrs[0] = (void *)&state->parsed->minimum_chunk_size;
    ptrs[1] = &current_size;
    ptrs[2] = &current_count;
    ptrs[3] = chunk_counts;
    if (simple_archiver_list_get(files_list, files_to_chunk_count, ptrs)) {
      free(ptrs);
      fprintf(stderr, "ERROR: Internal error calculating chunk counts!\n");
      return SDAS_INTERNAL_ERROR;
    }
    free(ptrs);
    if ((chunk_counts->count == 0 || current_size > 0) && current_count > 0) {
      uint64_t *count = malloc(sizeof(uint64_t));
      *count = current_count;
      simple_archiver_list_add(chunk_counts, count, NULL);
    }
  }

  // Verify chunk counts.
  {
    uint64_t count = 0;
    for (SDArchiverLLNode *node = chunk_counts->head->next;
         node != chunk_counts->tail; node = node->next) {
      if (*((uint64_t *)node->data) > 0xFFFFFFFF) {
        fprintf(stderr, "ERROR: file count in chunk is too large!\n");
        return SDAS_INTERNAL_ERROR;
      }
      count += *((uint64_t *)node->data);
      // fprintf(stderr, "DEBUG: chunk count %4llu\n",
      // *((uint64_t*)node->data));
    }
    if (count != files_list->count) {
      fprintf(stderr,
              "ERROR: Internal error calculating chunk counts (invalid number "
              "of files)!\n");
      return SDAS_INTERNAL_ERROR;
    }
  }

  // Write number of chunks.
  if (chunk_counts->count > 0xFFFFFFFF) {
    fprintf(stderr, "ERROR: Too many chunks!\n");
    return SDAS_INTERNAL_ERROR;
  }
  u32 = (uint32_t)chunk_counts->count;
  simple_archiver_helper_32_bit_be(&u32);
  if (fwrite(&u32, 4, 1, out_f) != 1) {
    return SDAS_FAILED_TO_WRITE;
  }

  __attribute__((cleanup(simple_archiver_helper_cleanup_malloced))) void
      *non_compressing_chunk_size = NULL;
  if (!state->parsed->compressor || !state->parsed->decompressor) {
    non_compressing_chunk_size = malloc(sizeof(uint64_t));
  }
  uint64_t *non_c_chunk_size = non_compressing_chunk_size;

  SDArchiverLLNode *file_node = files_list->head;
  uint64_t chunk_count = 0;
  for (SDArchiverLLNode *chunk_c_node = chunk_counts->head->next;
       chunk_c_node != chunk_counts->tail; chunk_c_node = chunk_c_node->next) {
    if (is_sig_int_occurred) {
      return SDAS_SIGINT;
    }
    fprintf(stderr,
            "CHUNK %3" PRIu64 " of %3zu\n",
            ++chunk_count,
            chunk_counts->count);
    // Write file count before iterating through files.
    if (non_c_chunk_size) {
      *non_c_chunk_size = 0;
    }

    u32 = (uint32_t)(*((uint64_t *)chunk_c_node->data));
    simple_archiver_helper_32_bit_be(&u32);
    if (fwrite(&u32, 4, 1, out_f) != 1) {
      return SDAS_FAILED_TO_WRITE;
    }
    SDArchiverLLNode *saved_node = file_node;
    for (uint64_t file_idx = 0; file_idx < *((uint64_t *)chunk_c_node->data);
         ++file_idx) {
      file_node = file_node->next;
      if (file_node == files_list->tail) {
        return SDAS_INTERNAL_ERROR;
      }
      const SDArchiverInternalFileInfo *file_info_struct = file_node->data;
      if (non_c_chunk_size) {
        *non_c_chunk_size += file_info_struct->file_size;
      }
      const size_t filename_len = strlen(file_info_struct->filename);
      if (state->parsed->prefix) {
        const size_t total_length = filename_len + prefix_length;
        if (total_length >= 0xFFFF) {
          fprintf(stderr, "ERROR: Filename with prefix is too large!\n");
          return SDAS_INVALID_FILE;
        }
        u16 = (uint16_t)total_length;
        simple_archiver_helper_16_bit_be(&u16);
        if (fwrite(&u16, 2, 1, out_f) != 1) {
          return SDAS_FAILED_TO_WRITE;
        }
        simple_archiver_helper_16_bit_be(&u16);
        if (fwrite(state->parsed->prefix, 1, prefix_length, out_f)
            != prefix_length) {
          return SDAS_FAILED_TO_WRITE;
        } else if (fwrite(file_info_struct->filename,
                          1,
                          filename_len + 1,
                          out_f)
                     != filename_len + 1) {
          return SDAS_FAILED_TO_WRITE;
        }
      } else {
        if (filename_len >= 0xFFFF) {
          fprintf(stderr, "ERROR: Filename is too large!\n");
          return SDAS_INVALID_FILE;
        }
        u16 = (uint16_t)filename_len;
        simple_archiver_helper_16_bit_be(&u16);
        if (fwrite(&u16, 2, 1, out_f) != 1) {
          return SDAS_FAILED_TO_WRITE;
        }
        simple_archiver_helper_16_bit_be(&u16);
        if (fwrite(file_info_struct->filename, 1, u16 + 1, out_f) !=
            (size_t)u16 + 1) {
          return SDAS_FAILED_TO_WRITE;
        }
      }

      if (fwrite(file_info_struct->bit_flags, 1, 4, out_f) != 4) {
        return SDAS_FAILED_TO_WRITE;
      }

      // UID and GID.

      // Forced UID/GID is already handled by "symlinks_and_files_from_files".

      u32 = file_info_struct->uid;
      if ((state->parsed->flags & 0x400) == 0) {
        uint32_t mapped_uid;
        if (simple_archiver_get_uid_mapping(state->parsed->mappings,
                                            state->parsed->users_infos,
                                            u32,
                                            &mapped_uid,
                                            NULL) == 0) {
          //fprintf(stderr,
          //        "NOTICE: Mapped UID %" PRIu32 " to %" PRIu32 " for %s\n",
          //        u32,
          //        mapped_uid,
          //        file_info_struct->filename);
          u32 = mapped_uid;
        }
      }
      simple_archiver_helper_32_bit_be(&u32);
      if (fwrite(&u32, 4, 1, out_f) != 1) {
        return SDAS_FAILED_TO_WRITE;
      }

      u32 = file_info_struct->gid;
      if ((state->parsed->flags & 0x800) == 0) {
        uint32_t mapped_gid;
        if (simple_archiver_get_gid_mapping(state->parsed->mappings,
                                            state->parsed->users_infos,
                                            u32,
                                            &mapped_gid,
                                            NULL) == 0) {
          //fprintf(stderr,
          //        "NOTICE: Mapped GID %" PRIu32 " to %" PRIu32 " for %s\n",
          //        u32,
          //        mapped_gid,
          //        file_info_struct->filename);
          u32 = mapped_gid;
        }
      }
      simple_archiver_helper_32_bit_be(&u32);
      if (fwrite(&u32, 4, 1, out_f) != 1) {
        return SDAS_FAILED_TO_WRITE;
      }

      uint64_t u64 = file_info_struct->file_size;
      simple_archiver_helper_64_bit_be(&u64);
      if (fwrite(&u64, 8, 1, out_f) != 1) {
        return SDAS_FAILED_TO_WRITE;
      }
    }

    file_node = saved_node;

    if (state->parsed->compressor && state->parsed->decompressor) {
      // Is compressing.

      size_t temp_filename_size = strlen(state->parsed->temp_dir) + 1 + 64;
      __attribute__((cleanup(
          simple_archiver_helper_cleanup_c_string))) char *temp_filename =
          malloc(temp_filename_size);

      __attribute__((cleanup(cleanup_temp_filename_delete))) void **ptrs_array =
          malloc(sizeof(void *) * 2);
      ptrs_array[0] = NULL;
      ptrs_array[1] = NULL;

      __attribute__((cleanup(simple_archiver_helper_cleanup_FILE)))
      FILE *temp_fd = NULL;

      if (state->parsed->temp_dir) {
        size_t idx = 0;
        size_t temp_dir_len = strlen(state->parsed->temp_dir);
        snprintf(temp_filename, temp_filename_size, TEMP_FILENAME_CMP,
                 state->parsed->temp_dir,
                 state->parsed->temp_dir[temp_dir_len - 1] == '/' ? "" : "/",
                 idx);
        do {
          FILE *test_fd = fopen(temp_filename, "rb");
          if (test_fd) {
            // File exists.
            fclose(test_fd);
            snprintf(
                temp_filename, temp_filename_size, TEMP_FILENAME_CMP,
                state->parsed->temp_dir,
                state->parsed->temp_dir[temp_dir_len - 1] == '/' ? "" : "/",
                ++idx);
          } else if (idx > 0xFFFF) {
            return SDAS_INTERNAL_ERROR;
          } else {
            break;
          }
        } while (1);
        temp_fd = fopen(temp_filename, "w+b");
        if (temp_fd) {
          ptrs_array[0] = temp_filename;
        }
      } else {
        temp_fd = tmpfile();
      }

      if (!temp_fd) {
        temp_fd = tmpfile();
        if (!temp_fd) {
          fprintf(stderr,
                  "ERROR: Failed to create a temporary file for archival!\n");
          return SDAS_INTERNAL_ERROR;
        }
      }

      // Handle SIGPIPE.
      is_sig_pipe_occurred = 0;
      signal(SIGPIPE, handle_sig_pipe);

      int pipe_into_cmd[2];
      int pipe_outof_cmd[2];
      __attribute__((cleanup(
          simple_archiver_internal_cleanup_decomp_pid))) pid_t compressor_pid =
          -1;

      if (pipe(pipe_into_cmd) != 0) {
        // Unable to create pipes.
        return SDAS_INTERNAL_ERROR;
      } else if (pipe(pipe_outof_cmd) != 0) {
        // Unable to create second set of pipes.
        close(pipe_into_cmd[0]);
        close(pipe_into_cmd[1]);
        return SDAS_INTERNAL_ERROR;
      } else if (fcntl(pipe_into_cmd[1], F_SETFL, O_NONBLOCK) != 0) {
        // Unable to set non-blocking on into-write-pipe.
        close(pipe_into_cmd[0]);
        close(pipe_into_cmd[1]);
        close(pipe_outof_cmd[0]);
        close(pipe_outof_cmd[1]);
        return SDAS_INTERNAL_ERROR;
      } else if (fcntl(pipe_outof_cmd[0], F_SETFL, O_NONBLOCK) != 0) {
        // Unable to set non-blocking on outof-read-pipe.
        close(pipe_into_cmd[0]);
        close(pipe_into_cmd[1]);
        close(pipe_outof_cmd[0]);
        close(pipe_outof_cmd[1]);
        return SDAS_INTERNAL_ERROR;
      } else if (simple_archiver_de_compress(pipe_into_cmd, pipe_outof_cmd,
                                             state->parsed->compressor,
                                             &compressor_pid) != 0) {
        // Failed to spawn compressor.
        close(pipe_into_cmd[1]);
        close(pipe_outof_cmd[0]);
        fprintf(stderr,
                "WARNING: Failed to start compressor cmd! Invalid cmd?\n");
        return SDAS_INTERNAL_ERROR;
      }

      // Close unnecessary pipe fds on this end of the transfer.
      close(pipe_into_cmd[0]);
      close(pipe_outof_cmd[1]);

      // Set up cleanup so that remaining open pipes in this side is cleaned up.
      __attribute__((cleanup(
          simple_archiver_internal_cleanup_int_fd))) int pipe_outof_read =
          pipe_outof_cmd[0];
      __attribute__((cleanup(
          simple_archiver_internal_cleanup_int_fd))) int pipe_into_write =
          pipe_into_cmd[1];

      int_fast8_t to_temp_finished = 0;
      for (uint64_t file_idx = 0; file_idx < *((uint64_t *)chunk_c_node->data);
           ++file_idx) {
        if (is_sig_int_occurred) {
          return SDAS_SIGINT;
        }
        file_node = file_node->next;
        if (file_node == files_list->tail) {
          return SDAS_INTERNAL_ERROR;
        }
        const SDArchiverInternalFileInfo *file_info_struct = file_node->data;
        fprintf(stderr,
                "  FILE %3" PRIu64 " of %3" PRIu64 ": %s\n",
                file_idx + 1,
                *(uint64_t *)chunk_c_node->data,
                file_info_struct->filename);
        __attribute__((cleanup(simple_archiver_helper_cleanup_FILE))) FILE *fd =
            fopen(file_info_struct->filename, "rb");

        int_fast8_t to_comp_finished = 0;
        char hold_buf[SIMPLE_ARCHIVER_BUFFER_SIZE];
        ssize_t has_hold = -1;
        while (!to_comp_finished) {
          if (is_sig_pipe_occurred) {
            fprintf(stderr, "ERROR: SIGPIPE while compressing!\n");
            return SDAS_INTERNAL_ERROR;
          }
          if (!to_comp_finished) {
            // Write to compressor.
            if (ferror(fd)) {
              fprintf(stderr, "ERROR: Writing to chunk, file read error!\n");
              return SDAS_INTERNAL_ERROR;
            }
            if (has_hold < 0) {
              size_t fread_ret = fread(buf, 1, SIMPLE_ARCHIVER_BUFFER_SIZE, fd);
              if (fread_ret > 0) {
                ssize_t write_ret = write(pipe_into_write, buf, fread_ret);
                if (write_ret < 0) {
                  if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    // Non-blocking write.
                    has_hold = (int)fread_ret;
                    memcpy(hold_buf, buf, fread_ret);
                    nanosleep(&nonblock_sleep, NULL);
                  } else {
                    fprintf(
                        stderr,
                        "ERROR: Writing to compressor, pipe write error!\n");
                    return SDAS_FAILED_TO_WRITE;
                  }
                } else if (write_ret == 0) {
                  fprintf(
                      stderr,
                      "ERROR: Writing to compressor, unable to write bytes!\n");
                  return SDAS_FAILED_TO_WRITE;
                } else if ((size_t)write_ret < fread_ret) {
                  has_hold = (ssize_t)fread_ret - write_ret;
                  memcpy(hold_buf, buf + write_ret, (size_t)has_hold);
                }
              }

              if (feof(fd) && has_hold < 0) {
                to_comp_finished = 1;
              }
            } else {
              ssize_t write_ret =
                  write(pipe_into_write, hold_buf, (size_t)has_hold);
              if (write_ret < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                  // Non-blocking write.
                  nanosleep(&nonblock_sleep, NULL);
                } else {
                  return SDAS_INTERNAL_ERROR;
                }
              } else if (write_ret < has_hold) {
                memcpy(buf, hold_buf + write_ret,
                       (size_t)(has_hold - write_ret));
                memcpy(hold_buf, buf, (size_t)(has_hold - write_ret));
                has_hold = has_hold - write_ret;
              } else if (write_ret != has_hold) {
                return SDAS_INTERNAL_ERROR;
              } else {
                has_hold = -1;
              }
            }
          }

          // Write compressed data to temp file.
          ssize_t read_ret =
              read(pipe_outof_read, buf, SIMPLE_ARCHIVER_BUFFER_SIZE);
          if (read_ret < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
              // Non-blocking read.
              nanosleep(&nonblock_sleep, NULL);
            } else {
              fprintf(stderr,
                      "ERROR: Reading from compressor, pipe read error!\n");
              return SDAS_INTERNAL_ERROR;
            }
          } else if (read_ret == 0) {
            // EOF.
            to_temp_finished = 1;
          } else {
            size_t fwrite_ret = fwrite(buf, 1, (size_t)read_ret, temp_fd);
            if (fwrite_ret != (size_t)read_ret) {
              fprintf(stderr,
                      "ERROR: Reading from compressor, failed to write to "
                      "temporary file!\n");
              return SDAS_INTERNAL_ERROR;
            }
          }
        }
      }

      simple_archiver_internal_cleanup_int_fd(&pipe_into_write);

      // Finish writing.
      if (!to_temp_finished) {
        while (1) {
          if (is_sig_pipe_occurred) {
            fprintf(stderr, "ERROR: SIGPIPE while compressing!\n");
            return SDAS_INTERNAL_ERROR;
          }
          ssize_t read_ret =
              read(pipe_outof_read, buf, SIMPLE_ARCHIVER_BUFFER_SIZE);
          if (read_ret < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
              // Non-blocking read.
              nanosleep(&nonblock_sleep, NULL);
            } else {
              fprintf(stderr,
                      "ERROR: Reading from compressor, pipe read error!\n");
              return SDAS_INTERNAL_ERROR;
            }
          } else if (read_ret == 0) {
            // EOF.
            break;
          } else {
            size_t fwrite_ret = fwrite(buf, 1, (size_t)read_ret, temp_fd);
            if (fwrite_ret != (size_t)read_ret) {
              fprintf(stderr,
                      "ERROR: Reading from compressor, failed to write to "
                      "temporary file!\n");
              return SDAS_INTERNAL_ERROR;
            }
          }
        }
      }

      long comp_chunk_size = ftell(temp_fd);
      if (comp_chunk_size < 0) {
        fprintf(stderr,
                "ERROR: Temp file reported negative size after compression!\n");
        return SDAS_INTERNAL_ERROR;
      }

      // Write compressed chunk size.
      uint64_t u64 = (uint64_t)comp_chunk_size;
      simple_archiver_helper_64_bit_be(&u64);
      if (fwrite(&u64, 8, 1, out_f) != 1) {
        return SDAS_FAILED_TO_WRITE;
      }

      if (fseek(temp_fd, 0, SEEK_SET) != 0) {
        return SDAS_INTERNAL_ERROR;
      }

      size_t written_size = 0;

      // Write compressed chunk.
      while (!feof(temp_fd)) {
        if (is_sig_int_occurred) {
          return SDAS_SIGINT;
        } else if (ferror(temp_fd)) {
          return SDAS_INTERNAL_ERROR;
        }
        size_t fread_ret = fread(buf, 1, SIMPLE_ARCHIVER_BUFFER_SIZE, temp_fd);
        if (fread_ret > 0) {
          size_t fwrite_ret = fwrite(buf, 1, fread_ret, out_f);
          written_size += fread_ret;
          if (fwrite_ret != fread_ret) {
            fprintf(stderr,
                    "ERROR: Partial write of read bytes from temp file to "
                    "output file!\n");
            return SDAS_FAILED_TO_WRITE;
          }
        }
      }

      if (written_size != (size_t)comp_chunk_size) {
        fprintf(stderr,
                "ERROR: Written chunk size is not actual chunk size!\n");
        return SDAS_FAILED_TO_WRITE;
      }

      // Cleanup and remove temp_fd.
      simple_archiver_helper_cleanup_FILE(&temp_fd);
    } else {
      // Is NOT compressing.
      if (!non_c_chunk_size) {
        return SDAS_INTERNAL_ERROR;
      }
      simple_archiver_helper_64_bit_be(non_c_chunk_size);
      fwrite(non_c_chunk_size, 8, 1, out_f);
      for (uint64_t file_idx = 0; file_idx < *((uint64_t *)chunk_c_node->data);
           ++file_idx) {
        if (is_sig_int_occurred) {
          return SDAS_SIGINT;
        }
        file_node = file_node->next;
        if (file_node == files_list->tail) {
          return SDAS_INTERNAL_ERROR;
        }
        const SDArchiverInternalFileInfo *file_info_struct = file_node->data;
        fprintf(stderr,
                "  FILE %3" PRIu64 " of %3" PRIu64 ": %s\n",
                file_idx + 1,
                *(uint64_t *)chunk_c_node->data,
                file_info_struct->filename);
        __attribute__((cleanup(simple_archiver_helper_cleanup_FILE))) FILE *fd =
            fopen(file_info_struct->filename, "rb");
        while (!feof(fd)) {
          if (ferror(fd)) {
            fprintf(stderr, "ERROR: Writing to chunk, file read error!\n");
            return SDAS_INTERNAL_ERROR;
          }
          size_t fread_ret = fread(buf, 1, SIMPLE_ARCHIVER_BUFFER_SIZE, fd);
          if (fread_ret > 0) {
            size_t fwrite_ret = fwrite(buf, 1, fread_ret, out_f);
            if (fwrite_ret != fread_ret) {
              fprintf(stderr, "ERROR: Writing to chunk, file write error!\n");
              return SDAS_FAILED_TO_WRITE;
            }
          }
        }
      }
    }
  }

  return SDAS_SUCCESS;
}

int simple_archiver_write_v2(FILE *out_f, SDArchiverState *state,
                             const SDArchiverLinkedList *filenames) {
  fprintf(stderr, "Writing archive of file format 2\n");
  // Because of some differences between version 1 and version 2, version 1's
  // write function cannot be called directly, so there will be some duplicate
  // code between that function and this one.

  // First create a "set" of absolute paths to given filenames.
  __attribute__((cleanup(simple_archiver_hash_map_free)))
  SDArchiverHashMap *abs_filenames = simple_archiver_hash_map_init();
  void **ptr_array = malloc(sizeof(void *) * 2);
  ptr_array[0] = abs_filenames;
  ptr_array[1] = (void *)state->parsed->user_cwd;
  if (simple_archiver_list_get(filenames, filenames_to_abs_map_fn, ptr_array)) {
    free(ptr_array);
    return SDAS_FAILED_TO_CREATE_MAP;
  }
  free(ptr_array);

  // Get a list of symlinks and a list of files.
  __attribute__((cleanup(simple_archiver_list_free)))
  SDArchiverLinkedList *symlinks_list = simple_archiver_list_init();
  __attribute__((cleanup(simple_archiver_list_free)))
  SDArchiverLinkedList *files_list = simple_archiver_list_init();
  __attribute__((cleanup(simple_archiver_list_free)))
  SDArchiverLinkedList *dirs_list = simple_archiver_list_init();
  __attribute__((cleanup(simple_archiver_priority_heap_free)))
  SDArchiverPHeap *files_pheap =
      (state->parsed->flags & 0x40)
          ? simple_archiver_priority_heap_init_less_fn(greater_fn)
          : NULL;
  uint64_t from_files_count = 0;

  ptr_array = malloc(sizeof(void *) * 7);
  ptr_array[0] = symlinks_list;
  ptr_array[1] = files_list;
  ptr_array[2] = (void *)state->parsed->user_cwd;
  ptr_array[3] = files_pheap;
  ptr_array[4] = dirs_list;
  ptr_array[5] = state;
  ptr_array[6] = &from_files_count;

  if (simple_archiver_list_get(filenames, symlinks_and_files_from_files,
                               ptr_array)) {
    free(ptr_array);
    return SDAS_INTERNAL_ERROR;
  }
  free(ptr_array);

  if (files_pheap) {
    while (files_pheap->size > 0) {
      simple_archiver_list_add(files_list,
                               simple_archiver_priority_heap_pop(files_pheap),
                               free_internal_file_info);
    }
    simple_archiver_priority_heap_free(&files_pheap);
  }

  if (symlinks_list->count
      + files_list->count
      + dirs_list->count != from_files_count) {
    fprintf(stderr,
            "ERROR: Count mismatch between files and symlinks and files from "
            "parser!\n");
    //fprintf(stderr,
    //        "symlinks_count: %u, files_list_count: %u, dirs_list_count: %u, "
    //        "filenames_count: %u\n",
    //        symlinks_list->count,
    //        files_list->count,
    //        dirs_list->count,
    //        filenames->count);
    return SDAS_INTERNAL_ERROR;
  }

  if (fwrite("SIMPLE_ARCHIVE_VER", 1, 18, out_f) != 18) {
    return SDAS_FAILED_TO_WRITE;
  }

  const size_t prefix_length = state->parsed->prefix
                               ? strlen(state->parsed->prefix)
                               : 0;

  char buf[SIMPLE_ARCHIVER_BUFFER_SIZE];
  uint16_t u16 = 2;

  simple_archiver_helper_16_bit_be(&u16);

  if (fwrite(&u16, 2, 1, out_f) != 1) {
    return SDAS_FAILED_TO_WRITE;
  }

  if (state->parsed->compressor && !state->parsed->decompressor) {
    return SDAS_NO_DECOMPRESSOR;
  } else if (!state->parsed->compressor && state->parsed->decompressor) {
    return SDAS_NO_COMPRESSOR;
  } else if (state->parsed->compressor && state->parsed->decompressor) {
    // 4 bytes flags, using de/compressor.
    memset(buf, 0, 4);
    buf[0] |= 1;
    if (fwrite(buf, 1, 4, out_f) != 4) {
      return SDAS_FAILED_TO_WRITE;
    }

    size_t len = strlen(state->parsed->compressor);
    if (len >= 0xFFFF) {
      fprintf(stderr, "ERROR: Compressor cmd is too long!\n");
      return SDAS_INVALID_PARSED_STATE;
    }

    u16 = (uint16_t)len;
    simple_archiver_helper_16_bit_be(&u16);
    if (fwrite(&u16, 1, 2, out_f) != 2) {
      return SDAS_FAILED_TO_WRITE;
    }
    simple_archiver_helper_16_bit_be(&u16);

    if (fwrite(state->parsed->compressor, 1, u16 + 1, out_f) !=
        (size_t)u16 + 1) {
      return SDAS_FAILED_TO_WRITE;
    }

    len = strlen(state->parsed->decompressor);
    if (len >= 0xFFFF) {
      fprintf(stderr, "ERROR: Decompressor cmd is too long!\n");
      return SDAS_INVALID_PARSED_STATE;
    }

    u16 = (uint16_t)len;
    simple_archiver_helper_16_bit_be(&u16);
    if (fwrite(&u16, 1, 2, out_f) != 2) {
      return SDAS_FAILED_TO_WRITE;
    }
    simple_archiver_helper_16_bit_be(&u16);

    if (fwrite(state->parsed->decompressor, 1, u16 + 1, out_f) !=
        (size_t)u16 + 1) {
      return SDAS_FAILED_TO_WRITE;
    }
  } else {
    // 4 bytes flags, not using de/compressor.
    memset(buf, 0, 4);
    if (fwrite(buf, 1, 4, out_f) != 4) {
      return SDAS_FAILED_TO_WRITE;
    }
  }

  if (symlinks_list->count > 0xFFFFFFFF) {
    fprintf(stderr, "ERROR: Too many symlinks!\n");
    return SDAS_INVALID_PARSED_STATE;
  }

  uint32_t u32 = (uint32_t)symlinks_list->count;
  simple_archiver_helper_32_bit_be(&u32);
  if (fwrite(&u32, 4, 1, out_f) != 1) {
    return SDAS_FAILED_TO_WRITE;
  }
  simple_archiver_helper_32_bit_be(&u32);

  // Change cwd if user specified.
  __attribute__((cleanup(
      simple_archiver_helper_cleanup_chdir_back))) char *original_cwd = NULL;
  if (state->parsed->user_cwd) {
#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
    original_cwd = realpath(".", NULL);
    if (chdir(state->parsed->user_cwd)) {
      return SDAS_INTERNAL_ERROR;
    }
#endif
  }

  {
    const SDArchiverLLNode *node = symlinks_list->head;
    for (u32 = 0;
         u32 < (uint32_t)symlinks_list->count && node != symlinks_list->tail;) {
      node = node->next;
      ++u32;
      memset(buf, 0, 2);

      uint_fast8_t is_invalid = 0;

#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
      __attribute__((cleanup(
          simple_archiver_helper_cleanup_malloced))) void *abs_path = NULL;
      __attribute__((cleanup(
          simple_archiver_helper_cleanup_malloced))) void *rel_path = NULL;
      if ((state->parsed->flags & 0x100) != 0) {
        // Preserve symlink target.
        char *path_buf = malloc(1024);
        ssize_t ret = readlink(node->data, path_buf, 1023);
        if (ret == -1) {
          fprintf(stderr, "WARNING: Failed to get symlink's target!\n");
          free(path_buf);
          is_invalid = 1;
        } else {
          path_buf[ret] = 0;
          if (path_buf[0] == '/') {
            abs_path = path_buf;
            buf[0] |= 1;
          } else {
            rel_path = path_buf;
          }
        }
      } else {
        abs_path = realpath(node->data, NULL);
        // Check if symlink points to thing to be stored into archive.
        if (abs_path) {
          __attribute__((cleanup(
              simple_archiver_helper_cleanup_malloced))) void *link_abs_path =
              simple_archiver_helper_real_path_to_name(node->data);
          if (!link_abs_path) {
            fprintf(stderr, "WARNING: Failed to get absolute path to link!\n");
          } else {
            rel_path = simple_archiver_filenames_to_relative_path(link_abs_path,
                                                                  abs_path);
          }
        }
      }

      if (abs_path && (state->parsed->flags & 0x20) == 0 &&
          (state->parsed->flags & 0x100) == 0 &&
          !simple_archiver_hash_map_get(abs_filenames, abs_path,
                                        strlen(abs_path) + 1)) {
        // Is not a filename being archived.
        buf[1] |= 0x8;
        if ((state->parsed->flags & 0x80) == 0) {
          // Not a "safe link", mark invalid and continue.
          is_invalid = 1;
          fprintf(stderr,
                  "WARNING: \"%s\" points to outside of archived files (or is "
                  "invalid) and \"--no-safe-links\" not specified, will not "
                  "store abs/rel-links to this entry!\n",
                  (const char *)node->data);
        } else {
          // Safe links disabled, set preference to absolute path.
          buf[0] |= 1;
        }
      } else if ((state->parsed->flags & 0x100) != 0 &&
                 (state->parsed->flags & 0x80) == 0 && !is_invalid) {
        __attribute__((cleanup(
            simple_archiver_helper_cleanup_c_string))) char *target_realpath =
            realpath(node->data, NULL);
        if (!target_realpath) {
          fprintf(
              stderr,
              "WARNING: \"%s\" is an invalid symlink and \"--no-safe-links\" "
              "not specified, will skip this symlink!\n",
              (const char *)node->data);
          is_invalid = 1;
        } else if (!simple_archiver_hash_map_get(abs_filenames, target_realpath,
                                                 strlen(target_realpath) + 1)) {
          fprintf(
              stderr,
              "WARNING: \"%s\" points to outside of archived files and "
              "\"--no-safe-links\" not specified, will skip this symlink!\n",
              (const char *)node->data);
          is_invalid = 1;
        }
      }

      if (!abs_path && !rel_path) {
        // No valid paths, mark as invalid.
        fprintf(stderr,
                "WARNING: \"%s\" is an invalid symlink, will not store rel/abs "
                "link paths!\n",
                (const char *)node->data);
        is_invalid = 1;
      }

      // Get symlink stats for permissions.
      struct stat stat_buf;
      memset(&stat_buf, 0, sizeof(struct stat));
      int stat_status =
          fstatat(AT_FDCWD, node->data, &stat_buf, AT_SYMLINK_NOFOLLOW);
      if (stat_status != 0) {
        return SDAS_INTERNAL_ERROR;
      }

      if ((stat_buf.st_mode & S_IRUSR) != 0) {
        buf[0] |= 2;
      }
      if ((stat_buf.st_mode & S_IWUSR) != 0) {
        buf[0] |= 4;
      }
      if ((stat_buf.st_mode & S_IXUSR) != 0) {
        buf[0] |= 8;
      }
      if ((stat_buf.st_mode & S_IRGRP) != 0) {
        buf[0] |= 0x10;
      }
      if ((stat_buf.st_mode & S_IWGRP) != 0) {
        buf[0] |= 0x20;
      }
      if ((stat_buf.st_mode & S_IXGRP) != 0) {
        buf[0] |= 0x40;
      }
      if ((stat_buf.st_mode & S_IROTH) != 0) {
        buf[0] |= (char)0x80;
      }
      if ((stat_buf.st_mode & S_IWOTH) != 0) {
        buf[1] |= 1;
      }
      if ((stat_buf.st_mode & S_IXOTH) != 0) {
        buf[1] |= 2;
      }
#else
      buf[0] = 0xFE;
      buf[1] = 0xB;
#endif

      if (is_invalid) {
        buf[1] |= 4;
      }

      if (fwrite(buf, 1, 2, out_f) != 2) {
        return SDAS_FAILED_TO_WRITE;
      }

      const size_t link_length = strlen(node->data);
      size_t len = link_length;
      if (state->parsed->prefix) {
        len += prefix_length;
      }
      if (len >= 0xFFFF) {
        fprintf(stderr, "ERROR: Link name is too long!\n");
        return SDAS_INVALID_PARSED_STATE;
      }

      u16 = (uint16_t)len;
      simple_archiver_helper_16_bit_be(&u16);
      if (fwrite(&u16, 2, 1, out_f) != 1) {
        return SDAS_FAILED_TO_WRITE;
      }
      simple_archiver_helper_16_bit_be(&u16);
      if (state->parsed->prefix) {
        size_t fwrite_ret = fwrite(state->parsed->prefix,
                                   1,
                                   prefix_length,
                                   out_f);
        fwrite_ret += fwrite(node->data, 1, link_length + 1, out_f);
        if (fwrite_ret != (size_t)u16 + 1) {
          return SDAS_FAILED_TO_WRITE;
        }
      } else if (fwrite(node->data, 1, u16 + 1, out_f) != (size_t)u16 + 1) {
        return SDAS_FAILED_TO_WRITE;
      }

      if (abs_path && (state->parsed->flags & 0x20) == 0 && !is_invalid) {
        if (state->parsed->prefix) {
          __attribute__((cleanup(simple_archiver_helper_cleanup_c_string)))
          char *abs_path_prefixed =
            simple_archiver_helper_insert_prefix_in_link_path(
              state->parsed->prefix, node->data, abs_path);
          if (!abs_path_prefixed) {
            fprintf(stderr,
                    "ERROR: Failed to add prefix to abs symlink!\n");
            return SDAS_INTERNAL_ERROR;
          }
          const size_t abs_path_pref_length = strlen(abs_path_prefixed);
          if (abs_path_pref_length >= 0xFFFF) {
            fprintf(stderr,
                    "ERROR: Symlink destination absolute path with prefix is "
                    "too long!\n");
            return SDAS_INVALID_PARSED_STATE;
          }

          u16 = (uint16_t)abs_path_pref_length;
          simple_archiver_helper_16_bit_be(&u16);
          if (fwrite(&u16, 2, 1, out_f) != 1) {
            return SDAS_FAILED_TO_WRITE;
          }
          simple_archiver_helper_16_bit_be(&u16);
          if (fwrite(abs_path_prefixed, 1, u16 + 1, out_f) != (size_t)u16 + 1) {
            return SDAS_FAILED_TO_WRITE;
          }
        } else {
          const size_t abs_path_length = strlen(abs_path);
          if (abs_path_length >= 0xFFFF) {
            fprintf(stderr,
                    "ERROR: Symlink destination absolute path is too long!\n");
            return SDAS_INVALID_PARSED_STATE;
          }

          u16 = (uint16_t)abs_path_length;
          simple_archiver_helper_16_bit_be(&u16);
          if (fwrite(&u16, 2, 1, out_f) != 1) {
            return SDAS_FAILED_TO_WRITE;
          }
          simple_archiver_helper_16_bit_be(&u16);
          if (fwrite(abs_path, 1, u16 + 1, out_f) != (size_t)u16 + 1) {
            return SDAS_FAILED_TO_WRITE;
          }
        }
      } else {
        u16 = 0;
        if (fwrite(&u16, 2, 1, out_f) != 1) {
          return SDAS_FAILED_TO_WRITE;
        }
      }

      if (rel_path && !is_invalid) {
        if (state->parsed->prefix) {
          __attribute__((cleanup(simple_archiver_helper_cleanup_c_string)))
          char *rel_path_prefixed =
            simple_archiver_helper_insert_prefix_in_link_path(
              state->parsed->prefix, node->data, rel_path);
          if (!rel_path_prefixed) {
            fprintf(stderr,
                    "ERROR: Failed to add prefix to relative symlink!\n");
            return SDAS_INTERNAL_ERROR;
          }
          const size_t rel_path_pref_length = strlen(rel_path_prefixed);
          if (rel_path_pref_length >= 0xFFFF) {
            fprintf(stderr,
                    "ERROR: Symlink destination relative path with prefix is "
                    "too long!\n");
            return SDAS_INVALID_PARSED_STATE;
          }

          u16 = (uint16_t)rel_path_pref_length;
          simple_archiver_helper_16_bit_be(&u16);
          if (fwrite(&u16, 2, 1, out_f) != 1) {
            return SDAS_FAILED_TO_WRITE;
          }
          simple_archiver_helper_16_bit_be(&u16);
          if (fwrite(rel_path_prefixed, 1, u16 + 1, out_f) != (size_t)u16 + 1) {
            return SDAS_FAILED_TO_WRITE;
          }
        } else {
          len = strlen(rel_path);
          if (len >= 0xFFFF) {
            fprintf(stderr,
                    "ERROR: Symlink destination relative path is too long!\n");
            return SDAS_INVALID_PARSED_STATE;
          }

          u16 = (uint16_t)len;
          simple_archiver_helper_16_bit_be(&u16);
          if (fwrite(&u16, 2, 1, out_f) != 1) {
            return SDAS_FAILED_TO_WRITE;
          }
          simple_archiver_helper_16_bit_be(&u16);
          if (fwrite(rel_path, 1, u16 + 1, out_f) != (size_t)u16 + 1) {
            return SDAS_FAILED_TO_WRITE;
          }
        }
      } else {
        u16 = 0;
        if (fwrite(&u16, 2, 1, out_f) != 1) {
          return SDAS_FAILED_TO_WRITE;
        }
      }
    }
    if (u32 != (uint32_t)symlinks_list->count) {
      fprintf(stderr,
              "ERROR: Iterated through %" PRIu32 " symlinks out of %zu total!"
                "\n",
              u32, symlinks_list->count);
      return SDAS_INTERNAL_ERROR;
    }
  }

  if (is_sig_int_occurred) {
    return SDAS_SIGINT;
  }

  __attribute__((cleanup(simple_archiver_list_free)))
  SDArchiverLinkedList *chunk_counts = simple_archiver_list_init();

  {
    uint64_t current_size = 0;
    uint64_t current_count = 0;
    void **ptrs = malloc(sizeof(void *) * 4);
    ptrs[0] = (void *)&state->parsed->minimum_chunk_size;
    ptrs[1] = &current_size;
    ptrs[2] = &current_count;
    ptrs[3] = chunk_counts;
    if (simple_archiver_list_get(files_list, files_to_chunk_count, ptrs)) {
      free(ptrs);
      fprintf(stderr, "ERROR: Internal error calculating chunk counts!\n");
      return SDAS_INTERNAL_ERROR;
    }
    free(ptrs);
    if ((chunk_counts->count == 0 || current_size > 0) && current_count > 0) {
      uint64_t *count = malloc(sizeof(uint64_t));
      *count = current_count;
      simple_archiver_list_add(chunk_counts, count, NULL);
    }
  }

  // Verify chunk counts.
  {
    uint64_t count = 0;
    for (SDArchiverLLNode *node = chunk_counts->head->next;
         node != chunk_counts->tail; node = node->next) {
      if (*((uint64_t *)node->data) > 0xFFFFFFFF) {
        fprintf(stderr, "ERROR: file count in chunk is too large!\n");
        return SDAS_INTERNAL_ERROR;
      }
      count += *((uint64_t *)node->data);
      // fprintf(stderr, "DEBUG: chunk count %4llu\n",
      // *((uint64_t*)node->data));
    }
    if (count != files_list->count) {
      fprintf(stderr,
              "ERROR: Internal error calculating chunk counts (invalid number "
              "of files)!\n");
      //fprintf(
      //    stderr,
      //    "count: %u, files_list_count: %u\n",
      //    count,
      //    files_list->count);
      return SDAS_INTERNAL_ERROR;
    }
  }

  // Write number of chunks.
  if (chunk_counts->count > 0xFFFFFFFF) {
    fprintf(stderr, "ERROR: Too many chunks!\n");
    return SDAS_INTERNAL_ERROR;
  }
  u32 = (uint32_t)chunk_counts->count;
  simple_archiver_helper_32_bit_be(&u32);
  if (fwrite(&u32, 4, 1, out_f) != 1) {
    return SDAS_FAILED_TO_WRITE;
  }

  __attribute__((cleanup(simple_archiver_helper_cleanup_malloced))) void
      *non_compressing_chunk_size = NULL;
  if (!state->parsed->compressor || !state->parsed->decompressor) {
    non_compressing_chunk_size = malloc(sizeof(uint64_t));
  }
  uint64_t *non_c_chunk_size = non_compressing_chunk_size;

  SDArchiverLLNode *file_node = files_list->head;
  uint64_t chunk_count = 0;
  for (SDArchiverLLNode *chunk_c_node = chunk_counts->head->next;
       chunk_c_node != chunk_counts->tail; chunk_c_node = chunk_c_node->next) {
    if (is_sig_int_occurred) {
      return SDAS_SIGINT;
    }
    fprintf(stderr,
            "CHUNK %3" PRIu64 " of %3zu\n",
            ++chunk_count,
            chunk_counts->count);
    // Write file count before iterating through files.
    if (non_c_chunk_size) {
      *non_c_chunk_size = 0;
    }

    u32 = (uint32_t)(*((uint64_t *)chunk_c_node->data));
    simple_archiver_helper_32_bit_be(&u32);
    if (fwrite(&u32, 4, 1, out_f) != 1) {
      return SDAS_FAILED_TO_WRITE;
    }
    SDArchiverLLNode *saved_node = file_node;
    for (uint64_t file_idx = 0; file_idx < *((uint64_t *)chunk_c_node->data);
         ++file_idx) {
      file_node = file_node->next;
      if (file_node == files_list->tail) {
        return SDAS_INTERNAL_ERROR;
      }
      const SDArchiverInternalFileInfo *file_info_struct = file_node->data;
      if (non_c_chunk_size) {
        *non_c_chunk_size += file_info_struct->file_size;
      }
      const size_t filename_len = strlen(file_info_struct->filename);
      if (state->parsed->prefix) {
        const size_t total_length = filename_len + prefix_length;
        if (total_length >= 0xFFFF) {
          fprintf(stderr, "ERROR: Filename with prefix is too large!\n");
          return SDAS_INVALID_FILE;
        }
        u16 = (uint16_t)total_length;
        simple_archiver_helper_16_bit_be(&u16);
        if (fwrite(&u16, 2, 1, out_f) != 1) {
          return SDAS_FAILED_TO_WRITE;
        }
        simple_archiver_helper_16_bit_be(&u16);
        if (fwrite(state->parsed->prefix, 1, prefix_length, out_f)
            != prefix_length) {
          return SDAS_FAILED_TO_WRITE;
        } else if (fwrite(file_info_struct->filename,
                          1,
                          filename_len + 1,
                          out_f)
                     != filename_len + 1) {
          return SDAS_FAILED_TO_WRITE;
        }
      } else {
        if (filename_len >= 0xFFFF) {
          fprintf(stderr, "ERROR: Filename is too large!\n");
          return SDAS_INVALID_FILE;
        }
        u16 = (uint16_t)filename_len;
        simple_archiver_helper_16_bit_be(&u16);
        if (fwrite(&u16, 2, 1, out_f) != 1) {
          return SDAS_FAILED_TO_WRITE;
        }
        simple_archiver_helper_16_bit_be(&u16);
        if (fwrite(file_info_struct->filename, 1, u16 + 1, out_f) !=
            (size_t)u16 + 1) {
          return SDAS_FAILED_TO_WRITE;
        }
      }

      if (fwrite(file_info_struct->bit_flags, 1, 4, out_f) != 4) {
        return SDAS_FAILED_TO_WRITE;
      }
      // UID and GID.

      // Forced UID/GID is already handled by "symlinks_and_files_from_files".

      u32 = file_info_struct->uid;
      if ((state->parsed->flags & 0x400) == 0) {
        uint32_t mapped_uid;
        if (simple_archiver_get_uid_mapping(state->parsed->mappings,
                                            state->parsed->users_infos,
                                            u32,
                                            &mapped_uid,
                                            NULL) == 0) {
          //fprintf(stderr,
          //        "NOTICE: Mapped UID %" PRIu32 " to %" PRIu32" for %s\n",
          //        u32,
          //        mapped_uid,
          //        file_info_struct->filename);
          u32 = mapped_uid;
        }
      }
      simple_archiver_helper_32_bit_be(&u32);
      if (fwrite(&u32, 4, 1, out_f) != 1) {
        return SDAS_FAILED_TO_WRITE;
      }
      u32 = file_info_struct->gid;
      if ((state->parsed->flags & 0x800) == 0) {
        uint32_t mapped_gid;
        if (simple_archiver_get_gid_mapping(state->parsed->mappings,
                                            state->parsed->users_infos,
                                            u32,
                                            &mapped_gid,
                                            NULL) == 0) {
          //fprintf(stderr,
          //        "NOTICE: Mapped GID %" PRIu32 " to %" PRIu32 " for %s\n",
          //        u32,
          //        mapped_gid,
          //        file_info_struct->filename);
          u32 = mapped_gid;
        }
      }
      simple_archiver_helper_32_bit_be(&u32);
      if (fwrite(&u32, 4, 1, out_f) != 1) {
        return SDAS_FAILED_TO_WRITE;
      }

      uint64_t u64 = file_info_struct->file_size;
      simple_archiver_helper_64_bit_be(&u64);
      if (fwrite(&u64, 8, 1, out_f) != 1) {
        return SDAS_FAILED_TO_WRITE;
      }
    }

    file_node = saved_node;

    if (state->parsed->compressor && state->parsed->decompressor) {
      // Is compressing.

      size_t temp_filename_size = strlen(state->parsed->temp_dir) + 1 + 64;
      __attribute__((cleanup(
          simple_archiver_helper_cleanup_c_string))) char *temp_filename =
          malloc(temp_filename_size);

      __attribute__((cleanup(cleanup_temp_filename_delete))) void **ptrs_array =
          malloc(sizeof(void *) * 2);
      ptrs_array[0] = NULL;
      ptrs_array[1] = NULL;

      __attribute__((cleanup(simple_archiver_helper_cleanup_FILE)))
      FILE *temp_fd = NULL;

      if (state->parsed->temp_dir) {
        size_t idx = 0;
        size_t temp_dir_len = strlen(state->parsed->temp_dir);
        snprintf(temp_filename, temp_filename_size, TEMP_FILENAME_CMP,
                 state->parsed->temp_dir,
                 state->parsed->temp_dir[temp_dir_len - 1] == '/' ? "" : "/",
                 idx);
        do {
          FILE *test_fd = fopen(temp_filename, "rb");
          if (test_fd) {
            // File exists.
            fclose(test_fd);
            snprintf(
                temp_filename, temp_filename_size, TEMP_FILENAME_CMP,
                state->parsed->temp_dir,
                state->parsed->temp_dir[temp_dir_len - 1] == '/' ? "" : "/",
                ++idx);
          } else if (idx > 0xFFFF) {
            return SDAS_INTERNAL_ERROR;
          } else {
            break;
          }
        } while (1);
        temp_fd = fopen(temp_filename, "w+b");
        if (temp_fd) {
          ptrs_array[0] = temp_filename;
        }
      } else {
        temp_fd = tmpfile();
      }

      if (!temp_fd) {
        temp_fd = tmpfile();
        if (!temp_fd) {
          fprintf(stderr,
                  "ERROR: Failed to create a temporary file for archival!\n");
          return SDAS_INTERNAL_ERROR;
        }
      }

      // Handle SIGPIPE.
      is_sig_pipe_occurred = 0;
      signal(SIGPIPE, handle_sig_pipe);

      int pipe_into_cmd[2];
      int pipe_outof_cmd[2];
      __attribute__((cleanup(
          simple_archiver_internal_cleanup_decomp_pid))) pid_t compressor_pid =
          -1;

      if (pipe(pipe_into_cmd) != 0) {
        // Unable to create pipes.
        return SDAS_INTERNAL_ERROR;
      } else if (pipe(pipe_outof_cmd) != 0) {
        // Unable to create second set of pipes.
        close(pipe_into_cmd[0]);
        close(pipe_into_cmd[1]);
        return SDAS_INTERNAL_ERROR;
      } else if (fcntl(pipe_into_cmd[1], F_SETFL, O_NONBLOCK) != 0) {
        // Unable to set non-blocking on into-write-pipe.
        close(pipe_into_cmd[0]);
        close(pipe_into_cmd[1]);
        close(pipe_outof_cmd[0]);
        close(pipe_outof_cmd[1]);
        return SDAS_INTERNAL_ERROR;
      } else if (fcntl(pipe_outof_cmd[0], F_SETFL, O_NONBLOCK) != 0) {
        // Unable to set non-blocking on outof-read-pipe.
        close(pipe_into_cmd[0]);
        close(pipe_into_cmd[1]);
        close(pipe_outof_cmd[0]);
        close(pipe_outof_cmd[1]);
        return SDAS_INTERNAL_ERROR;
      } else if (simple_archiver_de_compress(pipe_into_cmd, pipe_outof_cmd,
                                             state->parsed->compressor,
                                             &compressor_pid) != 0) {
        // Failed to spawn compressor.
        close(pipe_into_cmd[1]);
        close(pipe_outof_cmd[0]);
        fprintf(stderr,
                "WARNING: Failed to start compressor cmd! Invalid cmd?\n");
        return SDAS_INTERNAL_ERROR;
      }

      // Close unnecessary pipe fds on this end of the transfer.
      close(pipe_into_cmd[0]);
      close(pipe_outof_cmd[1]);

      // Set up cleanup so that remaining open pipes in this side is cleaned up.
      __attribute__((cleanup(
          simple_archiver_internal_cleanup_int_fd))) int pipe_outof_read =
          pipe_outof_cmd[0];
      __attribute__((cleanup(
          simple_archiver_internal_cleanup_int_fd))) int pipe_into_write =
          pipe_into_cmd[1];

      int_fast8_t to_temp_finished = 0;
      for (uint64_t file_idx = 0; file_idx < *((uint64_t *)chunk_c_node->data);
           ++file_idx) {
        if (is_sig_int_occurred) {
          return SDAS_SIGINT;
        }
        file_node = file_node->next;
        if (file_node == files_list->tail) {
          return SDAS_INTERNAL_ERROR;
        }
        const SDArchiverInternalFileInfo *file_info_struct = file_node->data;
        fprintf(stderr,
                "  FILE %3" PRIu64 " of %3" PRIu64 ": %s\n",
                file_idx + 1,
                *(uint64_t *)chunk_c_node->data,
                file_info_struct->filename);
        __attribute__((cleanup(simple_archiver_helper_cleanup_FILE))) FILE *fd =
            fopen(file_info_struct->filename, "rb");

        int_fast8_t to_comp_finished = 0;
        char hold_buf[SIMPLE_ARCHIVER_BUFFER_SIZE];
        ssize_t has_hold = -1;
        while (!to_comp_finished) {
          if (is_sig_pipe_occurred) {
            fprintf(stderr, "ERROR: SIGPIPE while compressing!\n");
            return SDAS_INTERNAL_ERROR;
          }
          if (!to_comp_finished) {
            // Write to compressor.
            if (ferror(fd)) {
              fprintf(stderr, "ERROR: Writing to chunk, file read error!\n");
              return SDAS_INTERNAL_ERROR;
            }
            if (has_hold < 0) {
              size_t fread_ret = fread(buf, 1, SIMPLE_ARCHIVER_BUFFER_SIZE, fd);
              if (fread_ret > 0) {
                ssize_t write_ret = write(pipe_into_write, buf, fread_ret);
                if (write_ret < 0) {
                  if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    // Non-blocking write.
                    has_hold = (int)fread_ret;
                    memcpy(hold_buf, buf, fread_ret);
                    nanosleep(&nonblock_sleep, NULL);
                  } else {
                    fprintf(
                        stderr,
                        "ERROR: Writing to compressor, pipe write error!\n");
                    return SDAS_FAILED_TO_WRITE;
                  }
                } else if (write_ret == 0) {
                  fprintf(
                      stderr,
                      "ERROR: Writing to compressor, unable to write bytes!\n");
                  return SDAS_FAILED_TO_WRITE;
                } else if ((size_t)write_ret < fread_ret) {
                  has_hold = (ssize_t)fread_ret - write_ret;
                  memcpy(hold_buf, buf + write_ret, (size_t)has_hold);
                }
              }

              if (feof(fd) && has_hold < 0) {
                to_comp_finished = 1;
              }
            } else {
              ssize_t write_ret =
                  write(pipe_into_write, hold_buf, (size_t)has_hold);
              if (write_ret < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                  // Non-blocking write.
                  nanosleep(&nonblock_sleep, NULL);
                } else {
                  return SDAS_INTERNAL_ERROR;
                }
              } else if (write_ret < has_hold) {
                memcpy(buf, hold_buf + write_ret,
                       (size_t)(has_hold - write_ret));
                memcpy(hold_buf, buf, (size_t)(has_hold - write_ret));
                has_hold = has_hold - write_ret;
              } else if (write_ret != has_hold) {
                return SDAS_INTERNAL_ERROR;
              } else {
                has_hold = -1;
              }
            }
          }

          // Write compressed data to temp file.
          ssize_t read_ret =
              read(pipe_outof_read, buf, SIMPLE_ARCHIVER_BUFFER_SIZE);
          if (read_ret < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
              // Non-blocking read.
              nanosleep(&nonblock_sleep, NULL);
            } else {
              fprintf(stderr,
                      "ERROR: Reading from compressor, pipe read error!\n");
              return SDAS_INTERNAL_ERROR;
            }
          } else if (read_ret == 0) {
            // EOF.
            to_temp_finished = 1;
          } else {
            size_t fwrite_ret = fwrite(buf, 1, (size_t)read_ret, temp_fd);
            if (fwrite_ret != (size_t)read_ret) {
              fprintf(stderr,
                      "ERROR: Reading from compressor, failed to write to "
                      "temporary file!\n");
              return SDAS_INTERNAL_ERROR;
            }
          }
        }
      }

      simple_archiver_internal_cleanup_int_fd(&pipe_into_write);

      // Finish writing.
      if (!to_temp_finished) {
        while (1) {
          if (is_sig_pipe_occurred) {
            fprintf(stderr, "ERROR: SIGPIPE while compressing!\n");
            return SDAS_INTERNAL_ERROR;
          }
          ssize_t read_ret =
              read(pipe_outof_read, buf, SIMPLE_ARCHIVER_BUFFER_SIZE);
          if (read_ret < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
              // Non-blocking read.
              nanosleep(&nonblock_sleep, NULL);
            } else {
              fprintf(stderr,
                      "ERROR: Reading from compressor, pipe read error!\n");
              return SDAS_INTERNAL_ERROR;
            }
          } else if (read_ret == 0) {
            // EOF.
            break;
          } else {
            size_t fwrite_ret = fwrite(buf, 1, (size_t)read_ret, temp_fd);
            if (fwrite_ret != (size_t)read_ret) {
              fprintf(stderr,
                      "ERROR: Reading from compressor, failed to write to "
                      "temporary file!\n");
              return SDAS_INTERNAL_ERROR;
            }
          }
        }
      }

      long comp_chunk_size = ftell(temp_fd);
      if (comp_chunk_size < 0) {
        fprintf(stderr,
                "ERROR: Temp file reported negative size after compression!\n");
        return SDAS_INTERNAL_ERROR;
      }

      // Write compressed chunk size.
      uint64_t u64 = (uint64_t)comp_chunk_size;
      simple_archiver_helper_64_bit_be(&u64);
      if (fwrite(&u64, 8, 1, out_f) != 1) {
        return SDAS_FAILED_TO_WRITE;
      }

      if (fseek(temp_fd, 0, SEEK_SET) != 0) {
        return SDAS_INTERNAL_ERROR;
      }

      size_t written_size = 0;

      // Write compressed chunk.
      while (!feof(temp_fd)) {
        if (is_sig_int_occurred) {
          return SDAS_SIGINT;
        } else if (ferror(temp_fd)) {
          return SDAS_INTERNAL_ERROR;
        }
        size_t fread_ret = fread(buf, 1, SIMPLE_ARCHIVER_BUFFER_SIZE, temp_fd);
        if (fread_ret > 0) {
          size_t fwrite_ret = fwrite(buf, 1, fread_ret, out_f);
          written_size += fread_ret;
          if (fwrite_ret != fread_ret) {
            fprintf(stderr,
                    "ERROR: Partial write of read bytes from temp file to "
                    "output file!\n");
            return SDAS_FAILED_TO_WRITE;
          }
        }
      }

      if (written_size != (size_t)comp_chunk_size) {
        fprintf(stderr,
                "ERROR: Written chunk size is not actual chunk size!\n");
        return SDAS_FAILED_TO_WRITE;
      }

      // Cleanup and remove temp_fd.
      simple_archiver_helper_cleanup_FILE(&temp_fd);
    } else {
      // Is NOT compressing.
      if (!non_c_chunk_size) {
        return SDAS_INTERNAL_ERROR;
      }
      simple_archiver_helper_64_bit_be(non_c_chunk_size);
      fwrite(non_c_chunk_size, 8, 1, out_f);
      for (uint64_t file_idx = 0; file_idx < *((uint64_t *)chunk_c_node->data);
           ++file_idx) {
        if (is_sig_int_occurred) {
          return SDAS_SIGINT;
        }
        file_node = file_node->next;
        if (file_node == files_list->tail) {
          return SDAS_INTERNAL_ERROR;
        }
        const SDArchiverInternalFileInfo *file_info_struct = file_node->data;
        fprintf(stderr,
                "  FILE %3" PRIu64 " of %3" PRIu64 ": %s\n",
                file_idx + 1,
                *(uint64_t *)chunk_c_node->data,
                file_info_struct->filename);
        __attribute__((cleanup(simple_archiver_helper_cleanup_FILE))) FILE *fd =
            fopen(file_info_struct->filename, "rb");
        while (!feof(fd)) {
          if (ferror(fd)) {
            fprintf(stderr, "ERROR: Writing to chunk, file read error!\n");
            return SDAS_INTERNAL_ERROR;
          }
          size_t fread_ret = fread(buf, 1, SIMPLE_ARCHIVER_BUFFER_SIZE, fd);
          if (fread_ret > 0) {
            size_t fwrite_ret = fwrite(buf, 1, fread_ret, out_f);
            if (fwrite_ret != fread_ret) {
              fprintf(stderr, "ERROR: Writing to chunk, file write error!\n");
              return SDAS_FAILED_TO_WRITE;
            }
          }
        }
      }
    }
  }

  // Write directory entries.
  //fprintf(stderr, "DEBUG: Writing directory entries\n");

  if (dirs_list->count > 0xFFFFFFFF) {
    return SDAS_TOO_MANY_DIRS;
  }

  u32 = (uint32_t)dirs_list->count;
  if (u32 != 0) {
    fprintf(stderr, "Directories:\n");
  }

  simple_archiver_helper_32_bit_be(&u32);

  if (fwrite(&u32, 4, 1, out_f) != 1) {
    return SDAS_FAILED_TO_WRITE;
  }

  void **void_ptrs = malloc(sizeof(void*) * 2);
  void_ptrs[0] = out_f;
  void_ptrs[1] = state;

  if (simple_archiver_list_get(dirs_list,
                               internal_write_dir_entries_v2_v3,
                               void_ptrs)) {
    free(void_ptrs);
    return SDAS_INTERNAL_ERROR;
  }
  free(void_ptrs);

  return SDAS_SUCCESS;
}

int simple_archiver_write_v3(FILE *out_f, SDArchiverState *state,
                             const SDArchiverLinkedList *filenames) {
  fprintf(stderr, "Writing archive of file format 3\n");

  // First create a "set" of absolute paths to given filenames.
  __attribute__((cleanup(simple_archiver_hash_map_free)))
  SDArchiverHashMap *abs_filenames = simple_archiver_hash_map_init();
  void **ptr_array = malloc(sizeof(void *) * 2);
  ptr_array[0] = abs_filenames;
  ptr_array[1] = (void *)state->parsed->user_cwd;
  if (simple_archiver_list_get(filenames, filenames_to_abs_map_fn, ptr_array)) {
    free(ptr_array);
    return SDAS_FAILED_TO_CREATE_MAP;
  }
  free(ptr_array);

  // Get a list of symlinks and a list of files.
  __attribute__((cleanup(simple_archiver_list_free)))
  SDArchiverLinkedList *symlinks_list = simple_archiver_list_init();
  __attribute__((cleanup(simple_archiver_list_free)))
  SDArchiverLinkedList *files_list = simple_archiver_list_init();
  __attribute__((cleanup(simple_archiver_list_free)))
  SDArchiverLinkedList *dirs_list = simple_archiver_list_init();
  __attribute__((cleanup(simple_archiver_priority_heap_free)))
  SDArchiverPHeap *files_pheap =
      (state->parsed->flags & 0x40)
          ? simple_archiver_priority_heap_init_less_fn(greater_fn)
          : NULL;
  uint64_t from_files_count = 0;

  ptr_array = malloc(sizeof(void *) * 7);
  ptr_array[0] = symlinks_list;
  ptr_array[1] = files_list;
  ptr_array[2] = (void *)state->parsed->user_cwd;
  ptr_array[3] = files_pheap;
  ptr_array[4] = dirs_list;
  ptr_array[5] = state;
  ptr_array[6] = &from_files_count;

  if (simple_archiver_list_get(filenames, symlinks_and_files_from_files,
                               ptr_array)) {
    free(ptr_array);
    return SDAS_INTERNAL_ERROR;
  }
  free(ptr_array);

  if (files_pheap) {
    while (files_pheap->size > 0) {
      simple_archiver_list_add(files_list,
                               simple_archiver_priority_heap_pop(files_pheap),
                               free_internal_file_info);
    }
    simple_archiver_priority_heap_free(&files_pheap);
  }

  if (symlinks_list->count
      + files_list->count
      + dirs_list->count != from_files_count) {
    fprintf(stderr,
            "ERROR: Count mismatch between files and symlinks and files from "
            "parser!\n");
    //fprintf(stderr,
    //        "symlinks_count: %u, files_list_count: %u, dirs_list_count: %u, "
    //        "filenames_count: %u\n",
    //        symlinks_list->count,
    //        files_list->count,
    //        dirs_list->count,
    //        filenames->count);
    return SDAS_INTERNAL_ERROR;
  }

  if (fwrite("SIMPLE_ARCHIVE_VER", 1, 18, out_f) != 18) {
    return SDAS_FAILED_TO_WRITE;
  }

  char buf[SIMPLE_ARCHIVER_BUFFER_SIZE];
  uint16_t u16 = 3;

  simple_archiver_helper_16_bit_be(&u16);

  if (fwrite(&u16, 2, 1, out_f) != 1) {
    return SDAS_FAILED_TO_WRITE;
  }

  const size_t prefix_length = state->parsed->prefix
                               ? strlen(state->parsed->prefix)
                               : 0;

  if (state->parsed->compressor && !state->parsed->decompressor) {
    return SDAS_NO_DECOMPRESSOR;
  } else if (!state->parsed->compressor && state->parsed->decompressor) {
    return SDAS_NO_COMPRESSOR;
  } else if (state->parsed->compressor && state->parsed->decompressor) {
    // 4 bytes flags, using de/compressor.
    memset(buf, 0, 4);
    buf[0] |= 1;
    if (fwrite(buf, 1, 4, out_f) != 4) {
      return SDAS_FAILED_TO_WRITE;
    }

    size_t len = strlen(state->parsed->compressor);
    if (len >= 0xFFFF) {
      fprintf(stderr, "ERROR: Compressor cmd is too long!\n");
      return SDAS_INVALID_PARSED_STATE;
    }

    u16 = (uint16_t)len;
    simple_archiver_helper_16_bit_be(&u16);
    if (fwrite(&u16, 1, 2, out_f) != 2) {
      return SDAS_FAILED_TO_WRITE;
    }
    simple_archiver_helper_16_bit_be(&u16);

    if (fwrite(state->parsed->compressor, 1, u16 + 1, out_f) !=
        (size_t)u16 + 1) {
      return SDAS_FAILED_TO_WRITE;
    }

    len = strlen(state->parsed->decompressor);
    if (len >= 0xFFFF) {
      fprintf(stderr, "ERROR: Decompressor cmd is too long!\n");
      return SDAS_INVALID_PARSED_STATE;
    }

    u16 = (uint16_t)len;
    simple_archiver_helper_16_bit_be(&u16);
    if (fwrite(&u16, 1, 2, out_f) != 2) {
      return SDAS_FAILED_TO_WRITE;
    }
    simple_archiver_helper_16_bit_be(&u16);

    if (fwrite(state->parsed->decompressor, 1, u16 + 1, out_f) !=
        (size_t)u16 + 1) {
      return SDAS_FAILED_TO_WRITE;
    }
  } else {
    // 4 bytes flags, not using de/compressor.
    memset(buf, 0, 4);
    if (fwrite(buf, 1, 4, out_f) != 4) {
      return SDAS_FAILED_TO_WRITE;
    }
  }

  if (symlinks_list->count > 0xFFFFFFFF) {
    fprintf(stderr, "ERROR: Too many symlinks!\n");
    return SDAS_INVALID_PARSED_STATE;
  }

  uint32_t u32 = (uint32_t)symlinks_list->count;
  simple_archiver_helper_32_bit_be(&u32);
  if (fwrite(&u32, 4, 1, out_f) != 1) {
    return SDAS_FAILED_TO_WRITE;
  }
  simple_archiver_helper_32_bit_be(&u32);

  // Change cwd if user specified.
  __attribute__((cleanup(
      simple_archiver_helper_cleanup_chdir_back))) char *original_cwd = NULL;
  if (state->parsed->user_cwd) {
#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
    original_cwd = realpath(".", NULL);
    if (chdir(state->parsed->user_cwd)) {
      return SDAS_INTERNAL_ERROR;
    }
#endif
  }

  {
    const SDArchiverLLNode *node = symlinks_list->head;
    uint32_t idx;
    for (idx = 0;
         idx < (uint32_t)symlinks_list->count && node != symlinks_list->tail;) {
      node = node->next;
      ++idx;
      memset(buf, 0, 2);

      uint_fast8_t is_invalid = 0;

#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
      __attribute__((cleanup(
          simple_archiver_helper_cleanup_malloced))) void *abs_path = NULL;
      __attribute__((cleanup(
          simple_archiver_helper_cleanup_malloced))) void *rel_path = NULL;
      if ((state->parsed->flags & 0x100) != 0) {
        // Preserve symlink target.
        char *path_buf = malloc(1024);
        ssize_t ret = readlink(node->data, path_buf, 1023);
        if (ret == -1) {
          fprintf(stderr, "WARNING: Failed to get symlink's target!\n");
          free(path_buf);
          is_invalid = 1;
        } else {
          path_buf[ret] = 0;
          if (path_buf[0] == '/') {
            abs_path = path_buf;
            buf[0] |= 1;
          } else {
            rel_path = path_buf;
          }
        }
      } else {
        abs_path = realpath(node->data, NULL);
        // Check if symlink points to thing to be stored into archive.
        if (abs_path) {
          __attribute__((cleanup(
              simple_archiver_helper_cleanup_malloced))) void *link_abs_path =
              simple_archiver_helper_real_path_to_name(node->data);
          if (!link_abs_path) {
            fprintf(stderr, "WARNING: Failed to get absolute path to link!\n");
          } else {
            rel_path = simple_archiver_filenames_to_relative_path(link_abs_path,
                                                                  abs_path);
          }
        }
      }

      if (abs_path && (state->parsed->flags & 0x20) == 0 &&
          (state->parsed->flags & 0x100) == 0 &&
          !simple_archiver_hash_map_get(abs_filenames, abs_path,
                                        strlen(abs_path) + 1)) {
        // Is not a filename being archived.
        buf[1] |= 8;
        if ((state->parsed->flags & 0x80) == 0) {
          // Not a "safe link", mark invalid and continue.
          is_invalid = 1;
          fprintf(stderr,
                  "WARNING: \"%s\" points to outside of archived files (or is "
                  "invalid) and \"--no-safe-links\" not specified, will not "
                  "store abs/rel-links to this entry!\n",
                  (const char *)node->data);
        } else {
          // Safe links disabled, set preference to absolute path.
          buf[0] |= 1;
        }
      } else if ((state->parsed->flags & 0x100) != 0 &&
                 (state->parsed->flags & 0x80) == 0 && !is_invalid) {
        __attribute__((cleanup(
            simple_archiver_helper_cleanup_c_string))) char *target_realpath =
            realpath(node->data, NULL);
        if (!target_realpath) {
          fprintf(
              stderr,
              "WARNING: \"%s\" is an invalid symlink and \"--no-safe-links\" "
              "not specified, will skip this symlink!\n",
              (const char *)node->data);
          is_invalid = 1;
        } else if (!simple_archiver_hash_map_get(abs_filenames, target_realpath,
                                                 strlen(target_realpath) + 1)) {
          fprintf(
              stderr,
              "WARNING: \"%s\" points to outside of archived files and "
              "\"--no-safe-links\" not specified, will skip this symlink!\n",
              (const char *)node->data);
          is_invalid = 1;
        }
      }

      if (!abs_path && !rel_path) {
        // No valid paths, mark as invalid.
        fprintf(stderr,
                "WARNING: \"%s\" is an invalid symlink, will not store rel/abs "
                "link paths!\n",
                (const char *)node->data);
        is_invalid = 1;
      }

      // Get symlink stats for permissions.
      struct stat stat_buf;
      memset(&stat_buf, 0, sizeof(struct stat));
      int stat_status =
          fstatat(AT_FDCWD, node->data, &stat_buf, AT_SYMLINK_NOFOLLOW);
      if (stat_status != 0) {
        return SDAS_INTERNAL_ERROR;
      }

      if ((stat_buf.st_mode & S_IRUSR) != 0) {
        buf[0] |= 2;
      }
      if ((stat_buf.st_mode & S_IWUSR) != 0) {
        buf[0] |= 4;
      }
      if ((stat_buf.st_mode & S_IXUSR) != 0) {
        buf[0] |= 8;
      }
      if ((stat_buf.st_mode & S_IRGRP) != 0) {
        buf[0] |= 0x10;
      }
      if ((stat_buf.st_mode & S_IWGRP) != 0) {
        buf[0] |= 0x20;
      }
      if ((stat_buf.st_mode & S_IXGRP) != 0) {
        buf[0] |= 0x40;
      }
      if ((stat_buf.st_mode & S_IROTH) != 0) {
        buf[0] |= (char)0x80;
      }
      if ((stat_buf.st_mode & S_IWOTH) != 0) {
        buf[1] |= 1;
      }
      if ((stat_buf.st_mode & S_IXOTH) != 0) {
        buf[1] |= 2;
      }
#else
      buf[0] = 0xFE;
      buf[1] = 0xB;
#endif

      if (is_invalid) {
        buf[1] |= 4;
      }

      if (fwrite(buf, 1, 2, out_f) != 2) {
        return SDAS_FAILED_TO_WRITE;
      }

      const size_t link_length = strlen(node->data);
      size_t len = link_length;
      if (state->parsed->prefix) {
        len += prefix_length;
      }
      if (len >= 0xFFFF) {
        fprintf(stderr, "ERROR: Link name is too long!\n");
        return SDAS_INVALID_PARSED_STATE;
      }

      u16 = (uint16_t)len;
      simple_archiver_helper_16_bit_be(&u16);
      if (fwrite(&u16, 2, 1, out_f) != 1) {
        return SDAS_FAILED_TO_WRITE;
      }
      simple_archiver_helper_16_bit_be(&u16);
      if (state->parsed->prefix) {
        size_t fwrite_ret = fwrite(state->parsed->prefix,
                                   1,
                                   prefix_length,
                                   out_f);
        fwrite_ret += fwrite(node->data, 1, link_length + 1, out_f);
        if (fwrite_ret != (size_t)u16 + 1) {
          return SDAS_FAILED_TO_WRITE;
        }
      } else if (fwrite(node->data, 1, u16 + 1, out_f) != (size_t)u16 + 1) {
        return SDAS_FAILED_TO_WRITE;
      }

      if (abs_path && (state->parsed->flags & 0x20) == 0 && !is_invalid) {
        if (state->parsed->prefix) {
          __attribute__((cleanup(simple_archiver_helper_cleanup_c_string)))
          char *abs_path_prefixed =
            simple_archiver_helper_insert_prefix_in_link_path(
              state->parsed->prefix, node->data, abs_path);
          if (!abs_path_prefixed) {
            fprintf(stderr,
                    "ERROR: Failed to add prefix to abs symlink!\n");
            return SDAS_INTERNAL_ERROR;
          }
          const size_t abs_path_pref_length = strlen(abs_path_prefixed);
          if (abs_path_pref_length >= 0xFFFF) {
            fprintf(stderr,
                    "ERROR: Symlink destination absolute path with prefix is "
                    "too long!\n");
            return SDAS_INVALID_PARSED_STATE;
          }

          u16 = (uint16_t)abs_path_pref_length;
          simple_archiver_helper_16_bit_be(&u16);
          if (fwrite(&u16, 2, 1, out_f) != 1) {
            return SDAS_FAILED_TO_WRITE;
          }
          simple_archiver_helper_16_bit_be(&u16);
          if (fwrite(abs_path_prefixed, 1, u16 + 1, out_f) != (size_t)u16 + 1) {
            return SDAS_FAILED_TO_WRITE;
          }
        } else {
          const size_t abs_path_length = strlen(abs_path);
          if (abs_path_length >= 0xFFFF) {
            fprintf(stderr,
                    "ERROR: Symlink destination absolute path is too long!\n");
            return SDAS_INVALID_PARSED_STATE;
          }

          u16 = (uint16_t)abs_path_length;
          simple_archiver_helper_16_bit_be(&u16);
          if (fwrite(&u16, 2, 1, out_f) != 1) {
            return SDAS_FAILED_TO_WRITE;
          }
          simple_archiver_helper_16_bit_be(&u16);
          if (fwrite(abs_path, 1, u16 + 1, out_f) != (size_t)u16 + 1) {
            return SDAS_FAILED_TO_WRITE;
          }
        }
      } else {
        u16 = 0;
        if (fwrite(&u16, 2, 1, out_f) != 1) {
          return SDAS_FAILED_TO_WRITE;
        }
      }

      if (rel_path && !is_invalid) {
        if (state->parsed->prefix) {
          __attribute__((cleanup(simple_archiver_helper_cleanup_c_string)))
          char *rel_path_prefixed =
            simple_archiver_helper_insert_prefix_in_link_path(
              state->parsed->prefix, node->data, rel_path);
          if (!rel_path_prefixed) {
            fprintf(stderr,
                    "ERROR: Failed to add prefix to relative symlink!\n");
            return SDAS_INTERNAL_ERROR;
          }
          const size_t rel_path_pref_length = strlen(rel_path_prefixed);
          if (rel_path_pref_length >= 0xFFFF) {
            fprintf(stderr,
                    "ERROR: Symlink destination relative path with prefix is "
                    "too long!\n");
            return SDAS_INVALID_PARSED_STATE;
          }

          u16 = (uint16_t)rel_path_pref_length;
          simple_archiver_helper_16_bit_be(&u16);
          if (fwrite(&u16, 2, 1, out_f) != 1) {
            return SDAS_FAILED_TO_WRITE;
          }
          simple_archiver_helper_16_bit_be(&u16);
          if (fwrite(rel_path_prefixed, 1, u16 + 1, out_f) != (size_t)u16 + 1) {
            return SDAS_FAILED_TO_WRITE;
          }
        } else {
          len = strlen(rel_path);
          if (len >= 0xFFFF) {
            fprintf(stderr,
                    "ERROR: Symlink destination relative path is too long!\n");
            return SDAS_INVALID_PARSED_STATE;
          }

          u16 = (uint16_t)len;
          simple_archiver_helper_16_bit_be(&u16);
          if (fwrite(&u16, 2, 1, out_f) != 1) {
            return SDAS_FAILED_TO_WRITE;
          }
          simple_archiver_helper_16_bit_be(&u16);
          if (fwrite(rel_path, 1, u16 + 1, out_f) != (size_t)u16 + 1) {
            return SDAS_FAILED_TO_WRITE;
          }
        }
      } else {
        u16 = 0;
        if (fwrite(&u16, 2, 1, out_f) != 1) {
          return SDAS_FAILED_TO_WRITE;
        }
      }

      u32 = stat_buf.st_uid;
      if (state->parsed->flags & 0x400) {
        u32 = state->parsed->uid;
      } else {
        uint32_t mapped_uid;
        if (simple_archiver_get_uid_mapping(state->parsed->mappings,
                                            state->parsed->users_infos,
                                            u32,
                                            &mapped_uid,
                                            NULL) == 0) {
          //fprintf(stderr,
          //        "NOTICE: Mapped UID %" PRIu32 " to %" PRIu32" for %s\n",
          //        u32,
          //        mapped_uid,
          //        (const char *)node->data);
          u32 = mapped_uid;
        }
      }
      simple_archiver_helper_32_bit_be(&u32);
      if (fwrite(&u32, 4, 1, out_f) != 1) {
        return SDAS_FAILED_TO_WRITE;
      }

      u32 = stat_buf.st_gid;
      if (state->parsed->flags & 0x800) {
        u32 = state->parsed->gid;
      } else {
        uint32_t mapped_gid;
        if (simple_archiver_get_gid_mapping(state->parsed->mappings,
                                            state->parsed->users_infos,
                                            u32,
                                            &mapped_gid,
                                            NULL) == 0) {
          //fprintf(stderr,
          //        "NOTICE: Mapped GID %" PRIu32 " to %" PRIu32 " for %s\n",
          //        u32,
          //        mapped_gid,
          //        (const char *)node->data);
          u32 = mapped_gid;
        }
      }
      simple_archiver_helper_32_bit_be(&u32);
      if (fwrite(&u32, 4, 1, out_f) != 1) {
        return SDAS_FAILED_TO_WRITE;
      }

      u32 = stat_buf.st_uid;
      if (state->parsed->flags & 0x400) {
        u32 = state->parsed->uid;
      }
      __attribute__((cleanup(simple_archiver_helper_cleanup_c_string)))
      char *to_cleanup_user = NULL;
      const char *username = simple_archiver_hash_map_get(
        state->parsed->users_infos.UidToUname,
        &u32,
        sizeof(uint32_t));
      if (username) {
        if ((state->parsed->flags & 0x400) == 0) {
          uint32_t out_uid;
          const char *mapped_user = NULL;
          if (simple_archiver_get_user_mapping(state->parsed->mappings,
                                               state->parsed->users_infos,
                                               username,
                                               &out_uid,
                                               &mapped_user) == 0
              && mapped_user) {
            //fprintf(stderr,
            //        "NOTICE: Mapped User %s to %s for %s\n",
            //        username,
            //        mapped_user,
            //        (const char *)node->data);
            username = mapped_user;
            to_cleanup_user = (char *)mapped_user;
          }
        }
        unsigned long name_length = strlen(username);
        if (name_length > 0xFFFF) {
          return SDAS_INTERNAL_ERROR;
        }
        u16 = (uint16_t)name_length;
        simple_archiver_helper_16_bit_be(&u16);
        if (fwrite(&u16, 2, 1, out_f) != 1) {
          return SDAS_FAILED_TO_WRITE;
        }
        simple_archiver_helper_16_bit_be(&u16);
        if (fwrite(username, 1, u16 + 1, out_f) != (size_t)u16 + 1) {
          return SDAS_FAILED_TO_WRITE;
        }
      } else {
        u16 = 0;
        if (fwrite(&u16, 2, 1, out_f) != 1) {
          return SDAS_FAILED_TO_WRITE;
        }
      }

      u32 = stat_buf.st_gid;
      if (state->parsed->flags & 0x800) {
        u32 = state->parsed->gid;
      }
      __attribute__((cleanup(simple_archiver_helper_cleanup_c_string)))
      char *to_cleanup_group = NULL;
      const char *groupname = simple_archiver_hash_map_get(
        state->parsed->users_infos.GidToGname,
        &u32,
        sizeof(uint32_t));
      if (groupname) {
        if ((state->parsed->flags & 0x800) == 0) {
          uint32_t out_gid;
          const char *mapped_group = NULL;
          if (simple_archiver_get_group_mapping(state->parsed->mappings,
                                                state->parsed->users_infos,
                                                groupname,
                                                &out_gid,
                                                &mapped_group) == 0
              && mapped_group) {
            //fprintf(stderr,
            //        "NOTICE: Mapped Group %s to %s for %s\n",
            //        groupname,
            //        mapped_group,
            //        (const char *)node->data);
            groupname = mapped_group;
            to_cleanup_group = (char *)mapped_group;
          }
        }
        unsigned long group_length = strlen(groupname);
        if (group_length > 0xFFFF) {
          return SDAS_INTERNAL_ERROR;
        }
        u16 = (uint16_t)group_length;
        simple_archiver_helper_16_bit_be(&u16);
        if (fwrite(&u16, 2, 1, out_f) != 1) {
          return SDAS_FAILED_TO_WRITE;
        }
        simple_archiver_helper_16_bit_be(&u16);
        if (fwrite(groupname, 1, u16 + 1, out_f) != (size_t)u16 + 1) {
          return SDAS_FAILED_TO_WRITE;
        }
      } else {
        u16 = 0;
        if (fwrite(&u16, 2, 1, out_f) != 1) {
          return SDAS_FAILED_TO_WRITE;
        }
      }
    }
    if (idx != (uint32_t)symlinks_list->count) {
      fprintf(stderr,
              "ERROR: Iterated through %" PRIu32 " symlinks out of %zu total!"
                "\n",
              idx, symlinks_list->count);
      return SDAS_INTERNAL_ERROR;
    }
  }

  if (is_sig_int_occurred) {
    return SDAS_SIGINT;
  }

  __attribute__((cleanup(simple_archiver_list_free)))
  SDArchiverLinkedList *chunk_counts = simple_archiver_list_init();

  {
    uint64_t current_size = 0;
    uint64_t current_count = 0;
    void **ptrs = malloc(sizeof(void *) * 4);
    ptrs[0] = (void *)&state->parsed->minimum_chunk_size;
    ptrs[1] = &current_size;
    ptrs[2] = &current_count;
    ptrs[3] = chunk_counts;
    if (simple_archiver_list_get(files_list, files_to_chunk_count, ptrs)) {
      free(ptrs);
      fprintf(stderr, "ERROR: Internal error calculating chunk counts!\n");
      return SDAS_INTERNAL_ERROR;
    }
    free(ptrs);
    if ((chunk_counts->count == 0 || current_size > 0) && current_count > 0) {
      uint64_t *count = malloc(sizeof(uint64_t));
      *count = current_count;
      simple_archiver_list_add(chunk_counts, count, NULL);
    }
  }

  // Verify chunk counts.
  {
    uint64_t count = 0;
    for (SDArchiverLLNode *node = chunk_counts->head->next;
         node != chunk_counts->tail; node = node->next) {
      if (*((uint64_t *)node->data) > 0xFFFFFFFF) {
        fprintf(stderr, "ERROR: file count in chunk is too large!\n");
        return SDAS_INTERNAL_ERROR;
      }
      count += *((uint64_t *)node->data);
      // fprintf(stderr, "DEBUG: chunk count %4llu\n",
      // *((uint64_t*)node->data));
    }
    if (count != files_list->count) {
      fprintf(stderr,
              "ERROR: Internal error calculating chunk counts (invalid number "
              "of files)!\n");
      //fprintf(
      //    stderr,
      //    "count: %u, files_list_count: %u\n",
      //    count,
      //    files_list->count);
      return SDAS_INTERNAL_ERROR;
    }
  }

  // Write number of chunks.
  if (chunk_counts->count > 0xFFFFFFFF) {
    fprintf(stderr, "ERROR: Too many chunks!\n");
    return SDAS_INTERNAL_ERROR;
  }
  u32 = (uint32_t)chunk_counts->count;
  simple_archiver_helper_32_bit_be(&u32);
  if (fwrite(&u32, 4, 1, out_f) != 1) {
    return SDAS_FAILED_TO_WRITE;
  }

  __attribute__((cleanup(simple_archiver_helper_cleanup_malloced))) void
      *non_compressing_chunk_size = NULL;
  if (!state->parsed->compressor || !state->parsed->decompressor) {
    non_compressing_chunk_size = malloc(sizeof(uint64_t));
  }
  uint64_t *non_c_chunk_size = non_compressing_chunk_size;

  SDArchiverLLNode *file_node = files_list->head;
  uint64_t chunk_count = 0;
  for (SDArchiverLLNode *chunk_c_node = chunk_counts->head->next;
       chunk_c_node != chunk_counts->tail; chunk_c_node = chunk_c_node->next) {
    if (is_sig_int_occurred) {
      return SDAS_SIGINT;
    }
    fprintf(stderr,
            "CHUNK %3" PRIu64 " of %3zu\n",
            ++chunk_count,
            chunk_counts->count);
    // Write file count before iterating through files.
    if (non_c_chunk_size) {
      *non_c_chunk_size = 0;
    }

    u32 = (uint32_t)(*((uint64_t *)chunk_c_node->data));
    simple_archiver_helper_32_bit_be(&u32);
    if (fwrite(&u32, 4, 1, out_f) != 1) {
      return SDAS_FAILED_TO_WRITE;
    }
    SDArchiverLLNode *saved_node = file_node;
    for (uint64_t file_idx = 0; file_idx < *((uint64_t *)chunk_c_node->data);
         ++file_idx) {
      file_node = file_node->next;
      if (file_node == files_list->tail) {
        return SDAS_INTERNAL_ERROR;
      }
      const SDArchiverInternalFileInfo *file_info_struct = file_node->data;
      if (non_c_chunk_size) {
        *non_c_chunk_size += file_info_struct->file_size;
      }
      const size_t filename_len = strlen(file_info_struct->filename);
      if (state->parsed->prefix) {
        const size_t total_length = filename_len + prefix_length;
        if (total_length >= 0xFFFF) {
          fprintf(stderr, "ERROR: Filename with prefix is too large!\n");
          return SDAS_INVALID_FILE;
        }
        u16 = (uint16_t)total_length;
        simple_archiver_helper_16_bit_be(&u16);
        if (fwrite(&u16, 2, 1, out_f) != 1) {
          return SDAS_FAILED_TO_WRITE;
        }
        simple_archiver_helper_16_bit_be(&u16);
        if (fwrite(state->parsed->prefix, 1, prefix_length, out_f)
            != prefix_length) {
          return SDAS_FAILED_TO_WRITE;
        } else if (fwrite(file_info_struct->filename,
                          1,
                          filename_len + 1,
                          out_f)
                     != filename_len + 1) {
          return SDAS_FAILED_TO_WRITE;
        }
      } else {
        if (filename_len >= 0xFFFF) {
          fprintf(stderr, "ERROR: Filename is too large!\n");
          return SDAS_INVALID_FILE;
        }
        u16 = (uint16_t)filename_len;
        simple_archiver_helper_16_bit_be(&u16);
        if (fwrite(&u16, 2, 1, out_f) != 1) {
          return SDAS_FAILED_TO_WRITE;
        }
        simple_archiver_helper_16_bit_be(&u16);
        if (fwrite(file_info_struct->filename, 1, u16 + 1, out_f) !=
            (size_t)u16 + 1) {
          return SDAS_FAILED_TO_WRITE;
        }
      }

      if (fwrite(file_info_struct->bit_flags, 1, 4, out_f) != 4) {
        return SDAS_FAILED_TO_WRITE;
      }
      // UID and GID.

      // Forced UID/GID is already handled by "symlinks_and_files_from_files".

      u32 = file_info_struct->uid;
      if ((state->parsed->flags & 0x400) == 0) {
        uint32_t mapped_uid;
        if (simple_archiver_get_uid_mapping(state->parsed->mappings,
                                            state->parsed->users_infos,
                                            u32,
                                            &mapped_uid,
                                            NULL) == 0) {
          //fprintf(stderr,
          //        "NOTICE: Mapped UID %" PRIu32 " to %" PRIu32" for %s\n",
          //        u32,
          //        mapped_uid,
          //        file_info_struct->filename);
          u32 = mapped_uid;
        }
      }
      simple_archiver_helper_32_bit_be(&u32);
      if (fwrite(&u32, 4, 1, out_f) != 1) {
        return SDAS_FAILED_TO_WRITE;
      }
      u32 = file_info_struct->gid;
      if ((state->parsed->flags & 0x800) == 0) {
        uint32_t mapped_gid;
        if(simple_archiver_get_gid_mapping(state->parsed->mappings,
                                           state->parsed->users_infos,
                                           u32,
                                           &mapped_gid,
                                           NULL) == 0) {
          //fprintf(stderr,
          //        "NOTICE: Mapped GID %" PRIu32 " to %" PRIu32" for %s\n",
          //        u32,
          //        mapped_gid,
          //        file_info_struct->filename);
          u32 = mapped_gid;
        }
      }
      simple_archiver_helper_32_bit_be(&u32);
      if (fwrite(&u32, 4, 1, out_f) != 1) {
        return SDAS_FAILED_TO_WRITE;
      }

      u32 = file_info_struct->uid;
      __attribute__((cleanup(simple_archiver_helper_cleanup_c_string)))
      char *to_cleanup_user = NULL;
      const char *username = simple_archiver_hash_map_get(
        state->parsed->users_infos.UidToUname, &u32, sizeof(uint32_t));
      if (username) {
        if ((state->parsed->flags & 0x400) == 0) {
          uint32_t out_uid;
          const char *mapped_username = NULL;
          if (simple_archiver_get_user_mapping(state->parsed->mappings,
                                               state->parsed->users_infos,
                                               username,
                                               &out_uid,
                                               &mapped_username) == 0
              && mapped_username) {
            //fprintf(stderr,
            //        "NOTICE: Mapped User from %s to %s for %s\n",
            //        username,
            //        mapped_username,
            //        file_info_struct->filename);
            username = mapped_username;
            to_cleanup_user = (char *)mapped_username;
          }
        }
        unsigned long name_length = strlen(username);
        if (name_length > 0xFFFF) {
          return SDAS_INTERNAL_ERROR;
        }
        u16 = (uint16_t)name_length;
        simple_archiver_helper_16_bit_be(&u16);
        if (fwrite(&u16, 2, 1, out_f) != 1) {
          return SDAS_FAILED_TO_WRITE;
        }
        simple_archiver_helper_16_bit_be(&u16);
        if (fwrite(username, 1, u16 + 1, out_f) != (size_t)u16 + 1) {
          return SDAS_FAILED_TO_WRITE;
        }
      } else {
        u16 = 0;
        if (fwrite(&u16, 2, 1, out_f) != 1) {
          return SDAS_FAILED_TO_WRITE;
        }
      }

      u32 = file_info_struct->gid;
      __attribute__((cleanup(simple_archiver_helper_cleanup_c_string)))
      char *to_cleanup_group = NULL;
      const char *groupname = simple_archiver_hash_map_get(
        state->parsed->users_infos.GidToGname, &u32, sizeof(uint32_t));
      if (groupname) {
        if ((state->parsed->flags & 0x800) == 0) {
          uint32_t out_gid;
          const char *mapped_group = NULL;
          if (simple_archiver_get_group_mapping(state->parsed->mappings,
                                                state->parsed->users_infos,
                                                groupname,
                                                &out_gid,
                                                &mapped_group) == 0
              && mapped_group) {
            //fprintf(stderr,
            //        "NOTICE: Mapped Group %s to %s for %s\n",
            //        groupname,
            //        mapped_group,
            //        file_info_struct->filename);
            groupname = mapped_group;
            to_cleanup_group = (char *)mapped_group;
          }
        }
        unsigned long group_length = strlen(groupname);
        if (group_length > 0xFFFF) {
          return SDAS_INTERNAL_ERROR;
        }
        u16 = (uint16_t)group_length;
        simple_archiver_helper_16_bit_be(&u16);
        if (fwrite(&u16, 2, 1, out_f) != 1) {
          return SDAS_FAILED_TO_WRITE;
        }
        simple_archiver_helper_16_bit_be(&u16);
        if (fwrite(groupname, 1, u16 + 1, out_f) != (size_t)u16 + 1) {
          return SDAS_FAILED_TO_WRITE;
        }
      } else {
        u16 = 0;
        if (fwrite(&u16, 2, 1, out_f) != 1) {
          return SDAS_FAILED_TO_WRITE;
        }
      }
      uint64_t u64 = file_info_struct->file_size;
      simple_archiver_helper_64_bit_be(&u64);
      if (fwrite(&u64, 8, 1, out_f) != 1) {
        return SDAS_FAILED_TO_WRITE;
      }
    }

    file_node = saved_node;

    if (state->parsed->compressor && state->parsed->decompressor) {
      // Is compressing.

      size_t temp_filename_size = strlen(state->parsed->temp_dir) + 1 + 64;
      __attribute__((cleanup(
          simple_archiver_helper_cleanup_c_string))) char *temp_filename =
          malloc(temp_filename_size);

      __attribute__((cleanup(cleanup_temp_filename_delete))) void **ptrs_array =
          malloc(sizeof(void *) * 2);
      ptrs_array[0] = NULL;
      ptrs_array[1] = NULL;

      __attribute__((cleanup(simple_archiver_helper_cleanup_FILE)))
      FILE *temp_fd = NULL;

      if (state->parsed->temp_dir) {
        size_t idx = 0;
        size_t temp_dir_len = strlen(state->parsed->temp_dir);
        snprintf(temp_filename, temp_filename_size, TEMP_FILENAME_CMP,
                 state->parsed->temp_dir,
                 state->parsed->temp_dir[temp_dir_len - 1] == '/' ? "" : "/",
                 idx);
        do {
          FILE *test_fd = fopen(temp_filename, "rb");
          if (test_fd) {
            // File exists.
            fclose(test_fd);
            snprintf(
                temp_filename, temp_filename_size, TEMP_FILENAME_CMP,
                state->parsed->temp_dir,
                state->parsed->temp_dir[temp_dir_len - 1] == '/' ? "" : "/",
                ++idx);
          } else if (idx > 0xFFFF) {
            return SDAS_INTERNAL_ERROR;
          } else {
            break;
          }
        } while (1);
        temp_fd = fopen(temp_filename, "w+b");
        if (temp_fd) {
          ptrs_array[0] = temp_filename;
        }
      } else {
        temp_fd = tmpfile();
      }

      if (!temp_fd) {
        temp_fd = tmpfile();
        if (!temp_fd) {
          fprintf(stderr,
                  "ERROR: Failed to create a temporary file for archival!\n");
          return SDAS_INTERNAL_ERROR;
        }
      }

      // Handle SIGPIPE.
      is_sig_pipe_occurred = 0;
      signal(SIGPIPE, handle_sig_pipe);

      int pipe_into_cmd[2];
      int pipe_outof_cmd[2];
      __attribute__((cleanup(
          simple_archiver_internal_cleanup_decomp_pid))) pid_t compressor_pid =
          -1;

      if (pipe(pipe_into_cmd) != 0) {
        // Unable to create pipes.
        return SDAS_INTERNAL_ERROR;
      } else if (pipe(pipe_outof_cmd) != 0) {
        // Unable to create second set of pipes.
        close(pipe_into_cmd[0]);
        close(pipe_into_cmd[1]);
        return SDAS_INTERNAL_ERROR;
      } else if (fcntl(pipe_into_cmd[1], F_SETFL, O_NONBLOCK) != 0) {
        // Unable to set non-blocking on into-write-pipe.
        close(pipe_into_cmd[0]);
        close(pipe_into_cmd[1]);
        close(pipe_outof_cmd[0]);
        close(pipe_outof_cmd[1]);
        return SDAS_INTERNAL_ERROR;
      } else if (fcntl(pipe_outof_cmd[0], F_SETFL, O_NONBLOCK) != 0) {
        // Unable to set non-blocking on outof-read-pipe.
        close(pipe_into_cmd[0]);
        close(pipe_into_cmd[1]);
        close(pipe_outof_cmd[0]);
        close(pipe_outof_cmd[1]);
        return SDAS_INTERNAL_ERROR;
      } else if (simple_archiver_de_compress(pipe_into_cmd, pipe_outof_cmd,
                                             state->parsed->compressor,
                                             &compressor_pid) != 0) {
        // Failed to spawn compressor.
        close(pipe_into_cmd[1]);
        close(pipe_outof_cmd[0]);
        fprintf(stderr,
                "WARNING: Failed to start compressor cmd! Invalid cmd?\n");
        return SDAS_INTERNAL_ERROR;
      }

      // Close unnecessary pipe fds on this end of the transfer.
      close(pipe_into_cmd[0]);
      close(pipe_outof_cmd[1]);

      // Set up cleanup so that remaining open pipes in this side is cleaned up.
      __attribute__((cleanup(
          simple_archiver_internal_cleanup_int_fd))) int pipe_outof_read =
          pipe_outof_cmd[0];
      __attribute__((cleanup(
          simple_archiver_internal_cleanup_int_fd))) int pipe_into_write =
          pipe_into_cmd[1];

      int_fast8_t to_temp_finished = 0;
      for (uint64_t file_idx = 0; file_idx < *((uint64_t *)chunk_c_node->data);
           ++file_idx) {
        if (is_sig_int_occurred) {
          return SDAS_SIGINT;
        }
        file_node = file_node->next;
        if (file_node == files_list->tail) {
          return SDAS_INTERNAL_ERROR;
        }
        const SDArchiverInternalFileInfo *file_info_struct = file_node->data;
        fprintf(stderr,
                "  FILE %3" PRIu64 " of %3" PRIu64 ": %s\n",
                file_idx + 1,
                *(uint64_t *)chunk_c_node->data,
                file_info_struct->filename);
        __attribute__((cleanup(simple_archiver_helper_cleanup_FILE))) FILE *fd =
            fopen(file_info_struct->filename, "rb");

        int_fast8_t to_comp_finished = 0;
        char hold_buf[SIMPLE_ARCHIVER_BUFFER_SIZE];
        ssize_t has_hold = -1;
        while (!to_comp_finished) {
          if (is_sig_pipe_occurred) {
            fprintf(stderr, "ERROR: SIGPIPE while compressing!\n");
            return SDAS_INTERNAL_ERROR;
          }
          if (!to_comp_finished) {
            // Write to compressor.
            if (ferror(fd)) {
              fprintf(stderr, "ERROR: Writing to chunk, file read error!\n");
              return SDAS_INTERNAL_ERROR;
            }
            if (has_hold < 0) {
              size_t fread_ret = fread(buf, 1, SIMPLE_ARCHIVER_BUFFER_SIZE, fd);
              if (fread_ret > 0) {
                ssize_t write_ret = write(pipe_into_write, buf, fread_ret);
                if (write_ret < 0) {
                  if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    // Non-blocking write.
                    has_hold = (int)fread_ret;
                    memcpy(hold_buf, buf, fread_ret);
                    nanosleep(&nonblock_sleep, NULL);
                  } else {
                    fprintf(
                        stderr,
                        "ERROR: Writing to compressor, pipe write error!\n");
                    return SDAS_FAILED_TO_WRITE;
                  }
                } else if (write_ret == 0) {
                  fprintf(
                      stderr,
                      "ERROR: Writing to compressor, unable to write bytes!\n");
                  return SDAS_FAILED_TO_WRITE;
                } else if ((size_t)write_ret < fread_ret) {
                  has_hold = (ssize_t)fread_ret - write_ret;
                  memcpy(hold_buf, buf + write_ret, (size_t)has_hold);
                }
              }

              if (feof(fd) && has_hold < 0) {
                to_comp_finished = 1;
              }
            } else {
              ssize_t write_ret =
                  write(pipe_into_write, hold_buf, (size_t)has_hold);
              if (write_ret < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                  // Non-blocking write.
                  nanosleep(&nonblock_sleep, NULL);
                } else {
                  return SDAS_INTERNAL_ERROR;
                }
              } else if (write_ret < has_hold) {
                memcpy(buf, hold_buf + write_ret,
                       (size_t)(has_hold - write_ret));
                memcpy(hold_buf, buf, (size_t)(has_hold - write_ret));
                has_hold = has_hold - write_ret;
              } else if (write_ret != has_hold) {
                return SDAS_INTERNAL_ERROR;
              } else {
                has_hold = -1;
              }
            }
          }

          // Write compressed data to temp file.
          ssize_t read_ret =
              read(pipe_outof_read, buf, SIMPLE_ARCHIVER_BUFFER_SIZE);
          if (read_ret < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
              // Non-blocking read.
              nanosleep(&nonblock_sleep, NULL);
            } else {
              fprintf(stderr,
                      "ERROR: Reading from compressor, pipe read error!\n");
              return SDAS_INTERNAL_ERROR;
            }
          } else if (read_ret == 0) {
            // EOF.
            to_temp_finished = 1;
          } else {
            size_t fwrite_ret = fwrite(buf, 1, (size_t)read_ret, temp_fd);
            if (fwrite_ret != (size_t)read_ret) {
              fprintf(stderr,
                      "ERROR: Reading from compressor, failed to write to "
                      "temporary file!\n");
              return SDAS_INTERNAL_ERROR;
            }
          }
        }
      }

      simple_archiver_internal_cleanup_int_fd(&pipe_into_write);

      // Finish writing.
      if (!to_temp_finished) {
        while (1) {
          if (is_sig_pipe_occurred) {
            fprintf(stderr, "ERROR: SIGPIPE while compressing!\n");
            return SDAS_INTERNAL_ERROR;
          }
          ssize_t read_ret =
              read(pipe_outof_read, buf, SIMPLE_ARCHIVER_BUFFER_SIZE);
          if (read_ret < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
              // Non-blocking read.
              nanosleep(&nonblock_sleep, NULL);
            } else {
              fprintf(stderr,
                      "ERROR: Reading from compressor, pipe read error!\n");
              return SDAS_INTERNAL_ERROR;
            }
          } else if (read_ret == 0) {
            // EOF.
            break;
          } else {
            size_t fwrite_ret = fwrite(buf, 1, (size_t)read_ret, temp_fd);
            if (fwrite_ret != (size_t)read_ret) {
              fprintf(stderr,
                      "ERROR: Reading from compressor, failed to write to "
                      "temporary file!\n");
              return SDAS_INTERNAL_ERROR;
            }
          }
        }
      }

      long comp_chunk_size = ftell(temp_fd);
      if (comp_chunk_size < 0) {
        fprintf(stderr,
                "ERROR: Temp file reported negative size after compression!\n");
        return SDAS_INTERNAL_ERROR;
      }

      // Write compressed chunk size.
      uint64_t u64 = (uint64_t)comp_chunk_size;
      simple_archiver_helper_64_bit_be(&u64);
      if (fwrite(&u64, 8, 1, out_f) != 1) {
        return SDAS_FAILED_TO_WRITE;
      }

      if (fseek(temp_fd, 0, SEEK_SET) != 0) {
        return SDAS_INTERNAL_ERROR;
      }

      size_t written_size = 0;

      // Write compressed chunk.
      while (!feof(temp_fd)) {
        if (is_sig_int_occurred) {
          return SDAS_SIGINT;
        } else if (ferror(temp_fd)) {
          return SDAS_INTERNAL_ERROR;
        }
        size_t fread_ret = fread(buf, 1, SIMPLE_ARCHIVER_BUFFER_SIZE, temp_fd);
        if (fread_ret > 0) {
          size_t fwrite_ret = fwrite(buf, 1, fread_ret, out_f);
          written_size += fread_ret;
          if (fwrite_ret != fread_ret) {
            fprintf(stderr,
                    "ERROR: Partial write of read bytes from temp file to "
                    "output file!\n");
            return SDAS_FAILED_TO_WRITE;
          }
        }
      }

      if (written_size != (size_t)comp_chunk_size) {
        fprintf(stderr,
                "ERROR: Written chunk size is not actual chunk size!\n");
        return SDAS_FAILED_TO_WRITE;
      }

      // Cleanup and remove temp_fd.
      simple_archiver_helper_cleanup_FILE(&temp_fd);
    } else {
      // Is NOT compressing.
      if (!non_c_chunk_size) {
        return SDAS_INTERNAL_ERROR;
      }
      simple_archiver_helper_64_bit_be(non_c_chunk_size);
      fwrite(non_c_chunk_size, 8, 1, out_f);
      for (uint64_t file_idx = 0; file_idx < *((uint64_t *)chunk_c_node->data);
           ++file_idx) {
        if (is_sig_int_occurred) {
          return SDAS_SIGINT;
        }
        file_node = file_node->next;
        if (file_node == files_list->tail) {
          return SDAS_INTERNAL_ERROR;
        }
        const SDArchiverInternalFileInfo *file_info_struct = file_node->data;
        fprintf(stderr,
                "  FILE %3" PRIu64 " of %3" PRIu64 ": %s\n",
                file_idx + 1,
                *(uint64_t *)chunk_c_node->data,
                file_info_struct->filename);
        __attribute__((cleanup(simple_archiver_helper_cleanup_FILE))) FILE *fd =
            fopen(file_info_struct->filename, "rb");
        while (!feof(fd)) {
          if (ferror(fd)) {
            fprintf(stderr, "ERROR: Writing to chunk, file read error!\n");
            return SDAS_INTERNAL_ERROR;
          }
          size_t fread_ret = fread(buf, 1, SIMPLE_ARCHIVER_BUFFER_SIZE, fd);
          if (fread_ret > 0) {
            size_t fwrite_ret = fwrite(buf, 1, fread_ret, out_f);
            if (fwrite_ret != fread_ret) {
              fprintf(stderr, "ERROR: Writing to chunk, file write error!\n");
              return SDAS_FAILED_TO_WRITE;
            }
          }
        }
      }
    }
  }

  // Write directory entries.

  if (dirs_list->count > 0xFFFFFFFF) {
    return SDAS_TOO_MANY_DIRS;
  }

  u32 = (uint32_t)dirs_list->count;
  if (u32 != 0) {
    fprintf(stderr, "Directories:\n");
  }

  simple_archiver_helper_32_bit_be(&u32);

  if (fwrite(&u32, 4, 1, out_f) != 1) {
    return SDAS_FAILED_TO_WRITE;
  }

  void **void_ptrs = malloc(sizeof(void*) * 2);
  void_ptrs[0] = out_f;
  void_ptrs[1] = state;

  if (simple_archiver_list_get(dirs_list,
                               internal_write_dir_entries_v2_v3,
                               void_ptrs)) {
    free(void_ptrs);
    return SDAS_INTERNAL_ERROR;
  }
  free(void_ptrs);

  return SDAS_SUCCESS;
}

int simple_archiver_parse_archive_info(FILE *in_f, int_fast8_t do_extract,
                                       const SDArchiverState *state) {
#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
  signal(SIGINT, handle_sig_int);
#endif

  uint8_t buf[32];
  memset(buf, 0, 32);
  uint16_t u16;

  if (fread(buf, 1, 18, in_f) != 18) {
    return SDAS_INVALID_FILE;
  } else if (memcmp(buf, "SIMPLE_ARCHIVE_VER", 18) != 0) {
    return SDAS_INVALID_FILE;
  } else if (fread(buf, 1, 2, in_f) != 2) {
    return SDAS_INVALID_FILE;
  }

  memcpy(&u16, buf, 2);
  simple_archiver_helper_16_bit_be(&u16);

  if (u16 == 0) {
    fprintf(stderr, "File format version 0\n");
    return simple_archiver_parse_archive_version_0(in_f, do_extract, state);
  } else if (u16 == 1) {
    fprintf(stderr, "File format version 1\n");
    return simple_archiver_parse_archive_version_1(in_f, do_extract, state);
  } else if (u16 == 2) {
    fprintf(stderr, "File format version 2\n");
    return simple_archiver_parse_archive_version_2(in_f, do_extract, state);
  } else if (u16 == 3) {
    fprintf(stderr, "File format version 3\n");
    return simple_archiver_parse_archive_version_3(in_f, do_extract, state);
  } else {
    fprintf(stderr, "ERROR Unsupported archive version %" PRIu16 "!\n", u16);
    return SDAS_INVALID_FILE;
  }
}

int simple_archiver_parse_archive_version_0(FILE *in_f, int_fast8_t do_extract,
                                            const SDArchiverState *state) {
  uint8_t buf[SIMPLE_ARCHIVER_BUFFER_SIZE];
  uint16_t u16;
  uint32_t u32;
  uint64_t u64;
  int_fast8_t is_compressed = 0;

  if (fread(buf, 1, 4, in_f) != 4) {
    return SDAS_INVALID_FILE;
  }

  if (do_extract && state->parsed->user_cwd) {
#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
    if (chdir(state->parsed->user_cwd)) {
      return SDAS_FAILED_TO_CHANGE_CWD;
    }
#endif
  }

  __attribute__((cleanup(
      simple_archiver_helper_cleanup_malloced))) void *decompressor_cmd = NULL;

  if ((buf[0] & 1) != 0) {
    fprintf(stderr, "De/compressor flag is set.\n");
    is_compressed = 1;

    // Read compressor data.
    if (fread(&u16, 2, 1, in_f) != 1) {
      return SDAS_INVALID_FILE;
    }
    simple_archiver_helper_16_bit_be(&u16);
    fprintf(stderr, "Compressor size is %" PRIu16 "\n", u16);
    if (u16 < SIMPLE_ARCHIVER_BUFFER_SIZE) {
      if (fread(buf, 1, u16 + 1, in_f) != (size_t)u16 + 1) {
        return SDAS_INVALID_FILE;
      }
      buf[SIMPLE_ARCHIVER_BUFFER_SIZE - 1] = 0;
      fprintf(stderr, "Compressor cmd: %s\n", buf);
    } else {
      __attribute__((
          cleanup(simple_archiver_helper_cleanup_malloced))) void *heap_buf =
          malloc(u16 + 1);
      uint8_t *uc_heap_buf = heap_buf;
      if (fread(uc_heap_buf, 1, u16 + 1, in_f) != (size_t)u16 + 1) {
        return SDAS_INVALID_FILE;
      }
      uc_heap_buf[u16 - 1] = 0;
      fprintf(stderr, "Compressor cmd: %s\n", uc_heap_buf);
    }

    // Read decompressor data.
    if (fread(&u16, 2, 1, in_f) != 1) {
      return SDAS_INVALID_FILE;
    }
    simple_archiver_helper_16_bit_be(&u16);
    fprintf(stderr, "Decompressor size is %" PRIu16 "\n", u16);
    if (u16 < SIMPLE_ARCHIVER_BUFFER_SIZE) {
      if (fread(buf, 1, u16 + 1, in_f) != (size_t)u16 + 1) {
        return SDAS_INVALID_FILE;
      }
      buf[SIMPLE_ARCHIVER_BUFFER_SIZE - 1] = 0;
      fprintf(stderr, "Decompressor cmd: %s\n", buf);
      decompressor_cmd = malloc(u16 + 1);
      memcpy((char *)decompressor_cmd, buf, u16 + 1);
      ((char *)decompressor_cmd)[u16] = 0;
    } else {
      __attribute__((
          cleanup(simple_archiver_helper_cleanup_malloced))) void *heap_buf =
          malloc(u16 + 1);
      uint8_t *uc_heap_buf = heap_buf;
      if (fread(uc_heap_buf, 1, u16 + 1, in_f) != (size_t)u16 + 1) {
        return SDAS_INVALID_FILE;
      }
      uc_heap_buf[u16 - 1] = 0;
      fprintf(stderr, "Decompressor cmd: %s\n", uc_heap_buf);
      decompressor_cmd = heap_buf;
      heap_buf = NULL;
    }
  } else {
    fprintf(stderr, "De/compressor flag is NOT set.\n");
  }

  if (fread(&u32, 1, 4, in_f) != 4) {
    return SDAS_INVALID_FILE;
  }
  simple_archiver_helper_32_bit_be(&u32);
  fprintf(stderr, "File count is %" PRIu32 "\n", u32);

  if (is_sig_int_occurred) {
    return SDAS_SIGINT;
  }

  const uint32_t size = u32;
  const size_t digits = simple_archiver_helper_num_digits(size);
  char format_str[128];
  snprintf(format_str, 128, FILE_COUNTS_OUTPUT_FORMAT_STR_0, digits, digits);
  int_fast8_t skip;
  __attribute__((cleanup(simple_archiver_hash_map_free)))
  SDArchiverHashMap *hash_map = NULL;
  if (state && state->parsed->working_files &&
      state->parsed->working_files[0] != NULL) {
    hash_map = simple_archiver_hash_map_init();
    for (char **iter = state->parsed->working_files; *iter != NULL; ++iter) {
      size_t len = strlen(*iter) + 1;
      char *key = malloc(len);
      memcpy(key, *iter, len);
      key[len - 1] = 0;
      simple_archiver_hash_map_insert(
          hash_map, key, key, len,
          simple_archiver_helper_datastructure_cleanup_nop, NULL);
      // fprintf(stderr, "\"%s\" put in map\n", key);
    }
  }

  __attribute__((cleanup(simple_archiver_list_free)))
  SDArchiverLinkedList *links_list =
      state && state->parsed && (state->parsed->flags & 0x80)
          ? NULL
          : simple_archiver_list_init();
  __attribute__((cleanup(simple_archiver_hash_map_free)))
  SDArchiverHashMap *files_map =
      state && state->parsed && (state->parsed->flags & 0x80)
          ? NULL
          : simple_archiver_hash_map_init();

  for (uint32_t idx = 0; idx < size; ++idx) {
    if (is_sig_int_occurred) {
      return SDAS_SIGINT;
    }
    skip = 0;
    fprintf(stderr, format_str, idx + 1, size);
    if (feof(in_f) || ferror(in_f)) {
      return SDAS_INVALID_FILE;
    } else if (fread(&u16, 2, 1, in_f) != 1) {
      return SDAS_INVALID_FILE;
    }
    simple_archiver_helper_16_bit_be(&u16);
    __attribute__((cleanup(
        simple_archiver_helper_cleanup_malloced))) void *out_f_name = NULL;
    __attribute__((cleanup(simple_archiver_helper_cleanup_FILE))) FILE *out_f =
        NULL;
    __attribute__((cleanup(
        cleanup_overwrite_filename_delete_simple))) char *to_overwrite_dest =
        NULL;
    __attribute__((cleanup(simple_archiver_helper_cleanup_c_string)))
    char *filename_with_prefix = NULL;
    if (u16 < SIMPLE_ARCHIVER_BUFFER_SIZE) {
      if (fread(buf, 1, u16 + 1, in_f) != (size_t)u16 + 1) {
        return SDAS_INVALID_FILE;
      }
      buf[SIMPLE_ARCHIVER_BUFFER_SIZE - 1] = 0;
      fprintf(stderr, "  Filename: %s\n", buf);
      if (simple_archiver_validate_file_path((char *)buf)) {
        fprintf(stderr, "  ERROR: Invalid filename!\n");
        skip = 1;
      }

      if (state && state->parsed->prefix) {
        const size_t buf_str_len = strlen((const char *)buf);
        const size_t prefix_length = strlen(state->parsed->prefix);
        filename_with_prefix = malloc(buf_str_len + prefix_length + 1);
        memcpy(filename_with_prefix, state->parsed->prefix, prefix_length);
        memcpy(filename_with_prefix + prefix_length, buf, buf_str_len);
        filename_with_prefix[prefix_length + buf_str_len] = 0;
      }

      if (do_extract && !skip) {
        if ((state->parsed->flags & 0x8) == 0) {
          __attribute__((cleanup(simple_archiver_helper_cleanup_FILE)))
          FILE *test_fd =
            fopen(filename_with_prefix
                    ? filename_with_prefix
                    : (const char *)buf,
                  "rb");
          if (test_fd) {
            skip = 1;
            fprintf(stderr,
                    "  WARNING: File already exists and "
                    "\"--overwrite-extract\" is not specified, skipping!\n");
          } else {
            skip = 0;
          }
        } else {
          skip = 0;
          int fd =
            open(filename_with_prefix
                   ? filename_with_prefix
                   : (const char *)buf,
                 O_RDONLY | O_NOFOLLOW);
          if (fd == -1) {
            if (errno == ELOOP) {
              // Is an existing symbolic file.
              // Defer deletion to after "is invalid" check.
              to_overwrite_dest = strdup(filename_with_prefix
                                         ? filename_with_prefix
                                         : (const char *)buf);
            }
          } else {
            close(fd);
            // Is an existing file.
            // Defer deletion to after "is invalid" check.
            to_overwrite_dest = strdup(filename_with_prefix
                                       ? filename_with_prefix
                                       : (const char *)buf);
          }
        }
        if (!skip) {
          // Don't replace with "filename_with_prefix" here,
          // original filename is needed later on.
          out_f_name = strdup((const char *)buf);
        }
      }
    } else {
      __attribute__((
          cleanup(simple_archiver_helper_cleanup_malloced))) void *heap_buf =
          malloc((uint32_t)u16 + 1);
      uint8_t *uc_heap_buf = heap_buf;
      if (fread(uc_heap_buf, 1, (uint32_t)u16 + 1, in_f) != (size_t)u16 + 1) {
        return SDAS_INVALID_FILE;
      }
      uc_heap_buf[u16] = 0;
      fprintf(stderr, "  Filename: %s\n", uc_heap_buf);

      if (simple_archiver_validate_file_path((char *)uc_heap_buf)) {
        fprintf(stderr, "  ERROR: Invalid filename!\n");
        skip = 1;
      }

      if (state && state->parsed->prefix) {
        const size_t heap_buf_str_len = strlen((const char *)uc_heap_buf);
        const size_t prefix_length = strlen(state->parsed->prefix);
        filename_with_prefix = malloc(heap_buf_str_len + prefix_length + 1);
        memcpy(filename_with_prefix, state->parsed->prefix, prefix_length);
        memcpy(filename_with_prefix + prefix_length, uc_heap_buf, heap_buf_str_len);
        filename_with_prefix[prefix_length + heap_buf_str_len] = 0;
      }

      if (do_extract && !skip) {
        if ((state->parsed->flags & 0x8) == 0) {
          __attribute__((cleanup(simple_archiver_helper_cleanup_FILE)))
          FILE *test_fd = fopen(filename_with_prefix
                                  ? filename_with_prefix
                                  : (const char *)uc_heap_buf,
                                "rb");
          if (test_fd) {
            skip = 1;
            fprintf(stderr,
                    "WARNING: File already exists and \"--overwrite-extract\" "
                    "is not specified, skipping!\n");
          } else {
            skip = 0;
          }
        } else {
          skip = 0;
          int fd = open(filename_with_prefix
                          ? filename_with_prefix
                          : (const char *)uc_heap_buf,
                        O_RDONLY | O_NOFOLLOW);
          if (fd == -1) {
            if (errno == ELOOP) {
              // Is an existing symbolic file.
              // Defer deletion to after "is invalid" check.
              to_overwrite_dest = strdup(filename_with_prefix
                                         ? filename_with_prefix
                                         : (const char *)uc_heap_buf);
            }
          } else {
            close(fd);
            // Is an existing file.
            // Defer deletion to after "is invalid" check.
            to_overwrite_dest = strdup(filename_with_prefix
                                       ? filename_with_prefix
                                       : (const char *)uc_heap_buf);
          }
        }
        if (!skip) {
          // Don't replace with "filename_with_prefix" here,
          // original filename is needed later on.
          out_f_name = strdup((const char *)uc_heap_buf);
        }
      }
    }

    if (fread(buf, 1, 4, in_f) != 4) {
      return SDAS_INVALID_FILE;
    }

    // Check for "invalid entry" flag.
    if ((buf[1] & 0x8) != 0) {
      free(to_overwrite_dest);
      to_overwrite_dest = NULL;
      fprintf(stderr, "  This file entry was marked invalid, skipping...\n");
      continue;
    } else {
      // Do deferred overwrite action: remove existing file/symlink.
      cleanup_overwrite_filename_delete_simple(&to_overwrite_dest);
    }

    if (files_map && !skip && out_f_name) {
      simple_archiver_internal_paths_to_files_map(files_map,
                                                  filename_with_prefix
                                                    ? filename_with_prefix
                                                    : out_f_name);
    }

#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
    mode_t permissions = 0;

    if (do_extract == 0) {
      fprintf(stderr, "  Permissions: ");
    }

    if ((buf[0] & 0x2) != 0) {
      permissions |= S_IRUSR;
      if (do_extract == 0) {
        fprintf(stderr, "r");
      }
    } else if (do_extract == 0) {
      fprintf(stderr, "-");
    }
    if ((buf[0] & 0x4) != 0) {
      permissions |= S_IWUSR;
      if (do_extract == 0) {
        fprintf(stderr, "w");
      }
    } else if (do_extract == 0) {
      fprintf(stderr, "-");
    }
    if ((buf[0] & 0x8) != 0) {
      permissions |= S_IXUSR;
      if (do_extract == 0) {
        fprintf(stderr, "x");
      }
    } else if (do_extract == 0) {
      fprintf(stderr, "-");
    }
    if ((buf[0] & 0x10) != 0) {
      permissions |= S_IRGRP;
      if (do_extract == 0) {
        fprintf(stderr, "r");
      }
    } else if (do_extract == 0) {
      fprintf(stderr, "-");
    }
    if ((buf[0] & 0x20) != 0) {
      permissions |= S_IWGRP;
      if (do_extract == 0) {
        fprintf(stderr, "w");
      }
    } else if (do_extract == 0) {
      fprintf(stderr, "-");
    }
    if ((buf[0] & 0x40) != 0) {
      permissions |= S_IXGRP;
      if (do_extract == 0) {
        fprintf(stderr, "x");
      }
    } else if (do_extract == 0) {
      fprintf(stderr, "-");
    }
    if ((buf[0] & 0x80) != 0) {
      permissions |= S_IROTH;
      if (do_extract == 0) {
        fprintf(stderr, "r");
      }
    } else if (do_extract == 0) {
      fprintf(stderr, "-");
    }
    if ((buf[1] & 0x1) != 0) {
      permissions |= S_IWOTH;
      if (do_extract == 0) {
        fprintf(stderr, "w");
      }
    } else if (do_extract == 0) {
      fprintf(stderr, "-");
    }
    if ((buf[1] & 0x2) != 0) {
      permissions |= S_IXOTH;
      if (do_extract == 0) {
        fprintf(stderr, "x");
      }
    } else if (do_extract == 0) {
      fprintf(stderr, "-");
    }

    if (do_extract == 0) {
      fprintf(stderr, "\n");
    }

#endif

    if (state && state->parsed->flags & 0x1000 && do_extract) {
      fprintf(stderr,
              "NOTICE: Forcing permissions as specified by "
              "\"--force-file-permissions\"!\n");
      permissions =
        simple_archiver_internal_permissions_to_mode_t(
          state->parsed->file_permissions);
    }

    const uint_fast8_t points_to_outside = (buf[1] & 0x10) ? 1 : 0;

    if ((buf[0] & 1) == 0) {
      // Not a sybolic link.
      if (fread(&u64, 8, 1, in_f) != 1) {
        return SDAS_INVALID_FILE;
      }
      simple_archiver_helper_64_bit_be(&u64);
      if (is_compressed) {
        fprintf(stderr, "  File size (compressed): %" PRIu64 "\n", u64);
      } else {
        fprintf(stderr, "  File size: %" PRIu64 "\n", u64);
      }

      int_fast8_t skip_due_to_map = 0;
      if (hash_map != NULL && out_f_name) {
        if (simple_archiver_hash_map_get(hash_map, out_f_name,
                                         strlen(out_f_name) + 1) == NULL) {
          skip_due_to_map = 1;
          fprintf(stderr, "Skipping not specified in args...\n");
        }
      }

      if (do_extract && !skip && !skip_due_to_map) {
        fprintf(stderr, "  Extracting...\n");

        simple_archiver_helper_make_dirs_perms(
          filename_with_prefix
            ? filename_with_prefix
            : (const char *)out_f_name,
          (state->parsed->flags & 0x2000)
            ? simple_archiver_internal_permissions_to_mode_t(
                state->parsed->dir_permissions)
            : (S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH),
          (state->parsed->flags & 0x400) ? state->parsed->uid : getuid(),
          (state->parsed->flags & 0x800) ? state->parsed->gid : getgid());

        out_f = fopen(filename_with_prefix
                        ? filename_with_prefix
                        : out_f_name,
                      "wb");
        if (!out_f) {
          fprintf(stderr,
                  "WARNING: Failed to open \"%s\" for writing! (No write "
                  "permissions?)\n",
                  filename_with_prefix
                    ? filename_with_prefix
                    : (char *)out_f_name);
        }
        __attribute__((
            cleanup(cleanup_temp_filename_delete))) void **ptrs_array =
            malloc(sizeof(void *) * 2);
        ptrs_array[0] = filename_with_prefix
                          ? filename_with_prefix
                          : out_f_name;
        ptrs_array[1] = &out_f;
#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
        if (is_compressed && out_f) {
          // Handle SIGPIPE.
          signal(SIGPIPE, handle_sig_pipe);

          int pipe_into_cmd[2];
          int pipe_outof_cmd[2];
          pid_t decompressor_pid;
          if (pipe(pipe_into_cmd) != 0) {
            // Unable to create pipes.
            break;
          } else if (pipe(pipe_outof_cmd) != 0) {
            // Unable to create second set of pipes.
            close(pipe_into_cmd[0]);
            close(pipe_into_cmd[1]);
            return SDAS_INTERNAL_ERROR;
          } else if (fcntl(pipe_into_cmd[1], F_SETFL, O_NONBLOCK) != 0) {
            // Unable to set non-blocking on into-write-pipe.
            close(pipe_into_cmd[0]);
            close(pipe_into_cmd[1]);
            close(pipe_outof_cmd[0]);
            close(pipe_outof_cmd[1]);
            return SDAS_INTERNAL_ERROR;
          } else if (fcntl(pipe_outof_cmd[0], F_SETFL, O_NONBLOCK) != 0) {
            // Unable to set non-blocking on outof-read-pipe.
            close(pipe_into_cmd[0]);
            close(pipe_into_cmd[1]);
            close(pipe_outof_cmd[0]);
            close(pipe_outof_cmd[1]);
            return SDAS_INTERNAL_ERROR;
          }

          if (state && state->parsed && state->parsed->decompressor) {
            if (simple_archiver_de_compress(pipe_into_cmd, pipe_outof_cmd,
                                            state->parsed->decompressor,
                                            &decompressor_pid) != 0) {
              // Failed to spawn compressor.
              close(pipe_into_cmd[1]);
              close(pipe_outof_cmd[0]);
              fprintf(
                  stderr,
                  "WARNING: Failed to start decompressor cmd! Invalid cmd?\n");
              return SDAS_INTERNAL_ERROR;
            }
          } else {
            if (simple_archiver_de_compress(pipe_into_cmd, pipe_outof_cmd,
                                            decompressor_cmd,
                                            &decompressor_pid) != 0) {
              // Failed to spawn compressor.
              close(pipe_into_cmd[1]);
              close(pipe_outof_cmd[0]);
              fprintf(
                  stderr,
                  "WARNING: Failed to start decompressor cmd! Invalid cmd?\n");
              return SDAS_INTERNAL_ERROR;
            }
          }

          // Close unnecessary pipe fds on this end of the transfer.
          close(pipe_into_cmd[0]);
          close(pipe_outof_cmd[1]);

          int decompressor_status;
          int decompressor_return_val;
          int decompressor_ret =
              waitpid(decompressor_pid, &decompressor_status, WNOHANG);
          if (decompressor_ret == decompressor_pid) {
            // Status is available.
            if (WIFEXITED(decompressor_status)) {
              decompressor_return_val = WEXITSTATUS(decompressor_status);
              fprintf(stderr,
                      "WARNING: Exec failed (exec exit code %d)! Invalid "
                      "decompressor cmd?\n",
                      decompressor_return_val);
              return SDAS_INTERNAL_ERROR;
            }
          } else if (decompressor_ret == 0) {
            // Probably still running, continue on.
          } else {
            // Error.
            fprintf(stderr,
                    "WARNING: Exec failed (exec exit code unknown)! Invalid "
                    "decompressor cmd?\n");
            return SDAS_INTERNAL_ERROR;
          }

          uint64_t compressed_file_size = u64;
          int_fast8_t write_again = 0;
          int_fast8_t write_pipe_done = 0;
          int_fast8_t read_pipe_done = 0;
          size_t fread_ret = 0;
          char recv_buf[SIMPLE_ARCHIVER_BUFFER_SIZE];
          size_t amount_to_read;
          while (!write_pipe_done || !read_pipe_done) {
            if (is_sig_int_occurred) {
              if (pipe_into_cmd[1] >= 0) {
                close(pipe_into_cmd[1]);
                pipe_into_cmd[1] = -1;
              }
              if (pipe_outof_cmd[0] >= 0) {
                close(pipe_outof_cmd[0]);
                pipe_outof_cmd[0] = -1;
              }
              return SDAS_SIGINT;
            } else if (is_sig_pipe_occurred) {
              fprintf(stderr,
                      "WARNING: Failed to write to decompressor (SIGPIPE)! "
                      "Invalid decompressor cmd?\n");
              return SDAS_INTERNAL_ERROR;
            }

            // Read from file.
            if (!write_pipe_done) {
              if (!write_again && compressed_file_size != 0) {
                if (compressed_file_size > SIMPLE_ARCHIVER_BUFFER_SIZE) {
                  amount_to_read = SIMPLE_ARCHIVER_BUFFER_SIZE;
                } else {
                  amount_to_read = compressed_file_size;
                }
                fread_ret = fread(buf, 1, amount_to_read, in_f);
                if (fread_ret > 0) {
                  compressed_file_size -= fread_ret;
                }
              }

              // Send over pipe to decompressor.
              if (fread_ret > 0) {
                ssize_t write_ret = write(pipe_into_cmd[1], buf, fread_ret);
                if (write_ret > 0 && (size_t)write_ret == fread_ret) {
                  // Successful write.
                  write_again = 0;
                  if (compressed_file_size == 0) {
                    close(pipe_into_cmd[1]);
                    pipe_into_cmd[1] = -1;
                    write_pipe_done = 1;
                  }
                } else if (write_ret == -1) {
                  if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    nanosleep(&nonblock_sleep, NULL);
                    write_again = 1;
                  } else {
                    // Error.
                    fprintf(stderr,
                            "WARNING: Failed to write to decompressor! Invalid "
                            "decompressor cmd?\n");
                    return SDAS_INTERNAL_ERROR;
                  }
                } else {
                  // Should be unreachable, error.
                  fprintf(stderr,
                          "WARNING: Failed to write to decompressor! Invalid "
                          "decompressor cmd?\n");
                  return SDAS_INTERNAL_ERROR;
                }
              }
            }

            // Read output from decompressor and write to file.
            if (!read_pipe_done) {
              ssize_t read_ret = read(pipe_outof_cmd[0], recv_buf,
                                      SIMPLE_ARCHIVER_BUFFER_SIZE);
              if (read_ret > 0) {
                size_t fwrite_ret =
                    fwrite(recv_buf, 1, (size_t)read_ret, out_f);
                if (fwrite_ret == (size_t)read_ret) {
                  // Success.
                } else if (ferror(out_f)) {
                  // Error.
                  fprintf(stderr,
                          "WARNING: Failed to read from decompressor! Invalid "
                          "decompressor cmd?\n");
                  return SDAS_INTERNAL_ERROR;
                } else {
                  // Invalid state, error.
                  fprintf(stderr,
                          "WARNING: Failed to read from decompressor! Invalid "
                          "decompressor cmd?\n");
                  return SDAS_INTERNAL_ERROR;
                }
              } else if (read_ret == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                  // No bytes to read yet.
                  nanosleep(&nonblock_sleep, NULL);
                } else {
                  // Error.
                  fprintf(stderr,
                          "WARNING: Failed to read from decompressor! Invalid "
                          "decompressor cmd?\n");
                  return SDAS_INTERNAL_ERROR;
                }
              } else if (read_ret == 0) {
                // EOF.
                read_pipe_done = 1;
                close(pipe_outof_cmd[0]);
                pipe_outof_cmd[0] = -1;
                simple_archiver_helper_cleanup_FILE(&out_f);
              } else {
                // Invalid state (unreachable?), error.
                fprintf(stderr,
                        "WARNING: Failed to read from decompressor! Invalid "
                        "decompressor cmd?\n");
                return SDAS_INTERNAL_ERROR;
              }
            }
          }

          if (is_sig_pipe_occurred) {
            fprintf(stderr,
                    "WARNING: Failed to write to decompressor (SIGPIPE)! "
                    "Invalid decompressor cmd?\n");
            return 1;
          }

          waitpid(decompressor_pid, NULL, 0);
        } else {
          uint64_t compressed_file_size = u64;
          size_t fread_ret;
          while (compressed_file_size != 0) {
            if (compressed_file_size > SIMPLE_ARCHIVER_BUFFER_SIZE) {
              fread_ret = fread(buf, 1, SIMPLE_ARCHIVER_BUFFER_SIZE, in_f);
              if (ferror(in_f)) {
                // Error.
                return SDAS_INTERNAL_ERROR;
              }
              if (out_f) {
                fwrite(buf, 1, fread_ret, out_f);
                if (ferror(out_f)) {
                  // Error.
                  return SDAS_INTERNAL_ERROR;
                }
              }
              compressed_file_size -= fread_ret;
            } else {
              fread_ret = fread(buf, 1, compressed_file_size, in_f);
              if (ferror(in_f)) {
                // Error.
                return SDAS_INTERNAL_ERROR;
              }
              if (out_f) {
                fwrite(buf, 1, fread_ret, out_f);
                if (ferror(out_f)) {
                  // Error.
                  return SDAS_INTERNAL_ERROR;
                }
              }
              compressed_file_size -= fread_ret;
            }
          }
        }

        if (chmod(filename_with_prefix
                    ? filename_with_prefix
                    : (const char *)out_f_name,
                  permissions) == -1) {
          // Error.
          return SDAS_INTERNAL_ERROR;
        }

        ptrs_array[0] = NULL;
        if (out_f) {
          fprintf(stderr, "  Extracted.\n");
        }
#endif
      } else {
        while (u64 != 0) {
          if (u64 > SIMPLE_ARCHIVER_BUFFER_SIZE) {
            size_t read_ret = fread(buf, 1, SIMPLE_ARCHIVER_BUFFER_SIZE, in_f);
            if (read_ret > 0) {
              u64 -= read_ret;
            } else if (ferror(in_f)) {
              return SDAS_INTERNAL_ERROR;
            }
          } else {
            size_t read_ret = fread(buf, 1, u64, in_f);
            if (read_ret > 0) {
              u64 -= read_ret;
            } else if (ferror(in_f)) {
              return SDAS_INTERNAL_ERROR;
            }
          }
        }
      }
    } else {
      // Is a symbolic link.

      int_fast8_t abs_preferred = (buf[1] & 0x4) != 0 ? 1 : 0;
      fprintf(stderr, "  Absolute path is %s\n",
              (abs_preferred ? "preferred" : "NOT preferred"));

      __attribute__((cleanup(
          simple_archiver_helper_cleanup_malloced))) void *abs_path = NULL;
      __attribute__((cleanup(
          simple_archiver_helper_cleanup_malloced))) void *rel_path = NULL;

      if (fread(&u16, 2, 1, in_f) != 1) {
        return SDAS_INVALID_FILE;
      }
      simple_archiver_helper_16_bit_be(&u16);
      if (u16 == 0) {
        fprintf(stderr, "  Link does not have absolute path.\n");
      } else if (u16 < SIMPLE_ARCHIVER_BUFFER_SIZE) {
        if (fread(buf, 1, u16 + 1, in_f) != (size_t)u16 + 1) {
          return SDAS_INVALID_FILE;
        }
        buf[SIMPLE_ARCHIVER_BUFFER_SIZE - 1] = 0;
        fprintf(stderr, "  Link absolute path: %s\n", buf);
        abs_path = malloc((size_t)u16 + 1);
        strncpy(abs_path, (char *)buf, (size_t)u16 + 1);
      } else {
        abs_path = malloc(u16 + 1);
        if (fread(abs_path, 1, u16 + 1, in_f) != (size_t)u16 + 1) {
          return SDAS_INVALID_FILE;
        }
        ((char *)abs_path)[u16 - 1] = 0;
        fprintf(stderr, "  Link absolute path: %s\n", (char *)abs_path);
      }

      if (fread(&u16, 2, 1, in_f) != 1) {
        return SDAS_INVALID_FILE;
      }
      simple_archiver_helper_16_bit_be(&u16);
      if (u16 == 0) {
        fprintf(stderr, "  Link does not have relative path.\n");
      } else if (u16 < SIMPLE_ARCHIVER_BUFFER_SIZE) {
        if (fread(buf, 1, u16 + 1, in_f) != (size_t)u16 + 1) {
          return SDAS_INVALID_FILE;
        }
        buf[SIMPLE_ARCHIVER_BUFFER_SIZE - 1] = 0;
        fprintf(stderr, "  Link relative path: %s\n", buf);
        rel_path = malloc((size_t)u16 + 1);
        strncpy(rel_path, (char *)buf, (size_t)u16 + 1);
      } else {
        rel_path = malloc(u16 + 1);
        if (fread(rel_path, 1, u16 + 1, in_f) != (size_t)u16 + 1) {
          return SDAS_INVALID_FILE;
        }
        ((char *)rel_path)[u16 - 1] = 0;
        fprintf(stderr, "  Link relative path: %s\n", (char *)rel_path);
      }

      if (do_extract && !skip) {
        simple_archiver_helper_make_dirs_perms(
          filename_with_prefix
            ? filename_with_prefix
            : (const char *)out_f_name,
          (state->parsed->flags & 0x2000)
            ? simple_archiver_internal_permissions_to_mode_t(
                state->parsed->dir_permissions)
            : (S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH),
          (state->parsed->flags & 0x400) ? state->parsed->uid : getuid(),
          (state->parsed->flags & 0x800) ? state->parsed->gid : getgid());
        if (abs_path && rel_path) {
          if (abs_preferred) {
#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
            int_fast8_t retry_symlink = 0;
            int ret;
            __attribute__((cleanup(simple_archiver_helper_cleanup_c_string)))
            char *abs_path_prefixed = NULL;
            if (state->parsed->prefix) {
              if (points_to_outside) {
                abs_path_prefixed = strdup(abs_path);
              } else {
                abs_path_prefixed =
                  simple_archiver_helper_insert_prefix_in_link_path(
                    state->parsed->prefix, out_f_name, abs_path);
              }
            }
            if (filename_with_prefix && !abs_path_prefixed) {
              fprintf(stderr, "  ERROR: Prefix specified but unable to resolve"
                " abs link with prefix!\n");
              return SDAS_FAILED_TO_EXTRACT_SYMLINK;
            }
          V0_SYMLINK_CREATE_RETRY_0:
            ret = symlink(
              abs_path_prefixed ? abs_path_prefixed : abs_path,
              filename_with_prefix ? filename_with_prefix : out_f_name);
            if (ret == -1) {
              if (retry_symlink) {
                fprintf(stderr,
                        "  WARNING: Failed to create symlink after removing "
                        "existing symlink!\n");
                goto V0_SYMLINK_CREATE_AFTER_0;
              } else if (errno == EEXIST) {
                if ((state->parsed->flags & 8) == 0) {
                  fprintf(
                      stderr,
                      "  WARNING: Symlink already exists and "
                      "\"--overwrite-extract\" is not specified, skipping!\n");
                  goto V0_SYMLINK_CREATE_AFTER_0;
                } else {
                  fprintf(stderr,
                          "  NOTICE: Symlink already exists and "
                          "\"--overwrite-extract\" specified, attempting to "
                          "overwrite...\n");
                  unlink(filename_with_prefix
                         ? filename_with_prefix
                         : out_f_name);
                  retry_symlink = 1;
                  goto V0_SYMLINK_CREATE_RETRY_0;
                }
              } else {
                return SDAS_FAILED_TO_EXTRACT_SYMLINK;
              }
            }
            if (links_list) {
              simple_archiver_list_add(links_list,
                                       filename_with_prefix
                                         ? strdup(filename_with_prefix)
                                         : strdup(out_f_name),
                                       NULL);
            }
            ret = fchmodat(AT_FDCWD,
                           filename_with_prefix
                             ? filename_with_prefix
                             : out_f_name,
                           permissions,
                           AT_SYMLINK_NOFOLLOW);
            if (ret == -1) {
              if (errno == EOPNOTSUPP) {
                fprintf(stderr,
                        "  NOTICE: Setting permissions of symlink is not "
                        "supported by FS/OS!\n");
              } else {
                fprintf(
                    stderr,
                    "  WARNING: Failed to set permissions of symlink (%d)!\n",
                    errno);
              }
            }
          V0_SYMLINK_CREATE_AFTER_0:
            retry_symlink = 1;
#endif
          } else {
#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
            int_fast8_t retry_symlink = 0;
            int ret;
            __attribute__((cleanup(simple_archiver_helper_cleanup_c_string)))
            char *rel_path_prefixed =
              simple_archiver_helper_insert_prefix_in_link_path(
                state->parsed->prefix, out_f_name, rel_path);
            if (filename_with_prefix && !rel_path_prefixed) {
              fprintf(stderr, "  ERROR: Prefix specified but unable to resolve"
                " relative link with prefix!\n");
              return SDAS_FAILED_TO_EXTRACT_SYMLINK;
            }
          V0_SYMLINK_CREATE_RETRY_1:
            ret = symlink(
              rel_path_prefixed ? rel_path_prefixed : rel_path,
              filename_with_prefix ? filename_with_prefix : out_f_name);
            if (ret == -1) {
              if (retry_symlink) {
                fprintf(stderr,
                        "  WARNING: Failed to create symlink after removing "
                        "existing symlink!\n");
                goto V0_SYMLINK_CREATE_AFTER_1;
              } else if (errno == EEXIST) {
                if ((state->parsed->flags & 8) == 0) {
                  fprintf(
                      stderr,
                      "  WARNING: Symlink already exists and "
                      "\"--overwrite-extract\" is not specified, skipping!\n");
                  goto V0_SYMLINK_CREATE_AFTER_1;
                } else {
                  fprintf(stderr,
                          "  NOTICE: Symlink already exists and "
                          "\"--overwrite-extract\" specified, attempting to "
                          "overwrite...\n");
                  unlink(filename_with_prefix
                         ? filename_with_prefix
                         : out_f_name);
                  retry_symlink = 1;
                  goto V0_SYMLINK_CREATE_RETRY_1;
                }
              } else {
                return SDAS_FAILED_TO_EXTRACT_SYMLINK;
              }
            }
            if (links_list) {
              simple_archiver_list_add(links_list,
                                       filename_with_prefix
                                         ? strdup(filename_with_prefix)
                                         : strdup(out_f_name),
                                       NULL);
            }
            ret = fchmodat(AT_FDCWD,
                           filename_with_prefix
                             ? filename_with_prefix
                             : out_f_name,
                           permissions,
                           AT_SYMLINK_NOFOLLOW);
            if (ret == -1) {
              if (errno == EOPNOTSUPP) {
                fprintf(stderr,
                        "  NOTICE: Setting permissions of symlink is not "
                        "supported by FS/OS!\n");
              } else {
                fprintf(
                    stderr,
                    "  WARNING: Failed to set permissions of symlink (%d)!\n",
                    errno);
              }
            }
          V0_SYMLINK_CREATE_AFTER_1:
            retry_symlink = 1;
#endif
          }
        } else if (abs_path) {
#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
          __attribute__((cleanup(simple_archiver_helper_cleanup_c_string)))
          char *abs_path_prefixed = NULL;
          if (state->parsed->prefix) {
            if (points_to_outside) {
              abs_path_prefixed = strdup(abs_path);
            } else {
              abs_path_prefixed =
                simple_archiver_helper_insert_prefix_in_link_path(
                  state->parsed->prefix, out_f_name, abs_path);
            }
          }
          if (filename_with_prefix && !abs_path_prefixed) {
            fprintf(stderr, "  ERROR: Prefix specified but unable to resolve"
              " abs link with prefix!\n");
            return SDAS_FAILED_TO_EXTRACT_SYMLINK;
          }
          int ret = symlink(
            abs_path_prefixed
              ? abs_path_prefixed
              : abs_path,
            filename_with_prefix
              ? filename_with_prefix
              : out_f_name);
          if (ret == -1) {
            return SDAS_FAILED_TO_EXTRACT_SYMLINK;
          }
          if (links_list) {
            simple_archiver_list_add(
              links_list,
              filename_with_prefix
                ? strdup(filename_with_prefix)
                : strdup(out_f_name),
              NULL);
          }
          ret = fchmodat(AT_FDCWD,
                         filename_with_prefix
                           ? filename_with_prefix
                           : out_f_name,
                         permissions,
                         AT_SYMLINK_NOFOLLOW);
          if (ret == -1) {
            if (errno == EOPNOTSUPP) {
              fprintf(
                  stderr,
                  "  NOTICE: Setting permissions of symlink is not supported "
                  "by FS/OS!\n");
            } else {
              fprintf(stderr,
                      "  WARNING: Failed to set permissions of symlink (%d)!\n",
                      errno);
            }
          }
#endif
        } else if (rel_path) {
#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
          __attribute__((cleanup(simple_archiver_helper_cleanup_c_string)))
          char *rel_path_prefixed =
            simple_archiver_helper_insert_prefix_in_link_path(
              state->parsed->prefix, out_f_name, rel_path);
          if (filename_with_prefix && !rel_path_prefixed) {
            fprintf(stderr, "  ERROR: Prefix specified but unable to resolve"
              " relative link with prefix!\n");
            return SDAS_FAILED_TO_EXTRACT_SYMLINK;
          }
          int ret = symlink(
            rel_path_prefixed
              ? rel_path_prefixed
              : rel_path,
            filename_with_prefix
              ? filename_with_prefix
              : out_f_name);
          if (ret == -1) {
            return SDAS_FAILED_TO_EXTRACT_SYMLINK;
          }
          if (links_list) {
            simple_archiver_list_add(
              links_list,
              filename_with_prefix
                ? strdup(filename_with_prefix)
                : strdup(out_f_name),
              NULL);
          }
          ret = fchmodat(AT_FDCWD,
                         filename_with_prefix
                           ? filename_with_prefix
                           : out_f_name,
                         permissions,
                         AT_SYMLINK_NOFOLLOW);
          if (ret == -1) {
            if (errno == EOPNOTSUPP) {
              fprintf(
                  stderr,
                  "  NOTICE: Setting permissions of symlink is not supported "
                  "by FS/OS!\n");
            } else {
              fprintf(stderr,
                      "  WARNING: Failed to set permissions of symlink (%d)!\n",
                      errno);
            }
          }
#endif
        } else {
          fprintf(
              stderr,
              "  WARNING: Symlink entry in archive has no paths to link to!\n");
        }
      }
    }
  }

  if (do_extract && links_list && files_map) {
    simple_archiver_safe_links_enforce(links_list, files_map);
  }

  if (is_sig_int_occurred) {
    return SDAS_SIGINT;
  }

  return SDAS_SUCCESS;
}

int simple_archiver_parse_archive_version_1(FILE *in_f, int_fast8_t do_extract,
                                            const SDArchiverState *state) {
  uint8_t buf[SIMPLE_ARCHIVER_BUFFER_SIZE];
  uint16_t u16;
  uint32_t u32;
  uint64_t u64;

  __attribute__((cleanup(simple_archiver_hash_map_free)))
  SDArchiverHashMap *working_files_map = NULL;
  if (state && state->parsed->working_files &&
      state->parsed->working_files[0] != NULL) {
    working_files_map = simple_archiver_hash_map_init();
    for (char **iter = state->parsed->working_files; *iter != NULL; ++iter) {
      size_t len = strlen(*iter) + 1;
      char *key = malloc(len);
      memcpy(key, *iter, len);
      key[len - 1] = 0;
      simple_archiver_hash_map_insert(
          working_files_map, key, key, len,
          simple_archiver_helper_datastructure_cleanup_nop, NULL);
    }
  }

  if (fread(buf, 1, 4, in_f) != 4) {
    return SDAS_INVALID_FILE;
  }

  if (do_extract && state->parsed->user_cwd) {
#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
    if (chdir(state->parsed->user_cwd)) {
      return SDAS_FAILED_TO_CHANGE_CWD;
    }
#endif
  }

  __attribute__((cleanup(simple_archiver_list_free)))
  SDArchiverLinkedList *links_list =
      state && state->parsed && state->parsed->flags & 0x80
          ? NULL
          : simple_archiver_list_init();
  __attribute__((cleanup(simple_archiver_hash_map_free)))
  SDArchiverHashMap *files_map =
      state && state->parsed && state->parsed->flags & 0x80
          ? NULL
          : simple_archiver_hash_map_init();

  __attribute__((
      cleanup(simple_archiver_helper_cleanup_c_string))) char *cwd_realpath =
      realpath(".", NULL);

  const int_fast8_t is_compressed = (buf[0] & 1) ? 1 : 0;

  __attribute__((cleanup(
      simple_archiver_helper_cleanup_c_string))) char *compressor_cmd = NULL;
  __attribute__((cleanup(
      simple_archiver_helper_cleanup_c_string))) char *decompressor_cmd = NULL;

  if (is_compressed) {
    if (fread(buf, 1, 2, in_f) != 2) {
      return SDAS_INVALID_FILE;
    }
    memcpy(&u16, buf, 2);
    simple_archiver_helper_16_bit_be(&u16);
    compressor_cmd = malloc(u16 + 1);
    int ret =
        read_buf_full_from_fd(in_f, (char *)buf, SIMPLE_ARCHIVER_BUFFER_SIZE,
                              u16 + 1, compressor_cmd);
    if (ret != SDAS_SUCCESS) {
      return ret;
    }
    compressor_cmd[u16] = 0;

    fprintf(stderr, "Compressor command: %s\n", compressor_cmd);

    if (fread(buf, 1, 2, in_f) != 2) {
      return SDAS_INVALID_FILE;
    }
    memcpy(&u16, buf, 2);
    simple_archiver_helper_16_bit_be(&u16);
    decompressor_cmd = malloc(u16 + 1);
    ret = read_buf_full_from_fd(in_f, (char *)buf, SIMPLE_ARCHIVER_BUFFER_SIZE,
                                u16 + 1, decompressor_cmd);
    if (ret != SDAS_SUCCESS) {
      return ret;
    }
    decompressor_cmd[u16] = 0;

    fprintf(stderr, "Decompressor command: %s\n", decompressor_cmd);
    if (state && state->parsed && state->parsed->decompressor) {
      fprintf(stderr, "Overriding decompressor with: %s\n",
              state->parsed->decompressor);
    }
  }

  if (is_sig_int_occurred) {
    return SDAS_SIGINT;
  }

  const size_t prefix_length = state && state->parsed->prefix
                               ? strlen(state->parsed->prefix)
                               : 0;

  // Link count.
  if (fread(buf, 1, 4, in_f) != 4) {
    return SDAS_INVALID_FILE;
  }
  memcpy(&u32, buf, 4);
  simple_archiver_helper_32_bit_be(&u32);

  for (uint32_t idx = 0; idx < u32; ++idx) {
    if (is_sig_int_occurred) {
      return SDAS_SIGINT;
    }
    fprintf(stderr, "SYMLINK %3" PRIu32 " of %3" PRIu32 "\n", idx + 1, u32);
    if (fread(buf, 1, 2, in_f) != 2) {
      return SDAS_INVALID_FILE;
    }
    const uint_fast8_t absolute_preferred = (buf[0] & 1) ? 1 : 0;
    const uint_fast8_t is_invalid = (buf[1] & 4) ? 1 : 0;
    const uint_fast8_t points_to_outside = (buf[1] & 8) ? 1 : 0;

    if (is_invalid) {
      fprintf(stderr, "  WARNING: This symlink entry was marked invalid!\n");
    }

#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
    mode_t permissions = permissions_from_bits_v1_symlink(buf, 0);
#endif

    uint_fast8_t link_extracted = 0;
    uint_fast8_t skip_due_to_map = 0;
    uint_fast8_t skip_due_to_invalid = is_invalid ? 1 : 0;

    if (fread(buf, 1, 2, in_f) != 2) {
      return SDAS_INVALID_FILE;
    }
    memcpy(&u16, buf, 2);
    simple_archiver_helper_16_bit_be(&u16);

    const size_t link_name_length = u16;

    __attribute__((
        cleanup(simple_archiver_helper_cleanup_c_string))) char *link_name =
        malloc(link_name_length + 1);

    int ret = read_buf_full_from_fd(in_f,
                                    (char *)buf,
                                    SIMPLE_ARCHIVER_BUFFER_SIZE,
                                    link_name_length + 1,
                                    link_name);
    if (ret != SDAS_SUCCESS) {
      return ret;
    }
    link_name[link_name_length] = 0;

    if (!do_extract) {
      fprintf(stderr, "  Link name: %s\n", link_name);
      if (absolute_preferred) {
        fprintf(stderr, "  Absolute path preferred.\n");
      } else {
        fprintf(stderr, "  Relative path preferred.\n");
      }
#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
      fprintf(stderr, "  Link Permissions: ");
      print_permissions(permissions);
      fprintf(stderr, "\n");
#endif
    }

    __attribute__((cleanup(simple_archiver_helper_cleanup_c_string)))
    char *link_name_prefixed = NULL;
    if (state && state->parsed->prefix) {
      link_name_prefixed = malloc(prefix_length + link_name_length + 1);
      memcpy(link_name_prefixed, state->parsed->prefix, prefix_length);
      memcpy(link_name_prefixed + prefix_length, link_name, link_name_length + 1);
      link_name_prefixed[prefix_length + link_name_length] = 0;
    }

    if (simple_archiver_validate_file_path(link_name)) {
      fprintf(stderr, "  WARNING: Invalid link name \"%s\"!\n", link_name);
      skip_due_to_invalid = 1;
    }

    if (working_files_map &&
        simple_archiver_hash_map_get(working_files_map, link_name, u16 + 1) ==
            NULL) {
      skip_due_to_map = 1;
      fprintf(stderr, "  Skipping not specified in args...\n");
    }

    __attribute__((cleanup(simple_archiver_helper_cleanup_c_string)))
    char *abs_path_prefixed = NULL;

    if (fread(&u16, 2, 1, in_f) != 1) {
      return SDAS_INVALID_FILE;
    }
    simple_archiver_helper_16_bit_be(&u16);
    if (u16 != 0) {
      const size_t path_length = u16;
      __attribute__((
          cleanup(simple_archiver_helper_cleanup_c_string))) char *path =
          malloc(path_length + 1);
      ret = read_buf_full_from_fd(in_f,
                                  (char *)buf,
                                  SIMPLE_ARCHIVER_BUFFER_SIZE,
                                  path_length + 1,
                                  path);
      if (ret != SDAS_SUCCESS) {
        return ret;
      }
      path[path_length] = 0;
      if (do_extract && !skip_due_to_map && !skip_due_to_invalid &&
          absolute_preferred) {
        if (state->parsed->prefix) {
          if (points_to_outside) {
            abs_path_prefixed = strdup(path);
          } else {
            abs_path_prefixed =
              simple_archiver_helper_insert_prefix_in_link_path(
                state->parsed->prefix, link_name, path);
          }
          if (!abs_path_prefixed) {
            fprintf(stderr,
                    "ERROR: Failed to insert prefix to absolute path!\n");
            return SDAS_INTERNAL_ERROR;
          }
        }
#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
        simple_archiver_helper_make_dirs_perms(
          link_name_prefixed ? link_name_prefixed : link_name,
          (state->parsed->flags & 0x2000)
            ? simple_archiver_internal_permissions_to_mode_t(
                state->parsed->dir_permissions)
            : (S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH),
          (state->parsed->flags & 0x400) ? state->parsed->uid : getuid(),
          (state->parsed->flags & 0x800) ? state->parsed->gid : getgid());
        int_fast8_t link_create_retry = 0;
      V1_SYMLINK_CREATE_RETRY_0:
        ret = symlink(
          abs_path_prefixed ? abs_path_prefixed : path,
          link_name_prefixed ? link_name_prefixed : link_name
        );
        if (ret == -1) {
          if (link_create_retry) {
            fprintf(
                stderr,
                "  WARNING: Failed to create symlink after removing existing "
                "symlink!\n");
            goto V1_SYMLINK_CREATE_AFTER_0;
          } else if (errno == EEXIST) {
            if ((state->parsed->flags & 8) == 0) {
              fprintf(stderr,
                      "  WARNING: Symlink already exists and "
                      "\"--overwrite-extract\" is not specified, skipping!\n");
              goto V1_SYMLINK_CREATE_AFTER_0;
            } else {
              fprintf(stderr,
                      "  NOTICE: Symlink already exists and "
                      "\"--overwrite-extract\" specified, attempting to "
                      "overwrite...\n");
              unlink(link_name_prefixed ? link_name_prefixed : link_name);
              link_create_retry = 1;
              goto V1_SYMLINK_CREATE_RETRY_0;
            }
          }
          return SDAS_FAILED_TO_EXTRACT_SYMLINK;
        }
        ret = fchmodat(AT_FDCWD,
                       link_name_prefixed ? link_name_prefixed : link_name,
                       permissions,
                       AT_SYMLINK_NOFOLLOW);
        if (ret == -1) {
          if (errno == EOPNOTSUPP) {
            fprintf(stderr,
                    "  NOTICE: Setting permissions of symlink is not supported "
                    "by FS/OS!\n");
          } else {
            fprintf(stderr,
                    "  WARNING: Failed to set permissions of symlink (%d)!\n",
                    errno);
          }
        }
        link_extracted = 1;
        fprintf(stderr,
                "  %s -> %s\n",
                link_name_prefixed ? link_name_prefixed : link_name,
                abs_path_prefixed ? abs_path_prefixed : path);
      V1_SYMLINK_CREATE_AFTER_0:
        link_create_retry = 1;
#endif
      } else if (!do_extract) {
        fprintf(stderr, "  Abs path: %s\n", path);
      }
    } else if (!do_extract) {
      fprintf(stderr, "  No Absolute path.\n");
    }

    __attribute__((cleanup(simple_archiver_helper_cleanup_c_string)))
    char *rel_path_prefixed = NULL;

    if (fread(buf, 1, 2, in_f) != 2) {
      return SDAS_INVALID_FILE;
    }
    memcpy(&u16, buf, 2);
    simple_archiver_helper_16_bit_be(&u16);
    if (u16 != 0) {
      const size_t path_length = u16;
      __attribute__((
          cleanup(simple_archiver_helper_cleanup_c_string))) char *path =
          malloc(path_length + 1);
      ret = read_buf_full_from_fd(in_f,
                                  (char *)buf,
                                  SIMPLE_ARCHIVER_BUFFER_SIZE,
                                  path_length + 1,
                                  path);
      if (ret != SDAS_SUCCESS) {
        return ret;
      }
      path[path_length] = 0;
      if (do_extract && !skip_due_to_map && !skip_due_to_invalid &&
          !absolute_preferred) {
        if (state->parsed->prefix) {
          rel_path_prefixed =
            simple_archiver_helper_insert_prefix_in_link_path(
              state->parsed->prefix, link_name, path);
          if (!rel_path_prefixed) {
            fprintf(stderr,
                    "ERROR: Failed to insert prefix to relative path!\n");
            return SDAS_INTERNAL_ERROR;
          }
        }
#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
        simple_archiver_helper_make_dirs_perms(
          link_name_prefixed ? link_name_prefixed : link_name,
          (state->parsed->flags & 0x2000)
            ? simple_archiver_internal_permissions_to_mode_t(
                state->parsed->dir_permissions)
            : (S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH),
          (state->parsed->flags & 0x400) ? state->parsed->uid : getuid(),
          (state->parsed->flags & 0x800) ? state->parsed->gid : getgid());
        int_fast8_t link_create_retry = 0;
      V1_SYMLINK_CREATE_RETRY_1:
        ret = symlink(
          rel_path_prefixed ? rel_path_prefixed : path,
          link_name_prefixed ? link_name_prefixed : link_name);
        if (ret == -1) {
          if (link_create_retry) {
            fprintf(
                stderr,
                "  WARNING: Failed to create symlink after removing existing "
                "symlink!\n");
            goto V1_SYMLINK_CREATE_AFTER_1;
          } else if (errno == EEXIST) {
            if ((state->parsed->flags & 8) == 0) {
              fprintf(stderr,
                      "  WARNING: Symlink already exists and "
                      "\"--overwrite-extract\" is not specified, skipping!\n");
              goto V1_SYMLINK_CREATE_AFTER_1;
            } else {
              fprintf(stderr,
                      "  NOTICE: Symlink already exists and "
                      "\"--overwrite-extract\" specified, attempting to "
                      "overwrite...\n");
              unlink(link_name_prefixed ? link_name_prefixed : link_name);
              link_create_retry = 1;
              goto V1_SYMLINK_CREATE_RETRY_1;
            }
          }
          return SDAS_FAILED_TO_EXTRACT_SYMLINK;
        }
        ret = fchmodat(AT_FDCWD,
                       link_name_prefixed ? link_name_prefixed : link_name,
                       permissions,
                       AT_SYMLINK_NOFOLLOW);
        if (ret == -1) {
          if (errno == EOPNOTSUPP) {
            fprintf(stderr,
                    "  NOTICE: Setting permissions of symlink is not supported "
                    "by FS/OS!\n");
          } else {
            fprintf(stderr,
                    "  WARNING: Failed to set permissions of symlink (%d)!\n",
                    errno);
          }
        }
        if (geteuid() == 0
            && (state->parsed->flags & 0x400 || state->parsed->flags & 0x800)) {
          ret = fchownat(
              AT_FDCWD,
              link_name_prefixed ? link_name_prefixed : link_name,
              state->parsed->flags & 0x400 ? state->parsed->uid : getuid(),
              state->parsed->flags & 0x800 ? state->parsed->gid : getgid(),
              AT_SYMLINK_NOFOLLOW);
          if (ret == -1) {
            fprintf(stderr,
                    "  WARNING: Failed to force set UID/GID of symlink \"%s\""
                    "(errno %d)!\n",
                    link_name_prefixed ? link_name_prefixed : link_name,
                    errno);
          }
        }
        link_extracted = 1;
        fprintf(stderr,
                "  %s -> %s\n",
                link_name_prefixed ? link_name_prefixed : link_name,
                rel_path_prefixed ? rel_path_prefixed : path);
      V1_SYMLINK_CREATE_AFTER_1:
        link_create_retry = 1;
#endif
      } else if (!do_extract) {
        fprintf(stderr, "  Rel path: %s\n", path);
      }
    } else if (!do_extract) {
      fprintf(stderr, "  No Relative path.\n");
    }

    if (do_extract && !link_extracted && !skip_due_to_map &&
        !skip_due_to_invalid) {
      fprintf(stderr, "  WARNING: Symlink \"%s\" was not created!\n",
              link_name);
    } else if (do_extract && link_extracted && !skip_due_to_map &&
               !skip_due_to_invalid && links_list) {
      simple_archiver_list_add(links_list, strdup(link_name), NULL);
    }
  }

  if (fread(buf, 1, 4, in_f) != 4) {
    return SDAS_INVALID_FILE;
  }
  memcpy(&u32, buf, 4);
  simple_archiver_helper_32_bit_be(&u32);

  const uint32_t chunk_count = u32;
  for (uint32_t chunk_idx = 0; chunk_idx < chunk_count; ++chunk_idx) {
    if (is_sig_int_occurred) {
      return SDAS_SIGINT;
    }
    fprintf(stderr,
            "CHUNK %3" PRIu32 " of %3" PRIu32 "\n",
            chunk_idx + 1,
            chunk_count);

    if (fread(buf, 1, 4, in_f) != 4) {
      return SDAS_INVALID_FILE;
    }
    memcpy(&u32, buf, 4);
    simple_archiver_helper_32_bit_be(&u32);

    const uint32_t file_count = u32;

    __attribute__((cleanup(simple_archiver_list_free)))
    SDArchiverLinkedList *file_info_list = simple_archiver_list_init();

    __attribute__((cleanup(cleanup_internal_file_info)))
    SDArchiverInternalFileInfo *file_info = NULL;

    for (uint32_t file_idx = 0; file_idx < file_count; ++file_idx) {
      file_info = malloc(sizeof(SDArchiverInternalFileInfo));
      memset(file_info, 0, sizeof(SDArchiverInternalFileInfo));

      if (fread(buf, 1, 2, in_f) != 2) {
        return SDAS_INVALID_FILE;
      }
      memcpy(&u16, buf, 2);
      simple_archiver_helper_16_bit_be(&u16);

      file_info->filename = malloc(u16 + 1);
      int ret =
          read_buf_full_from_fd(in_f, (char *)buf, SIMPLE_ARCHIVER_BUFFER_SIZE,
                                u16 + 1, file_info->filename);
      if (ret != SDAS_SUCCESS) {
        return ret;
      }
      file_info->filename[u16] = 0;

      if (simple_archiver_validate_file_path(file_info->filename)) {
        fprintf(stderr,
                "ERROR: File idx %" PRIu32 ": Invalid filename!\n",
                file_idx);
        file_info->other_flags |= 1;
      }

      if (state && state->parsed && (state->parsed->flags & 8) != 0) {
        int fd = open((const char *)buf, O_RDONLY | O_NOFOLLOW);
        if (fd == -1) {
          if (errno == ELOOP) {
            // Exists as a symlink.
            fprintf(stderr,
                    "WARNING: Filename \"%s\" already exists as symlink, "
                    "removing...\n",
                    (const char *)buf);
            unlink((const char *)buf);
          } else {
            // File doesn't exist, do nothing.
          }
        } else {
          close(fd);
          fprintf(stderr, "WARNING: File \"%s\" already exists, removing...\n",
                  (const char *)buf);
          unlink((const char *)buf);
        }
      }

      if (fread(file_info->bit_flags, 1, 4, in_f) != 4) {
        return SDAS_INVALID_FILE;
      }

      if (fread(buf, 1, 4, in_f) != 4) {
        return SDAS_INVALID_FILE;
      }
      memcpy(&u32, buf, 4);
      simple_archiver_helper_32_bit_be(&u32);
      if (state && (state->parsed->flags & 0x400)) {
        file_info->uid = state->parsed->uid;
      } else {
        if (state) {
          const char *mapped_uid_user = simple_archiver_hash_map_get(
            state->parsed->users_infos.UidToUname, &u32, sizeof(uint32_t));
          uint32_t remapped_uid;
          if (state->parsed->flags & 0x4000) {
            // Prefer UID first.
            if (simple_archiver_get_uid_mapping(state->parsed->mappings,
                                                state->parsed->users_infos,
                                                u32,
                                                &remapped_uid,
                                                NULL) == 0) {
              file_info->uid = remapped_uid;
            } else if (mapped_uid_user
                && simple_archiver_get_user_mapping(state->parsed->mappings,
                                                    state->parsed->users_infos,
                                                    mapped_uid_user,
                                                    &remapped_uid,
                                                    NULL) == 0) {
              file_info->uid = remapped_uid;
            } else {
              file_info->uid = u32;
            }
          } else {
            // Prefer Username first.
            if (mapped_uid_user
                && simple_archiver_get_user_mapping(state->parsed->mappings,
                                                    state->parsed->users_infos,
                                                    mapped_uid_user,
                                                    &remapped_uid,
                                                    NULL) == 0) {
              file_info->uid = remapped_uid;
            } else if (simple_archiver_get_uid_mapping(
                state->parsed->mappings,
                state->parsed->users_infos,
                u32,
                &remapped_uid,
                NULL) == 0) {
              file_info->uid = remapped_uid;
            } else {
              file_info->uid = u32;
            }
          }
        } else {
          file_info->uid = u32;
        }
      }

      if (fread(buf, 1, 4, in_f) != 4) {
        return SDAS_INVALID_FILE;
      }
      memcpy(&u32, buf, 4);
      simple_archiver_helper_32_bit_be(&u32);
      if (state && (state->parsed->flags & 0x800)) {
        file_info->gid = state->parsed->gid;
      } else {
        if (state) {
          // Check remappings.
          const char *mapped_gid_group = simple_archiver_hash_map_get(
            state->parsed->users_infos.GidToGname, &u32, sizeof(uint32_t));
          uint32_t remapped_gid;
          if (state->parsed->flags & 0x8000) {
            // Prefer GID first.
            if (simple_archiver_get_gid_mapping(state->parsed->mappings,
                                                state->parsed->users_infos,
                                                u32,
                                                &remapped_gid,
                                                NULL) == 0) {
              file_info->gid = remapped_gid;
            } else if (mapped_gid_group
                && simple_archiver_get_group_mapping(state->parsed->mappings,
                                                     state->parsed->users_infos,
                                                     mapped_gid_group,
                                                     &remapped_gid,
                                                     NULL) == 0) {
              file_info->gid = remapped_gid;
            } else {
              file_info->gid = u32;
            }
          } else {
            // Prefer Group first.
            if (mapped_gid_group
                && simple_archiver_get_group_mapping(state->parsed->mappings,
                                                     state->parsed->users_infos,
                                                     mapped_gid_group,
                                                     &remapped_gid,
                                                     NULL) == 0) {
              file_info->gid = remapped_gid;
            } else if (simple_archiver_get_gid_mapping(
                state->parsed->mappings,
                state->parsed->users_infos,
                u32,
                &remapped_gid,
                NULL) == 0) {
              file_info->gid = remapped_gid;
            } else {
              file_info->gid = u32;
            }
          }
        } else {
          file_info->gid = u32;
        }
      }

      if (fread(buf, 1, 8, in_f) != 8) {
        return SDAS_INVALID_FILE;
      }
      memcpy(&u64, buf, 8);
      simple_archiver_helper_64_bit_be(&u64);
      file_info->file_size = u64;

      if (files_map) {
        simple_archiver_internal_paths_to_files_map(files_map,
                                                    file_info->filename);
      }

      simple_archiver_list_add(file_info_list, file_info,
                               free_internal_file_info);
      file_info = NULL;
    }

    if (fread(buf, 1, 8, in_f) != 8) {
      return SDAS_INVALID_FILE;
    }
    memcpy(&u64, buf, 8);
    simple_archiver_helper_64_bit_be(&u64);

    const uint64_t chunk_size = u64;
    uint64_t chunk_remaining = chunk_size;
    uint64_t chunk_idx = 0;

    SDArchiverLLNode *node = file_info_list->head;
    uint16_t file_idx = 0;

#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
    if (is_compressed) {
      // Start the decompressing process and read into files.

      // Handle SIGPIPE.
      is_sig_pipe_occurred = 0;
      signal(SIGPIPE, handle_sig_pipe);

      int pipe_into_cmd[2];
      int pipe_outof_cmd[2];
      __attribute__((cleanup(
          simple_archiver_internal_cleanup_decomp_pid))) pid_t decompressor_pid;
      if (pipe(pipe_into_cmd) != 0) {
        // Unable to create pipes.
        break;
      } else if (pipe(pipe_outof_cmd) != 0) {
        // Unable to create second set of pipes.
        close(pipe_into_cmd[0]);
        close(pipe_into_cmd[1]);
        return SDAS_INTERNAL_ERROR;
      } else if (fcntl(pipe_into_cmd[1], F_SETFL, O_NONBLOCK) != 0) {
        // Unable to set non-blocking on into-write-pipe.
        close(pipe_into_cmd[0]);
        close(pipe_into_cmd[1]);
        close(pipe_outof_cmd[0]);
        close(pipe_outof_cmd[1]);
        return SDAS_INTERNAL_ERROR;
      } else if (fcntl(pipe_outof_cmd[0], F_SETFL, O_NONBLOCK) != 0) {
        // Unable to set non-blocking on outof-read-pipe.
        close(pipe_into_cmd[0]);
        close(pipe_into_cmd[1]);
        close(pipe_outof_cmd[0]);
        close(pipe_outof_cmd[1]);
        return SDAS_INTERNAL_ERROR;
      }

      if (state && state->parsed && state->parsed->decompressor) {
        if (simple_archiver_de_compress(pipe_into_cmd, pipe_outof_cmd,
                                        state->parsed->decompressor,
                                        &decompressor_pid) != 0) {
          // Failed to spawn compressor.
          close(pipe_into_cmd[1]);
          close(pipe_outof_cmd[0]);
          fprintf(stderr,
                  "WARNING: Failed to start decompressor cmd! Invalid cmd?\n");
          return SDAS_INTERNAL_ERROR;
        }
      } else {
        if (simple_archiver_de_compress(pipe_into_cmd, pipe_outof_cmd,
                                        decompressor_cmd,
                                        &decompressor_pid) != 0) {
          // Failed to spawn compressor.
          close(pipe_into_cmd[1]);
          close(pipe_outof_cmd[0]);
          fprintf(stderr,
                  "WARNING: Failed to start decompressor cmd! Invalid cmd?\n");
          return SDAS_INTERNAL_ERROR;
        }
      }

      // Close unnecessary pipe fds on this end of the transfer.
      close(pipe_into_cmd[0]);
      close(pipe_outof_cmd[1]);

      __attribute__((cleanup(
          simple_archiver_internal_cleanup_int_fd))) int pipe_outof_read =
          pipe_outof_cmd[0];
      __attribute__((cleanup(
          simple_archiver_internal_cleanup_int_fd))) int pipe_into_write =
          pipe_into_cmd[1];

      int decompressor_status;
      int decompressor_return_val;
      int decompressor_ret =
          waitpid(decompressor_pid, &decompressor_status, WNOHANG);
      if (decompressor_ret == decompressor_pid) {
        // Status is available.
        if (WIFEXITED(decompressor_status)) {
          decompressor_return_val = WEXITSTATUS(decompressor_status);
          fprintf(stderr,
                  "WARNING: Exec failed (exec exit code %d)! Invalid "
                  "decompressor cmd?\n",
                  decompressor_return_val);
          return SDAS_INTERNAL_ERROR;
        }
      } else if (decompressor_ret == 0) {
        // Probably still running, continue on.
      } else {
        // Error.
        fprintf(stderr,
                "WARNING: Exec failed (exec exit code unknown)! Invalid "
                "decompressor cmd?\n");
        return SDAS_INTERNAL_ERROR;
      }

      char hold_buf[SIMPLE_ARCHIVER_BUFFER_SIZE];
      ssize_t has_hold = -1;

      while (node->next != file_info_list->tail) {
        if (is_sig_int_occurred) {
          return SDAS_SIGINT;
        }
        node = node->next;
        const SDArchiverInternalFileInfo *file_info = node->data;
        fprintf(stderr,
                "  FILE %3" PRIu16 " of %3" PRIu32 ": %s\n",
                ++file_idx,
                file_count,
                file_info->filename);

        const size_t filename_length = strlen(file_info->filename);

        __attribute__((cleanup(simple_archiver_helper_cleanup_c_string)))
        char *filename_prefixed = NULL;
        if (state && state->parsed->prefix) {
          filename_prefixed = malloc(prefix_length + filename_length + 1);
          memcpy(filename_prefixed, state->parsed->prefix, prefix_length);
          memcpy(filename_prefixed + prefix_length, file_info->filename, filename_length + 1);
          filename_prefixed[prefix_length + filename_length] = 0;
        }

        uint_fast8_t skip_due_to_map = 0;
        if (working_files_map && simple_archiver_hash_map_get(
                                     working_files_map, file_info->filename,
                                     filename_length + 1) == NULL) {
          skip_due_to_map = 1;
          fprintf(stderr, "    Skipping not specified in args...\n");
        } else if ((file_info->other_flags & 1) != 0) {
          fprintf(stderr, "    Skipping invalid filename...\n");
        }

        if (do_extract && !skip_due_to_map &&
            (file_info->other_flags & 1) == 0) {
#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
          mode_t permissions;
          if (state->parsed->flags & 0x1000) {
            permissions =
              simple_archiver_internal_permissions_to_mode_t(
                state->parsed->file_permissions);
          } else {
            permissions = permissions_from_bits_version_1(
              file_info->bit_flags,
              0);
          }
#endif
          if ((state->parsed->flags & 8) == 0) {
            // Check if file already exists.
            __attribute__((cleanup(simple_archiver_helper_cleanup_FILE)))
            FILE *temp_fd = fopen(
              filename_prefixed ? filename_prefixed : file_info->filename,
              "r");
            if (temp_fd) {
              fprintf(stderr,
                      "  WARNING: File already exists and "
                      "\"--overwrite-extract\" is not specified, skipping!\n");
              int ret = read_decomp_to_out_file(
                  NULL, pipe_outof_read, (char *)buf,
                  SIMPLE_ARCHIVER_BUFFER_SIZE, file_info->file_size,
                  &pipe_into_write, &chunk_remaining, in_f, hold_buf,
                  &has_hold);
              if (ret != SDAS_SUCCESS) {
                return ret;
              }
              continue;
            }
          }

          simple_archiver_helper_make_dirs_perms(
            filename_prefixed ? filename_prefixed : file_info->filename,
            (state->parsed->flags & 0x2000)
              ? simple_archiver_internal_permissions_to_mode_t(
                  state->parsed->dir_permissions)
              : (S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH),
            (state->parsed->flags & 0x400)
              ? state->parsed->uid
              : file_info->uid,
            (state->parsed->flags & 0x800)
              ? state->parsed->gid
              : file_info->gid);
          int ret = read_decomp_to_out_file(
              filename_prefixed ? filename_prefixed : file_info->filename,
              pipe_outof_read,
              (char *)buf,
              SIMPLE_ARCHIVER_BUFFER_SIZE,
              file_info->file_size,
              &pipe_into_write,
              &chunk_remaining,
              in_f,
              hold_buf,
              &has_hold);
          if (ret != SDAS_SUCCESS) {
            return ret;
          }
#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
          if (chmod(filename_prefixed ? filename_prefixed : file_info->filename,
                    permissions)
                == -1) {
            return SDAS_INTERNAL_ERROR;
          } else if (geteuid() == 0 &&
                     chown(filename_prefixed
                             ? filename_prefixed
                             : file_info->filename,
                           file_info->uid,
                           file_info->gid) != 0) {
            fprintf(stderr,
                    "    ERROR Failed to set UID/GID of file \"%s\"!\n",
                    filename_prefixed
                      ? filename_prefixed
                      : file_info->filename);
            return SDAS_INTERNAL_ERROR;
          }
#endif
        } else if (!skip_due_to_map && (file_info->other_flags & 1) == 0) {
          fprintf(stderr, "    Permissions: ");
          permissions_from_bits_version_1(file_info->bit_flags, 1);
          fprintf(stderr,
                  "\n    UID: %" PRIu32 "\n    GID: %" PRIu32 "\n",
                  file_info->uid,
                  file_info->gid);
          if (is_compressed) {
            fprintf(stderr,
                    "    File size (uncompressed): %" PRIu64 "\n",
                    file_info->file_size);
          } else {
            fprintf(stderr,
                    "    File size: %" PRIu64 "\n",
                    file_info->file_size);
          }
          int ret = read_decomp_to_out_file(
              NULL, pipe_outof_read, (char *)buf, SIMPLE_ARCHIVER_BUFFER_SIZE,
              file_info->file_size, &pipe_into_write, &chunk_remaining, in_f,
              hold_buf, &has_hold);
          if (ret != SDAS_SUCCESS) {
            return ret;
          }
        } else {
          int ret = read_decomp_to_out_file(
              NULL, pipe_outof_read, (char *)buf, SIMPLE_ARCHIVER_BUFFER_SIZE,
              file_info->file_size, &pipe_into_write, &chunk_remaining, in_f,
              hold_buf, &has_hold);
          if (ret != SDAS_SUCCESS) {
            return ret;
          }
        }
      }

      // Ensure EOF is left from pipe.
      ssize_t read_ret =
          read(pipe_outof_read, buf, SIMPLE_ARCHIVER_BUFFER_SIZE);
      if (read_ret > 0) {
        fprintf(stderr, "WARNING decompressor didn't reach EOF!\n");
      }
    } else {
#else
    // } (This comment exists so that vim can correctly match curly-braces).
    if (!is_compressed) {
#endif
      while (node->next != file_info_list->tail) {
        if (is_sig_int_occurred) {
          return SDAS_SIGINT;
        }
        node = node->next;
        const SDArchiverInternalFileInfo *file_info = node->data;
        fprintf(stderr,
                "  FILE %3" PRIu16 " of %3" PRIu32 ": %s\n",
                ++file_idx,
                file_count,
                file_info->filename);
        chunk_idx += file_info->file_size;
        if (chunk_idx > chunk_size) {
          fprintf(stderr, "ERROR Files in chunk is larger than chunk!\n");
          return SDAS_INTERNAL_ERROR;
        }

        const size_t filename_length = strlen(file_info->filename);

        __attribute__((cleanup(simple_archiver_helper_cleanup_c_string)))
        char *filename_prefixed = NULL;
        if (state && state->parsed->prefix) {
          filename_prefixed = malloc(prefix_length + filename_length + 1);
          memcpy(filename_prefixed, state->parsed->prefix, prefix_length);
          memcpy(filename_prefixed + prefix_length, file_info->filename, filename_length + 1);
          filename_prefixed[prefix_length + filename_length] = 0;
        }

        uint_fast8_t skip_due_to_map = 0;
        if (working_files_map && simple_archiver_hash_map_get(
                                     working_files_map, file_info->filename,
                                     filename_length + 1) == NULL) {
          skip_due_to_map = 1;
          fprintf(stderr, "    Skipping not specified in args...\n");
        } else if (file_info->other_flags & 1) {
          fprintf(stderr, "    Skipping invalid filename...\n");
        }

        if (do_extract && !skip_due_to_map &&
            (file_info->other_flags & 1) == 0) {
#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
          mode_t permissions;

          if (state->parsed->flags & 0x1000) {
            permissions = simple_archiver_internal_permissions_to_mode_t(
              state->parsed->file_permissions);
            fprintf(stderr,
                    "NOTICE: Forcing permissions as specified by "
                    "\"--force-file-permissions\"!\n");
          } else {
            permissions =
              permissions_from_bits_version_1(file_info->bit_flags, 0);
          }
#endif
          if ((state->parsed->flags & 8) == 0) {
            // Check if file already exists.
            __attribute__((cleanup(simple_archiver_helper_cleanup_FILE)))
            FILE *temp_fd = fopen(filename_prefixed
                                    ? filename_prefixed
                                    : file_info->filename,
                                  "r");
            if (temp_fd) {
              fprintf(stderr,
                      "  WARNING: File already exists and "
                      "\"--overwrite-extract\" is not specified, skipping!\n");
              int ret = read_buf_full_from_fd(in_f, (char *)buf,
                                              SIMPLE_ARCHIVER_BUFFER_SIZE,
                                              file_info->file_size, NULL);
              if (ret != SDAS_SUCCESS) {
                return ret;
              }
              continue;
            }
          }
          simple_archiver_helper_make_dirs_perms(
            filename_prefixed ? filename_prefixed : file_info->filename,
            (state->parsed->flags & 0x2000)
              ? simple_archiver_internal_permissions_to_mode_t(
                  state->parsed->dir_permissions)
              : (S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH),
            (state->parsed->flags & 0x400)
              ? state->parsed->uid
              : file_info->uid,
            (state->parsed->flags & 0x800)
              ? state->parsed->gid
              : file_info->gid);
          __attribute__((cleanup(simple_archiver_helper_cleanup_FILE)))
          FILE *out_fd = fopen(filename_prefixed
                                 ? filename_prefixed
                                 : file_info->filename,
                               "wb");
          int ret = read_fd_to_out_fd(in_f, out_fd, (char *)buf,
                                      SIMPLE_ARCHIVER_BUFFER_SIZE,
                                      file_info->file_size);
          if (ret != SDAS_SUCCESS) {
            return ret;
          }
          simple_archiver_helper_cleanup_FILE(&out_fd);
#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
          if (chmod(filename_prefixed ? filename_prefixed : file_info->filename,
                    permissions) == -1) {
            fprintf(stderr,
                    "ERROR Failed to set permissions of file \"%s\"!\n",
                    filename_prefixed
                      ? filename_prefixed
                      : file_info->filename);
            return SDAS_INTERNAL_ERROR;
          } else if (geteuid() == 0 &&
                     chown(filename_prefixed
                             ? filename_prefixed
                             : file_info->filename,
                           file_info->uid,
                           file_info->gid) != 0) {
            fprintf(stderr,
                    "    ERROR Failed to set UID/GID of file \"%s\"!\n",
                    file_info->filename);
            return SDAS_INTERNAL_ERROR;
          }
#endif
        } else if (!skip_due_to_map && (file_info->other_flags & 1) == 0) {
          fprintf(stderr, "    Permissions:");
          permissions_from_bits_version_1(file_info->bit_flags, 1);
          fprintf(stderr,
                  "\n    UID: %" PRIu32 "\n    GID: %" PRIu32 "\n",
                  file_info->uid,
                  file_info->gid);
          if (is_compressed) {
            fprintf(stderr,
                    "    File size (compressed): %" PRIu64 "\n",
                    file_info->file_size);
          } else {
            fprintf(stderr,
                    "    File size: %" PRIu64 "\n",
                    file_info->file_size);
          }
          int ret = read_buf_full_from_fd(in_f, (char *)buf,
                                          SIMPLE_ARCHIVER_BUFFER_SIZE,
                                          file_info->file_size, NULL);
          if (ret != SDAS_SUCCESS) {
            return ret;
          }
        } else {
          int ret = read_buf_full_from_fd(in_f, (char *)buf,
                                          SIMPLE_ARCHIVER_BUFFER_SIZE,
                                          file_info->file_size, NULL);
          if (ret != SDAS_SUCCESS) {
            return ret;
          }
        }
      }
    }
  }

  if (do_extract && links_list && files_map) {
    simple_archiver_safe_links_enforce(links_list, files_map);
  }

  return SDAS_SUCCESS;
}

int simple_archiver_parse_archive_version_2(FILE *in_f, int_fast8_t do_extract,
                                            const SDArchiverState *state) {
  int ret = simple_archiver_parse_archive_version_1(in_f, do_extract, state);
  if (ret != SDAS_SUCCESS) {
    return ret;
  }

  uint32_t u32;
  if (fread(&u32, 4, 1, in_f) != 1) {
    fprintf(stderr, "ERROR: Failed to read directory count!\n");
    return SDAS_INTERNAL_ERROR;
  }

  simple_archiver_helper_32_bit_be(&u32);

  uint16_t u16;
  __attribute__((cleanup(simple_archiver_helper_cleanup_c_string)))
  char *buf = NULL;

  const uint32_t size = u32;
  for (uint32_t idx = 0; idx < size; ++idx) {
    if (fread(&u16, 2, 1, in_f) != 1) {
      fprintf(stderr, "ERROR: Failed to read directory name length!\n");
      return SDAS_INTERNAL_ERROR;
    }

    simple_archiver_helper_16_bit_be(&u16);
    simple_archiver_helper_cleanup_c_string(&buf);
    buf = malloc(u16 + 1);

    if (fread(buf, 1, u16 + 1, in_f) != (size_t)u16 + 1) {
      fprintf(stderr, "ERROR: Failed to read directory name!\n");
      return SDAS_INTERNAL_ERROR;
    }

    buf[u16] = 0;

    uint8_t perms_flags[4];
    if (fread(perms_flags, 1, 2, in_f) != 2) {
      fprintf(stderr,
              "ERROR: Failed to read permission flags for \"%s\"!\n",
              buf);
      return SDAS_INTERNAL_ERROR;
    }
    perms_flags[2] = 0;
    perms_flags[3] = 0;

    uint32_t uid;
    if (fread(&uid, 4, 1, in_f) != 1) {
      fprintf(stderr,
              "ERROR: Failed to read UID for \"%s\"!\n", buf);
      return SDAS_INTERNAL_ERROR;
    }
    simple_archiver_helper_32_bit_be(&uid);

    uint32_t gid;
    if (fread(&gid, 4, 1, in_f) != 1) {
      fprintf(stderr,
              "ERROR: Failed to read GID for \"%s\"!\n", buf);
      return SDAS_INTERNAL_ERROR;
    }
    simple_archiver_helper_32_bit_be(&gid);

    if (do_extract) {
      fprintf(stderr, "Creating dir \"%s\"\n", buf);
    } else {
      fprintf(stderr, "Dir entry \"%s\"\n", buf);
      fprintf(stderr, "  Permissions: ");
      fprintf(stderr, "%s", (perms_flags[0] & 1)    ? "r" : "-");
      fprintf(stderr, "%s", (perms_flags[0] & 2)    ? "w" : "-");
      fprintf(stderr, "%s", (perms_flags[0] & 4)    ? "x" : "-");
      fprintf(stderr, "%s", (perms_flags[0] & 8)    ? "r" : "-");
      fprintf(stderr, "%s", (perms_flags[0] & 0x10) ? "w" : "-");
      fprintf(stderr, "%s", (perms_flags[0] & 0x20) ? "x" : "-");
      fprintf(stderr, "%s", (perms_flags[0] & 0x40) ? "r" : "-");
      fprintf(stderr, "%s", (perms_flags[0] & 0x80) ? "w" : "-");
      fprintf(stderr, "%s", (perms_flags[1] & 1)    ? "x" : "-");
      fprintf(stderr, "\n");

      fprintf(stderr,
              "  UID: %" PRIu32 ", GID: %" PRIu32 "\n",
              uid,
              gid);
    }

    if (state) {
      // Check uid remapping.
      const char *mapped_uid_user = simple_archiver_hash_map_get(
        state->parsed->users_infos.UidToUname, &uid, sizeof(uint32_t));
      uint32_t remapped_uid;
      if (state->parsed->flags & 0x4000) {
        // Prefer UID first.
        if (simple_archiver_get_uid_mapping(state->parsed->mappings,
                                            state->parsed->users_infos,
                                            uid,
                                            &remapped_uid,
                                            NULL) == 0) {
          uid = remapped_uid;
        } else if (mapped_uid_user
            && simple_archiver_get_user_mapping(state->parsed->mappings,
                                                state->parsed->users_infos,
                                                mapped_uid_user,
                                                &remapped_uid,
                                                NULL) == 0) {
          uid = remapped_uid;
        }
      } else {
        // Prefer Username first.
        if (mapped_uid_user
            && simple_archiver_get_user_mapping(state->parsed->mappings,
                                                state->parsed->users_infos,
                                                mapped_uid_user,
                                                &remapped_uid,
                                                NULL) == 0) {
          uid = remapped_uid;
        } else if (simple_archiver_get_uid_mapping(state->parsed->mappings,
                                                   state->parsed->users_infos,
                                                   uid,
                                                   &remapped_uid,
                                                   NULL) == 0) {
          uid = remapped_uid;
        }
      }

      // Check GID remapping.
      const char *mapped_gid_group = simple_archiver_hash_map_get(
        state->parsed->users_infos.GidToGname, &gid, sizeof(uint32_t));
      uint32_t remapped_gid;
      if (state->parsed->flags & 0x8000) {
        // Prefer GID first.
        if (simple_archiver_get_gid_mapping(state->parsed->mappings,
                                            state->parsed->users_infos,
                                            gid,
                                            &remapped_gid,
                                            NULL) == 0) {
          gid = remapped_gid;
        } else if (mapped_gid_group
            && simple_archiver_get_group_mapping(state->parsed->mappings,
                                                 state->parsed->users_infos,
                                                 mapped_gid_group,
                                                 &remapped_gid,
                                                 NULL) == 0) {
          gid = remapped_gid;
        }
      } else {
        // Prefer Group first.
        if (mapped_gid_group
            && simple_archiver_get_group_mapping(state->parsed->mappings,
                                                 state->parsed->users_infos,
                                                 mapped_gid_group,
                                                 &remapped_gid,
                                                 NULL) == 0) {
          gid = remapped_gid;
        } else if (simple_archiver_get_gid_mapping(state->parsed->mappings,
                                                   state->parsed->users_infos,
                                                   gid,
                                                   &remapped_gid,
                                                   NULL) == 0) {
          gid = remapped_gid;
        }
      }
    }

    __attribute__((cleanup(simple_archiver_helper_cleanup_c_string)))
    char *abs_path_dir = realpath(".", NULL);
    if (!abs_path_dir) {
      fprintf(
        stderr,
        "ERROR: Failed to get abs_path_dir of current working directory!\n");
      return SDAS_INTERNAL_ERROR;
    }

    __attribute__((cleanup(simple_archiver_helper_string_parts_free)))
    SAHelperStringParts string_parts =
      simple_archiver_helper_string_parts_init();

    simple_archiver_helper_string_parts_add(string_parts, abs_path_dir);

    if (abs_path_dir[strlen(abs_path_dir) - 1] != '/') {
      simple_archiver_helper_string_parts_add(string_parts, "/");
    }

    if (state && state->parsed->prefix) {
      simple_archiver_helper_string_parts_add(string_parts,
                                              state->parsed->prefix);
    }

    simple_archiver_helper_string_parts_add(string_parts, buf);

    __attribute__((cleanup(simple_archiver_helper_cleanup_c_string)))
    char *abs_dir_path =
      simple_archiver_helper_string_parts_combine(string_parts);

    simple_archiver_helper_string_parts_add(string_parts, "/UNUSED");

    __attribute__((cleanup(simple_archiver_helper_cleanup_c_string)))
    char *abs_dir_path_with_suffix =
      simple_archiver_helper_string_parts_combine(string_parts);

    if (do_extract) {
      int ret = simple_archiver_helper_make_dirs_perms(
        abs_dir_path_with_suffix,
        state && (state->parsed->flags & 0x2000)
          ? simple_archiver_internal_permissions_to_mode_t(
              state->parsed->dir_permissions)
          : (S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH),
        state && (state->parsed->flags & 0x400) ? state->parsed->uid : uid,
        state && (state->parsed->flags & 0x800) ? state->parsed->gid : gid);
      if (ret != 0) {
        fprintf(stderr, "ERROR: Failed to make dirs (%d)!\n", ret);
        return SDAS_INTERNAL_ERROR;
      }
#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
      mode_t perms = simple_archiver_internal_bits_to_mode_t(perms_flags);
      ret = chmod(abs_dir_path,
                  state && (state->parsed->flags & 0x10000)
                    ? simple_archiver_internal_permissions_to_mode_t(
                        state->parsed->empty_dir_permissions)
                    : perms);
      if (ret != 0) {
        fprintf(stderr,
                "WARNING: Failed to set permissions on dir \"%s\"!\n",
                abs_dir_path);
      }
#endif
    }
  }

  return SDAS_SUCCESS;
}

int simple_archiver_parse_archive_version_3(FILE *in_f,
                                            int_fast8_t do_extract,
                                            const SDArchiverState *state) {
  uint8_t buf[SIMPLE_ARCHIVER_BUFFER_SIZE];
  uint16_t u16;
  uint32_t u32;
  uint64_t u64;

  __attribute__((cleanup(simple_archiver_hash_map_free)))
  SDArchiverHashMap *working_files_map = NULL;
  if (do_extract && state && state->parsed->working_files &&
      state->parsed->working_files[0] != NULL) {
    working_files_map = simple_archiver_hash_map_init();
    for (char **iter = state->parsed->working_files; *iter != NULL; ++iter) {
      size_t len = strlen(*iter) + 1;
      char *key = malloc(len);
      memcpy(key, *iter, len);
      key[len - 1] = 0;
      simple_archiver_hash_map_insert(
          working_files_map, key, key, len,
          simple_archiver_helper_datastructure_cleanup_nop, NULL);
    }
  }

  if (fread(buf, 1, 4, in_f) != 4) {
    return SDAS_INVALID_FILE;
  }

  if (do_extract && state->parsed->user_cwd) {
#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
    if (chdir(state->parsed->user_cwd)) {
      return SDAS_FAILED_TO_CHANGE_CWD;
    }
#endif
  }

  __attribute__((cleanup(simple_archiver_list_free)))
  SDArchiverLinkedList *links_list =
      do_extract && state && state->parsed && state->parsed->flags & 0x80
          ? NULL
          : simple_archiver_list_init();
  __attribute__((cleanup(simple_archiver_hash_map_free)))
  SDArchiverHashMap *files_map =
      do_extract && state && state->parsed && state->parsed->flags & 0x80
          ? NULL
          : simple_archiver_hash_map_init();

  __attribute__((
      cleanup(simple_archiver_helper_cleanup_c_string))) char *cwd_realpath =
      realpath(".", NULL);

  const int_fast8_t is_compressed = (buf[0] & 1) ? 1 : 0;

  __attribute__((cleanup(
      simple_archiver_helper_cleanup_c_string))) char *compressor_cmd = NULL;
  __attribute__((cleanup(
      simple_archiver_helper_cleanup_c_string))) char *decompressor_cmd = NULL;

  if (is_compressed) {
    if (fread(buf, 1, 2, in_f) != 2) {
      return SDAS_INVALID_FILE;
    }
    memcpy(&u16, buf, 2);
    simple_archiver_helper_16_bit_be(&u16);
    compressor_cmd = malloc(u16 + 1);
    int ret =
        read_buf_full_from_fd(in_f, (char *)buf, SIMPLE_ARCHIVER_BUFFER_SIZE,
                              u16 + 1, compressor_cmd);
    if (ret != SDAS_SUCCESS) {
      return ret;
    }
    compressor_cmd[u16] = 0;

    fprintf(stderr, "Compressor command: %s\n", compressor_cmd);

    if (fread(buf, 1, 2, in_f) != 2) {
      return SDAS_INVALID_FILE;
    }
    memcpy(&u16, buf, 2);
    simple_archiver_helper_16_bit_be(&u16);
    decompressor_cmd = malloc(u16 + 1);
    ret = read_buf_full_from_fd(in_f, (char *)buf, SIMPLE_ARCHIVER_BUFFER_SIZE,
                                u16 + 1, decompressor_cmd);
    if (ret != SDAS_SUCCESS) {
      return ret;
    }
    decompressor_cmd[u16] = 0;

    fprintf(stderr, "Decompressor command: %s\n", decompressor_cmd);
    if (state && state->parsed && state->parsed->decompressor) {
      fprintf(stderr, "Overriding decompressor with: %s\n",
              state->parsed->decompressor);
    }
  }

  if (is_sig_int_occurred) {
    return SDAS_SIGINT;
  }

  const size_t prefix_length = state && state->parsed->prefix
                               ? strlen(state->parsed->prefix)
                               : 0;

  // Link count.
  if (fread(buf, 1, 4, in_f) != 4) {
    return SDAS_INVALID_FILE;
  }
  memcpy(&u32, buf, 4);
  simple_archiver_helper_32_bit_be(&u32);

  const uint32_t count = u32;
  for (uint32_t idx = 0; idx < count; ++idx) {
    if (is_sig_int_occurred) {
      return SDAS_SIGINT;
    }
    if (fread(buf, 1, 2, in_f) != 2) {
      return SDAS_INVALID_FILE;
    }
    const uint_fast8_t absolute_preferred = (buf[0] & 1) ? 1 : 0;
    const uint_fast8_t is_invalid = (buf[1] & 4) ? 1 : 0;
    const uint_fast8_t points_to_outside = (buf[1] & 8) ? 1 : 0;

#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
    mode_t permissions = permissions_from_bits_v1_symlink(buf, 0);
#endif

    uint_fast8_t link_extracted = 0;
    uint_fast8_t skip_due_to_map = 0;
    uint_fast8_t skip_due_to_invalid = is_invalid ? 1 : 0;

    if (fread(&u16, 2, 1, in_f) != 1) {
      return SDAS_INVALID_FILE;
    }
    simple_archiver_helper_16_bit_be(&u16);

    const size_t link_name_length = u16;

    __attribute__((cleanup(simple_archiver_helper_cleanup_c_string)))
    char *link_name = malloc(link_name_length + 1);

    int ret = read_buf_full_from_fd(in_f,
                                    (char *)buf,
                                    SIMPLE_ARCHIVER_BUFFER_SIZE,
                                    link_name_length + 1,
                                    link_name);
    if (ret != SDAS_SUCCESS) {
      return ret;
    }
    link_name[link_name_length] = 0;

    const uint_fast8_t lists_allowed = simple_archiver_helper_string_allowed_lists(
      link_name,
      state->parsed->whitelist_contains,
      state->parsed->whitelist_begins,
      state->parsed->whitelist_ends,
      state->parsed->blacklist_contains,
      state->parsed->blacklist_begins,
      state->parsed->blacklist_ends);

    if (!do_extract && lists_allowed) {
      fprintf(stderr, "SYMLINK %3" PRIu32 " of %3" PRIu32 "\n", idx + 1, count);
      if (is_invalid) {
        fprintf(stderr, "  WARNING: This symlink entry was marked invalid!\n");
      }
      fprintf(stderr, "  Link name: %s\n", link_name);
      if (absolute_preferred) {
        fprintf(stderr, "  Absolute path preferred.\n");
      } else {
        fprintf(stderr, "  Relative path preferred.\n");
      }
#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
      fprintf(stderr, "  Link Permissions: ");
      print_permissions(permissions);
      fprintf(stderr, "\n");
#endif
    } else if (do_extract && lists_allowed) {
      if (is_invalid) {
        fprintf(stderr, "  WARNING: This symlink entry was marked invalid!\n");
      }
    }

    __attribute__((cleanup(simple_archiver_helper_cleanup_c_string)))
    char *link_name_prefixed = NULL;
    if (do_extract && state && state->parsed->prefix) {
      link_name_prefixed = malloc(prefix_length + link_name_length + 1);
      memcpy(link_name_prefixed, state->parsed->prefix, prefix_length);
      memcpy(link_name_prefixed + prefix_length, link_name, link_name_length + 1);
      link_name_prefixed[prefix_length + link_name_length] = 0;
    }

    if (simple_archiver_validate_file_path(link_name)) {
      if (lists_allowed) {
        fprintf(stderr, "  WARNING: Invalid link name \"%s\"!\n", link_name);
      }
      skip_due_to_invalid = 1;
    }

    if (working_files_map &&
        simple_archiver_hash_map_get(working_files_map, link_name, u16 + 1) ==
            NULL) {
      skip_due_to_map = 1;
      fprintf(stderr, "  Skipping not specified in args...\n");
    }

    if (fread(&u16, 2, 1, in_f) != 1) {
      return SDAS_INVALID_FILE;
    }
    simple_archiver_helper_16_bit_be(&u16);

    __attribute__((cleanup(simple_archiver_helper_cleanup_c_string)))
    char *abs_path_prefixed = NULL;

    __attribute__((cleanup(simple_archiver_helper_cleanup_c_string)))
    char *parsed_abs_path = NULL;
    if (u16 != 0) {
      const size_t path_length = u16;
      parsed_abs_path = malloc(path_length + 1);
      ret = read_buf_full_from_fd(in_f,
                                  (char *)buf,
                                  SIMPLE_ARCHIVER_BUFFER_SIZE,
                                  path_length + 1,
                                  parsed_abs_path);
      if (ret != SDAS_SUCCESS) {
        return ret;
      }
      parsed_abs_path[path_length] = 0;
      if (!do_extract && lists_allowed) {
        fprintf(stderr, "  Abs path: %s\n", parsed_abs_path);
      }

      if (do_extract && state && state->parsed->prefix) {
        if (points_to_outside) {
          abs_path_prefixed = strdup(parsed_abs_path);
        } else {
          abs_path_prefixed =
            simple_archiver_helper_insert_prefix_in_link_path(
              state->parsed->prefix, link_name, parsed_abs_path);
        }
        if (!abs_path_prefixed) {
          fprintf(stderr,
                  "ERROR: Failed to insert prefix to absolute path!\n");
          return SDAS_INTERNAL_ERROR;
        }
      }
    } else if (!do_extract && lists_allowed) {
      fprintf(stderr, "  No Absolute path.\n");
    }

    if (fread(buf, 1, 2, in_f) != 2) {
      return SDAS_INVALID_FILE;
    }
    memcpy(&u16, buf, 2);
    simple_archiver_helper_16_bit_be(&u16);

    __attribute__((cleanup(simple_archiver_helper_cleanup_c_string)))
    char *rel_path_prefixed = NULL;

    __attribute__((cleanup(simple_archiver_helper_cleanup_c_string)))
    char *parsed_rel_path = NULL;
    if (u16 != 0) {
      const size_t path_length = u16;
      parsed_rel_path = malloc(path_length + 1);
      ret = read_buf_full_from_fd(in_f,
                                  (char *)buf,
                                  SIMPLE_ARCHIVER_BUFFER_SIZE,
                                  path_length + 1,
                                  parsed_rel_path);
      if (ret != SDAS_SUCCESS) {
        return ret;
      }
      parsed_rel_path[path_length] = 0;
      if (!do_extract && lists_allowed) {
        fprintf(stderr, "  Rel path: %s\n", parsed_rel_path);
      }

      if (do_extract && state && state->parsed->prefix) {
        rel_path_prefixed =
          simple_archiver_helper_insert_prefix_in_link_path(
            state->parsed->prefix, link_name, parsed_rel_path);
        if (!rel_path_prefixed) {
          fprintf(stderr,
                  "ERROR: Failed to insert prefix to relative path!\n");
          return SDAS_INTERNAL_ERROR;
        }
      }
    } else if (!do_extract && lists_allowed) {
      fprintf(stderr, "  No Relative path.\n");
    }

    if (fread(&u32, 4, 1, in_f) != 1) {
      fprintf(stderr, "  ERROR: Failed to read UID for symlink!\n");
      return SDAS_INVALID_FILE;
    }
    simple_archiver_helper_32_bit_be(&u32);

    uint32_t uid = u32;
    if (lists_allowed) {
      fprintf(stderr, "  UID: %" PRIu32 "\n", uid);
    }

    if (fread(&u32, 4, 1, in_f) != 1) {
      fprintf(stderr, "  ERROR: Failed to read GID for symlink!\n");
      return SDAS_INVALID_FILE;
    }
    simple_archiver_helper_32_bit_be(&u32);

    uint32_t gid = u32;
    if (lists_allowed) {
      fprintf(stderr, "  GID: %" PRIu32 "\n", gid);
    }

    if (fread(&u16, 2, 1, in_f) != 1) {
      fprintf(stderr, "  ERROR: Failed to read Username length for symlink!\n");
      return SDAS_INVALID_FILE;
    }
    simple_archiver_helper_16_bit_be(&u16);

    __attribute__((cleanup(simple_archiver_helper_cleanup_c_string)))
    char *username = malloc(u16 + 1);

    if (u16 != 0) {
      if (fread(username, 1, u16 + 1, in_f) != (size_t)u16 + 1) {
        fprintf(stderr, "  ERROR: Failed to read Username for symlink!\n");
        return SDAS_INVALID_FILE;
      }
      username[u16] = 0;
      if (lists_allowed) {
        fprintf(stderr, "  Username: %s\n", username);
      }
    } else {
      free(username);
      username = NULL;
      if (lists_allowed) {
        fprintf(stderr, "  Username does not exist for this link\n");
      }
    }

    uint32_t *username_uid_mapped = NULL;
    if (do_extract && state && username) {
      username_uid_mapped = simple_archiver_hash_map_get(
        state->parsed->users_infos.UnameToUid,
        username,
        u16 + 1);
    }

    __attribute__((cleanup(simple_archiver_helper_cleanup_uint32)))
    uint32_t *uid_remapped = NULL;
    __attribute__((cleanup(simple_archiver_helper_cleanup_uint32)))
    uint32_t *user_remapped_uid = NULL;
    uint32_t current_uid = uid;
    if (do_extract && state) {
      if ((state->parsed->flags & 0x4000) == 0 && username_uid_mapped) {
        current_uid = *username_uid_mapped;
      }

      uint32_t out_uid;
      if (simple_archiver_get_uid_mapping(state->parsed->mappings,
                                          state->parsed->users_infos,
                                          uid,
                                          &out_uid,
                                          NULL) == 0) {
        uid_remapped = malloc(sizeof(uint32_t));
        *uid_remapped = out_uid;
      }
      if (username
          && simple_archiver_get_user_mapping(state->parsed->mappings,
                                              state->parsed->users_infos,
                                              username,
                                              &out_uid,
                                              NULL) == 0) {
        user_remapped_uid = malloc(sizeof(uint32_t));
        *user_remapped_uid = out_uid;
      }

      if (state->parsed->flags & 0x4000) {
        if (uid_remapped) {
          current_uid = *uid_remapped;
        } else if (user_remapped_uid) {
          current_uid = *user_remapped_uid;
        }
      } else {
        if (user_remapped_uid) {
          current_uid = *user_remapped_uid;
        } else if (uid_remapped) {
          current_uid = *uid_remapped;
        }
      }
    }

    if (fread(&u16, 2, 1, in_f) != 1) {
      fprintf(stderr,
              "  ERROR: Failed to read Groupname length for symlink!\n");
      return SDAS_INVALID_FILE;
    }
    simple_archiver_helper_16_bit_be(&u16);

    __attribute__((cleanup(simple_archiver_helper_cleanup_c_string)))
    char *groupname = malloc(u16 + 1);

    if (u16 != 0) {
      if (fread(groupname, 1, u16 + 1, in_f) != (size_t)u16 + 1) {
        fprintf(stderr, "  ERROR: Failed to read Groupname for symlink!\n");
        return SDAS_INVALID_FILE;
      }
      groupname[u16] = 0;
      if (lists_allowed) {
        fprintf(stderr, "  Groupname: %s\n", groupname);
      }
    } else {
      free(groupname);
      groupname = NULL;
      if (lists_allowed) {
        fprintf(stderr, "  Groupname does not exist for this link\n");
      }
    }

    uint32_t *group_gid_mapped = NULL;
    if (do_extract && state && groupname) {
      group_gid_mapped = simple_archiver_hash_map_get(
        state->parsed->users_infos.GnameToGid,
        groupname,
        u16 + 1);
    }

    __attribute__((cleanup(simple_archiver_helper_cleanup_uint32)))
    uint32_t *gid_remapped = NULL;
    __attribute__((cleanup(simple_archiver_helper_cleanup_uint32)))
    uint32_t *group_remapped_gid = NULL;
    uint32_t current_gid = gid;
    if (do_extract && state) {
      if ((state->parsed->flags & 0x8000) == 0 && group_gid_mapped) {
        current_gid = *group_gid_mapped;
      }

      uint32_t out_gid;
      if (simple_archiver_get_gid_mapping(state->parsed->mappings, state->parsed->users_infos, gid, &out_gid, NULL) == 0) {
        gid_remapped = malloc(sizeof(uint32_t));
        *gid_remapped = out_gid;
      }
      if (groupname
          && simple_archiver_get_group_mapping(state->parsed->mappings,
                                               state->parsed->users_infos,
                                               groupname,
                                               &out_gid,
                                               NULL) == 0) {
        group_remapped_gid = malloc(sizeof(uint32_t));
        *group_remapped_gid = out_gid;
      }

      if (state->parsed->flags & 0x8000) {
        if (gid_remapped) {
          current_gid = *gid_remapped;
        } else if (group_remapped_gid) {
          current_gid = *group_remapped_gid;
        }
      } else {
        if (group_remapped_gid) {
          current_gid = *group_remapped_gid;
        } else if (gid_remapped) {
          current_gid = *gid_remapped;
        }
      }
    }

    if (do_extract
        && !skip_due_to_map
        && !skip_due_to_invalid
        && lists_allowed
        && absolute_preferred
        && parsed_abs_path) {
#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
  SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
  SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
      simple_archiver_helper_make_dirs_perms(
        link_name_prefixed ? link_name_prefixed : link_name,
        (state->parsed->flags & 0x2000)
          ? simple_archiver_internal_permissions_to_mode_t(
              state->parsed->dir_permissions)
          : (S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH),
        (state->parsed->flags & 0x400) ? state->parsed->uid : current_uid,
        (state->parsed->flags & 0x800) ? state->parsed->gid : current_gid);
      int_fast8_t link_create_retry = 0;
    V3_SYMLINK_CREATE_RETRY_0:
      ret = symlink(
        abs_path_prefixed ? abs_path_prefixed : parsed_abs_path,
        link_name_prefixed ? link_name_prefixed : link_name);
      if (ret == -1) {
        if (link_create_retry) {
          fprintf(
              stderr,
              "  WARNING: Failed to create symlink after removing existing "
              "symlink!\n");
          goto V3_SYMLINK_CREATE_AFTER_0;
        } else if (errno == EEXIST) {
          if ((state->parsed->flags & 8) == 0) {
            fprintf(stderr,
                    "  WARNING: Symlink already exists and "
                    "\"--overwrite-extract\" is not specified, skipping!\n");
            goto V3_SYMLINK_CREATE_AFTER_0;
          } else {
            fprintf(stderr,
                    "  NOTICE: Symlink already exists and "
                    "\"--overwrite-extract\" specified, attempting to "
                    "overwrite...\n");
            unlink(link_name_prefixed ? link_name_prefixed : link_name);
            link_create_retry = 1;
            goto V3_SYMLINK_CREATE_RETRY_0;
          }
        }
        return SDAS_FAILED_TO_EXTRACT_SYMLINK;
      }
      ret = fchmodat(AT_FDCWD,
                     link_name_prefixed ? link_name_prefixed : link_name,
                     permissions,
                     AT_SYMLINK_NOFOLLOW);
      if (ret == -1) {
        if (errno == EOPNOTSUPP) {
          fprintf(stderr,
                  "  NOTICE: Setting permissions of symlink is not supported "
                  "by FS/OS!\n");
        } else {
          fprintf(stderr,
                  "  WARNING: Failed to set permissions of symlink (%d)!\n",
                  errno);
        }
      }
      link_extracted = 1;
      fprintf(stderr,
              "  %s -> %s\n",
              link_name_prefixed ? link_name_prefixed : link_name,
              abs_path_prefixed ? abs_path_prefixed : parsed_abs_path);
    V3_SYMLINK_CREATE_AFTER_0:
      link_create_retry = 1;
#endif
    } else if (do_extract
        && !skip_due_to_map
        && !skip_due_to_invalid
        && lists_allowed
        && !absolute_preferred
        && parsed_rel_path) {
#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
  SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
  SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
      simple_archiver_helper_make_dirs_perms(
        link_name_prefixed ? link_name_prefixed : link_name,
        (state->parsed->flags & 0x2000)
          ? simple_archiver_internal_permissions_to_mode_t(
              state->parsed->dir_permissions)
          : (S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH),
        (state->parsed->flags & 0x400) ? state->parsed->uid : current_uid,
        (state->parsed->flags & 0x800) ? state->parsed->gid : current_gid);
      int_fast8_t link_create_retry = 0;
    V3_SYMLINK_CREATE_RETRY_1:
      ret = symlink(
        rel_path_prefixed ? rel_path_prefixed : parsed_rel_path,
        link_name_prefixed ? link_name_prefixed : link_name);
      if (ret == -1) {
        if (link_create_retry) {
          fprintf(
              stderr,
              "  WARNING: Failed to create symlink after removing existing "
              "symlink!\n");
          goto V3_SYMLINK_CREATE_AFTER_1;
        } else if (errno == EEXIST) {
          if ((state->parsed->flags & 8) == 0) {
            fprintf(stderr,
                    "  WARNING: Symlink already exists and "
                    "\"--overwrite-extract\" is not specified, skipping!\n");
            goto V3_SYMLINK_CREATE_AFTER_1;
          } else {
            fprintf(stderr,
                    "  NOTICE: Symlink already exists and "
                    "\"--overwrite-extract\" specified, attempting to "
                    "overwrite...\n");
            unlink(link_name_prefixed ? link_name_prefixed : link_name);
            link_create_retry = 1;
            goto V3_SYMLINK_CREATE_RETRY_1;
          }
        }
        return SDAS_FAILED_TO_EXTRACT_SYMLINK;
      }
      ret = fchmodat(AT_FDCWD,
                     link_name_prefixed ? link_name_prefixed : link_name,
                     permissions,
                     AT_SYMLINK_NOFOLLOW);
      if (ret == -1) {
        if (errno == EOPNOTSUPP) {
          fprintf(stderr,
                  "  NOTICE: Setting permissions of symlink is not supported "
                  "by FS/OS!\n");
        } else {
          fprintf(stderr,
                  "  WARNING: Failed to set permissions of symlink (%d)!\n",
                  errno);
        }
      }

      link_extracted = 1;
      fprintf(stderr,
              "  %s -> %s\n",
              link_name_prefixed ? link_name_prefixed : link_name,
              rel_path_prefixed ? rel_path_prefixed : parsed_rel_path);
    V3_SYMLINK_CREATE_AFTER_1:
      link_create_retry = 1;
#endif
    }

    if (do_extract && lists_allowed && link_extracted && geteuid() == 0) {
      uint32_t picked_uid;
      if (uid_remapped || user_remapped_uid) {
        if (state->parsed->flags & 0x4000) {
          if (uid_remapped) {
            picked_uid = *uid_remapped;
          } else if (user_remapped_uid) {
            picked_uid = *user_remapped_uid;
          } else {
            fprintf(stderr,
                    "ERROR: Failed to pick uid for link \"%s\"!\n",
                    link_name);
            return SDAS_INTERNAL_ERROR;
          }
        } else {
          if (user_remapped_uid) {
            picked_uid = *user_remapped_uid;
          } else if (uid_remapped) {
            picked_uid = *uid_remapped;
          } else {
            fprintf(stderr,
                    "ERROR: Failed to pick uid for link \"%s\"!\n",
                    link_name);
            return SDAS_INTERNAL_ERROR;
          }
        }
      } else {
        if (state->parsed->flags & 0x4000) {
          picked_uid = uid;
        } else if (username_uid_mapped) {
          picked_uid = *username_uid_mapped;
        } else {
          picked_uid = uid;
        }
      }
      uint32_t picked_gid;
      if (gid_remapped || group_remapped_gid) {
        if (state->parsed->flags & 0x8000) {
          if (gid_remapped) {
            picked_gid = *gid_remapped;
          } else if (group_remapped_gid) {
            picked_gid = *group_remapped_gid;
          } else {
            fprintf(stderr,
                    "ERROR: Failed to pick gid for link \"%s\"!\n",
                    link_name);
            return SDAS_INTERNAL_ERROR;
          }
        } else {
          if (group_remapped_gid) {
            picked_gid = *group_remapped_gid;
          } else if (gid_remapped) {
            picked_gid = *gid_remapped;
          } else {
            fprintf(stderr,
                    "ERROR: Failed to pick gid for link \"%s\"!\n",
                    link_name);
            return SDAS_INTERNAL_ERROR;
          }
        }
      } else {
        if (state->parsed->flags & 0x8000) {
          picked_gid = gid;
        } else if (group_gid_mapped) {
          picked_gid = *group_gid_mapped;
        } else {
          picked_gid = gid;
        }
      }
      ret = fchownat(
          AT_FDCWD,
          link_name_prefixed ? link_name_prefixed : link_name,
          state->parsed->flags & 0x400 ? state->parsed->uid : picked_uid,
          state->parsed->flags & 0x800 ? state->parsed->gid : picked_gid,
          AT_SYMLINK_NOFOLLOW);
      if (ret == -1) {
        fprintf(stderr,
                "  WARNING: Failed to force set UID/GID of symlink \"%s\""
                "(errno %d)!\n",
                link_name,
                errno);
      }
    }

    if (do_extract
        && !link_extracted
        && !skip_due_to_map
        && !skip_due_to_invalid
        && lists_allowed) {
      fprintf(stderr, "  WARNING: Symlink \"%s\" was not created!\n",
              link_name);
    } else if (do_extract
        && link_extracted
        && !skip_due_to_map
        && !skip_due_to_invalid
        && lists_allowed
        && links_list) {
      simple_archiver_list_add(links_list, strdup(link_name), NULL);
    }
  }

  if (fread(buf, 1, 4, in_f) != 4) {
    return SDAS_INVALID_FILE;
  }
  memcpy(&u32, buf, 4);
  simple_archiver_helper_32_bit_be(&u32);

  const uint32_t chunk_count = u32;
  for (uint32_t chunk_idx = 0; chunk_idx < chunk_count; ++chunk_idx) {
    if (is_sig_int_occurred) {
      return SDAS_SIGINT;
    }
    fprintf(stderr,
            "CHUNK %3" PRIu32 " of %3" PRIu32 "\n",
            chunk_idx + 1,
            chunk_count);

    if (fread(buf, 1, 4, in_f) != 4) {
      return SDAS_INVALID_FILE;
    }
    memcpy(&u32, buf, 4);
    simple_archiver_helper_32_bit_be(&u32);

    const uint32_t file_count = u32;

    __attribute__((cleanup(simple_archiver_list_free)))
    SDArchiverLinkedList *file_info_list = simple_archiver_list_init();

    __attribute__((cleanup(cleanup_internal_file_info)))
    SDArchiverInternalFileInfo *file_info = NULL;

    for (uint32_t file_idx = 0; file_idx < file_count; ++file_idx) {
      file_info = malloc(sizeof(SDArchiverInternalFileInfo));
      memset(file_info, 0, sizeof(SDArchiverInternalFileInfo));

      if (fread(buf, 1, 2, in_f) != 2) {
        return SDAS_INVALID_FILE;
      }
      memcpy(&u16, buf, 2);
      simple_archiver_helper_16_bit_be(&u16);

      file_info->filename = malloc(u16 + 1);
      int ret =
          read_buf_full_from_fd(in_f, (char *)buf, SIMPLE_ARCHIVER_BUFFER_SIZE,
                                u16 + 1, file_info->filename);
      if (ret != SDAS_SUCCESS) {
        return ret;
      }
      file_info->filename[u16] = 0;

      if (simple_archiver_validate_file_path(file_info->filename)) {
        fprintf(stderr,
                "ERROR: File idx %" PRIu32 ": Invalid filename!\n",
                file_idx);
        file_info->other_flags |= 1;
      } else if (simple_archiver_helper_string_allowed_lists(
          file_info->filename,
          state->parsed->whitelist_contains,
          state->parsed->whitelist_begins,
          state->parsed->whitelist_ends,
          state->parsed->blacklist_contains,
          state->parsed->blacklist_begins,
          state->parsed->blacklist_ends)) {
        file_info->other_flags |= 2;
      }

      if (do_extract
          && state
          && state->parsed
          && (state->parsed->flags & 8) != 0
          && (file_info->other_flags & 2) != 0) {
        int fd = open((const char *)buf, O_RDONLY | O_NOFOLLOW);
        if (fd == -1) {
          if (errno == ELOOP) {
            // Exists as a symlink.
            fprintf(stderr,
                    "WARNING: Filename \"%s\" already exists as symlink, "
                    "removing...\n",
                    (const char *)buf);
            unlink((const char *)buf);
          } else {
            // File doesn't exist, do nothing.
          }
        } else {
          close(fd);
          fprintf(stderr, "WARNING: File \"%s\" already exists, removing...\n",
                  (const char *)buf);
          unlink((const char *)buf);
        }
      }

      if (fread(file_info->bit_flags, 1, 4, in_f) != 4) {
        return SDAS_INVALID_FILE;
      }

      if (fread(&u32, 4, 1, in_f) != 1) {
        return SDAS_INVALID_FILE;
      }
      simple_archiver_helper_32_bit_be(&u32);
      __attribute__((cleanup(simple_archiver_helper_cleanup_uint32)))
      uint32_t *remapped_uid = NULL;
      if (do_extract && state && (state->parsed->flags & 0x400)) {
        file_info->uid = state->parsed->uid;
      } else {
        file_info->uid = u32;
        uint32_t out_uid;
        if (do_extract
            && state
            && simple_archiver_get_uid_mapping(state->parsed->mappings,
                                               state->parsed->users_infos,
                                               u32,
                                               &out_uid,
                                               NULL) == 0) {
          remapped_uid = malloc(sizeof(uint32_t));
          *remapped_uid = out_uid;
        }
      }

      if (fread(&u32, 4, 1, in_f) != 1) {
        return SDAS_INVALID_FILE;
      }
      simple_archiver_helper_32_bit_be(&u32);
      __attribute__((cleanup(simple_archiver_helper_cleanup_uint32)))
      uint32_t *remapped_gid = NULL;
      if (do_extract && state && (state->parsed->flags & 0x800)) {
        file_info->gid = state->parsed->gid;
      } else {
        file_info->gid = u32;
        uint32_t out_gid;
        if (do_extract
            && state
            && simple_archiver_get_gid_mapping(state->parsed->mappings,
                                               state->parsed->users_infos,
                                               u32,
                                               &out_gid,
                                               NULL) == 0) {
          remapped_gid = malloc(sizeof(uint32_t));
          *remapped_gid = out_gid;
        }
      }

      if (fread(&u16, 2, 1, in_f) != 1) {
        return SDAS_INVALID_FILE;
      }
      simple_archiver_helper_16_bit_be(&u16);

      __attribute__((cleanup(simple_archiver_helper_cleanup_c_string)))
      char *username = malloc(u16 + 1);

      if (u16 != 0) {
        if (fread(username, 1, u16 + 1, in_f) != (size_t)u16 + 1) {
          return SDAS_INVALID_FILE;
        }
        username[u16] = 0;
        file_info->username = strdup(username);
      } else {
        free(username);
        username = NULL;
      }

      __attribute__((cleanup(simple_archiver_helper_cleanup_uint32)))
      uint32_t *remapped_username_uid = NULL;
      if (username && state) {
        uint32_t out_uid;
        if (simple_archiver_get_user_mapping(state->parsed->mappings,
                                             state->parsed->users_infos,
                                             username,
                                             &out_uid,
                                             NULL) == 0) {
          remapped_username_uid = malloc(sizeof(uint32_t));
          *remapped_username_uid = out_uid;
        }
      }

      if (fread(&u16, 2, 1, in_f) != 1) {
        return SDAS_INVALID_FILE;
      }
      simple_archiver_helper_16_bit_be(&u16);

      __attribute__((cleanup(simple_archiver_helper_cleanup_c_string)))
      char *groupname = malloc(u16 + 1);

      if (u16 != 0) {
        if (fread(groupname, 1, u16 + 1, in_f) != (size_t)u16 + 1) {
          return SDAS_INVALID_FILE;
        }
        groupname[u16] = 0;
        file_info->groupname = strdup(groupname);
      } else {
        free(groupname);
        groupname = NULL;
      }

      __attribute__((cleanup(simple_archiver_helper_cleanup_uint32)))
      uint32_t *remapped_group_gid = NULL;
      if (groupname && state) {
        uint32_t out_gid;
        if (simple_archiver_get_group_mapping(state->parsed->mappings,
                                              state->parsed->users_infos,
                                              groupname,
                                              &out_gid,
                                              NULL) == 0) {
          remapped_group_gid = malloc(sizeof(uint32_t));
          *remapped_group_gid = out_gid;
        }
      }

      // Prefer uid derived from username by default.
      if (do_extract && state && file_info->username) {
        uint32_t *username_uid = simple_archiver_hash_map_get(
          state->parsed->users_infos.UnameToUid,
          file_info->username,
          strlen(file_info->username) + 1);
        if ((state->parsed->flags & 0x400) == 0
            && (state->parsed->flags & 0x4000) == 0
            && username_uid) {
          file_info->uid = *username_uid;
        }
      }

      // Apply user remapping.
      if (do_extract && state) {
        if (state->parsed->flags & 0x4000) {
          // Prefer UID first.
          if (remapped_uid) {
            file_info->uid = *remapped_uid;
          } else if (remapped_username_uid) {
            file_info->uid = *remapped_username_uid;
          }
        } else {
          // Prefer Username first.
          if (remapped_username_uid) {
            file_info->uid = *remapped_username_uid;
          } else if (remapped_uid) {
            file_info->uid = *remapped_uid;
          }
        }
      }

      // Prefer gid derived from group by default.
      if (do_extract && state && file_info->groupname) {
        uint32_t *groupname_gid = simple_archiver_hash_map_get(
          state->parsed->users_infos.GnameToGid,
          file_info->groupname,
          strlen(file_info->groupname) + 1);
        if ((state->parsed->flags & 0x800) == 0
            && (state->parsed->flags & 0x8000) == 0
            && groupname_gid) {
          file_info->gid = *groupname_gid;
        }
      }

      // Apply group remapping.
      if (do_extract && state) {
        if (state->parsed->flags & 0x8000) {
          // Prefer GID first.
          if (remapped_gid) {
            file_info->gid = *remapped_gid;
          } else if (remapped_group_gid) {
            file_info->gid = *remapped_group_gid;
          }
        } else {
          // Prefer Groupname first.
          if (remapped_group_gid) {
            file_info->gid = *remapped_group_gid;
          } else if (remapped_gid) {
            file_info->gid = *remapped_gid;
          }
        }
      }

      if (fread(&u64, 8, 1, in_f) != 1) {
        return SDAS_INVALID_FILE;
      }
      simple_archiver_helper_64_bit_be(&u64);
      file_info->file_size = u64;

      if (files_map && file_info->other_flags & 2) {
        simple_archiver_internal_paths_to_files_map(files_map,
                                                    file_info->filename);
      }

      simple_archiver_list_add(file_info_list, file_info,
                               free_internal_file_info);
      file_info = NULL;
    }

    if (fread(buf, 1, 8, in_f) != 8) {
      return SDAS_INVALID_FILE;
    }
    memcpy(&u64, buf, 8);
    simple_archiver_helper_64_bit_be(&u64);

    const uint64_t chunk_size = u64;
    uint64_t chunk_remaining = chunk_size;
    uint64_t chunk_idx = 0;

    SDArchiverLLNode *node = file_info_list->head;
    uint16_t file_idx = 0;

#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
    if (is_compressed) {
      // Start the decompressing process and read into files.

      // Handle SIGPIPE.
      is_sig_pipe_occurred = 0;
      signal(SIGPIPE, handle_sig_pipe);

      int pipe_into_cmd[2];
      int pipe_outof_cmd[2];
      __attribute__((cleanup(
          simple_archiver_internal_cleanup_decomp_pid))) pid_t decompressor_pid;
      if (pipe(pipe_into_cmd) != 0) {
        // Unable to create pipes.
        break;
      } else if (pipe(pipe_outof_cmd) != 0) {
        // Unable to create second set of pipes.
        close(pipe_into_cmd[0]);
        close(pipe_into_cmd[1]);
        return SDAS_INTERNAL_ERROR;
      } else if (fcntl(pipe_into_cmd[1], F_SETFL, O_NONBLOCK) != 0) {
        // Unable to set non-blocking on into-write-pipe.
        close(pipe_into_cmd[0]);
        close(pipe_into_cmd[1]);
        close(pipe_outof_cmd[0]);
        close(pipe_outof_cmd[1]);
        return SDAS_INTERNAL_ERROR;
      } else if (fcntl(pipe_outof_cmd[0], F_SETFL, O_NONBLOCK) != 0) {
        // Unable to set non-blocking on outof-read-pipe.
        close(pipe_into_cmd[0]);
        close(pipe_into_cmd[1]);
        close(pipe_outof_cmd[0]);
        close(pipe_outof_cmd[1]);
        return SDAS_INTERNAL_ERROR;
      }

      if (state && state->parsed && state->parsed->decompressor) {
        if (simple_archiver_de_compress(pipe_into_cmd, pipe_outof_cmd,
                                        state->parsed->decompressor,
                                        &decompressor_pid) != 0) {
          // Failed to spawn compressor.
          close(pipe_into_cmd[1]);
          close(pipe_outof_cmd[0]);
          fprintf(stderr,
                  "WARNING: Failed to start decompressor cmd! Invalid cmd?\n");
          return SDAS_INTERNAL_ERROR;
        }
      } else {
        if (simple_archiver_de_compress(pipe_into_cmd, pipe_outof_cmd,
                                        decompressor_cmd,
                                        &decompressor_pid) != 0) {
          // Failed to spawn compressor.
          close(pipe_into_cmd[1]);
          close(pipe_outof_cmd[0]);
          fprintf(stderr,
                  "WARNING: Failed to start decompressor cmd! Invalid cmd?\n");
          return SDAS_INTERNAL_ERROR;
        }
      }

      // Close unnecessary pipe fds on this end of the transfer.
      close(pipe_into_cmd[0]);
      close(pipe_outof_cmd[1]);

      __attribute__((cleanup(
          simple_archiver_internal_cleanup_int_fd))) int pipe_outof_read =
          pipe_outof_cmd[0];
      __attribute__((cleanup(
          simple_archiver_internal_cleanup_int_fd))) int pipe_into_write =
          pipe_into_cmd[1];

      int decompressor_status;
      int decompressor_return_val;
      int decompressor_ret =
          waitpid(decompressor_pid, &decompressor_status, WNOHANG);
      if (decompressor_ret == decompressor_pid) {
        // Status is available.
        if (WIFEXITED(decompressor_status)) {
          decompressor_return_val = WEXITSTATUS(decompressor_status);
          fprintf(stderr,
                  "WARNING: Exec failed (exec exit code %d)! Invalid "
                  "decompressor cmd?\n",
                  decompressor_return_val);
          return SDAS_INTERNAL_ERROR;
        }
      } else if (decompressor_ret == 0) {
        // Probably still running, continue on.
      } else {
        // Error.
        fprintf(stderr,
                "WARNING: Exec failed (exec exit code unknown)! Invalid "
                "decompressor cmd?\n");
        return SDAS_INTERNAL_ERROR;
      }

      char hold_buf[SIMPLE_ARCHIVER_BUFFER_SIZE];
      ssize_t has_hold = -1;

      while (node->next != file_info_list->tail) {
        if (is_sig_int_occurred) {
          return SDAS_SIGINT;
        }
        node = node->next;
        const SDArchiverInternalFileInfo *file_info = node->data;
        if (file_info->other_flags & 2) {
          fprintf(stderr,
                  "  FILE %3" PRIu16 " of %3" PRIu32 ": %s\n",
                  file_idx,
                  file_count,
                  file_info->filename);
        }
        ++file_idx;

        const size_t filename_length = strlen(file_info->filename);

        __attribute__((cleanup(simple_archiver_helper_cleanup_c_string)))
        char *filename_prefixed = NULL;
        if (do_extract && state && state->parsed->prefix) {
          filename_prefixed = malloc(prefix_length + filename_length + 1);
          memcpy(filename_prefixed, state->parsed->prefix, prefix_length);
          memcpy(filename_prefixed + prefix_length,
                 file_info->filename,
                 filename_length + 1);
          filename_prefixed[prefix_length + filename_length] = 0;
        }

        uint_fast8_t skip_due_to_map = 0;
        if (working_files_map && simple_archiver_hash_map_get(
                                     working_files_map, file_info->filename,
                                     filename_length + 1) == NULL) {
          skip_due_to_map = 1;
          fprintf(stderr, "    Skipping not specified in args...\n");
        } else if ((file_info->other_flags & 1) != 0
            && (file_info->other_flags & 2) != 0) {
          fprintf(stderr, "    Skipping invalid filename...\n");
        }

        if (do_extract
            && !skip_due_to_map
            && (file_info->other_flags & 1) == 0
            && (file_info->other_flags & 2) != 0) {
#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
          mode_t permissions;
          if (state->parsed->flags & 0x1000) {
            permissions =
              simple_archiver_internal_permissions_to_mode_t(
                state->parsed->file_permissions);
          } else {
            permissions = permissions_from_bits_version_1(
              file_info->bit_flags,
              0);
          }
#endif
          if ((state->parsed->flags & 8) == 0) {
            // Check if file already exists.
            __attribute__((cleanup(simple_archiver_helper_cleanup_FILE)))
            FILE *temp_fd = fopen(
              filename_prefixed ? filename_prefixed : file_info->filename,
              "r");
            if (temp_fd) {
              fprintf(stderr,
                      "  WARNING: File already exists and "
                      "\"--overwrite-extract\" is not specified, skipping!\n");
              int ret = read_decomp_to_out_file(
                  NULL, pipe_outof_read, (char *)buf,
                  SIMPLE_ARCHIVER_BUFFER_SIZE, file_info->file_size,
                  &pipe_into_write, &chunk_remaining, in_f, hold_buf,
                  &has_hold);
              if (ret != SDAS_SUCCESS) {
                return ret;
              }
              continue;
            }
          }

          simple_archiver_helper_make_dirs_perms(
            filename_prefixed ? filename_prefixed : file_info->filename,
            (state->parsed->flags & 0x2000)
              ? simple_archiver_internal_permissions_to_mode_t(
                  state->parsed->dir_permissions)
              : (S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH),
            (state->parsed->flags & 0x400) ? state->parsed->uid : file_info->uid,
            (state->parsed->flags & 0x800) ? state->parsed->gid : file_info->gid);
          int ret = read_decomp_to_out_file(
              filename_prefixed ? filename_prefixed : file_info->filename,
              pipe_outof_read,
              (char *)buf,
              SIMPLE_ARCHIVER_BUFFER_SIZE,
              file_info->file_size,
              &pipe_into_write,
              &chunk_remaining,
              in_f,
              hold_buf,
              &has_hold);
          if (ret != SDAS_SUCCESS) {
            return ret;
          }
#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
          if (chmod(filename_prefixed ? filename_prefixed : file_info->filename,
                    permissions)
                == -1) {
            return SDAS_INTERNAL_ERROR;
          } else if (geteuid() == 0 &&
                     chown(filename_prefixed
                             ? filename_prefixed
                             : file_info->filename,
                           file_info->uid,
                           file_info->gid) != 0) {
            fprintf(stderr,
                    "    ERROR Failed to set UID/GID of file \"%s\"!\n",
                    filename_prefixed
                      ? filename_prefixed
                      : file_info->filename);
            return SDAS_INTERNAL_ERROR;
          }
#endif
        } else if (!skip_due_to_map
            && (file_info->other_flags & 1) == 0
            && (file_info->other_flags & 2) != 0) {
          fprintf(stderr, "    Permissions:");
          permissions_from_bits_version_1(file_info->bit_flags, 1);
          fprintf(stderr,
                  "\n    UID: %" PRIu32 "\n    GID: %" PRIu32 "\n",
                  file_info->uid,
                  file_info->gid);
          if (file_info->username) {
            fprintf(stderr, "    Username: %s\n", file_info->username);
          } else {
            fprintf(stderr, "    Username not in archive\n");
          }
          if (file_info->groupname) {
            fprintf(stderr, "    Groupname: %s\n", file_info->groupname);
          } else {
            fprintf(stderr, "    Groupname not in archive\n");
          }
          if (is_compressed) {
            fprintf(stderr,
                    "    File size (uncompressed): %" PRIu64 "\n",
                    file_info->file_size);
          } else {
            fprintf(stderr,
                    "    File size: %" PRIu64 "\n",
                    file_info->file_size);
          }
          int ret = read_decomp_to_out_file(
              NULL, pipe_outof_read, (char *)buf, SIMPLE_ARCHIVER_BUFFER_SIZE,
              file_info->file_size, &pipe_into_write, &chunk_remaining, in_f,
              hold_buf, &has_hold);
          if (ret != SDAS_SUCCESS) {
            return ret;
          }
        } else {
          int ret = read_decomp_to_out_file(
              NULL, pipe_outof_read, (char *)buf, SIMPLE_ARCHIVER_BUFFER_SIZE,
              file_info->file_size, &pipe_into_write, &chunk_remaining, in_f,
              hold_buf, &has_hold);
          if (ret != SDAS_SUCCESS) {
            return ret;
          }
        }
      }

      // Ensure EOF is left from pipe.
      ssize_t read_ret =
          read(pipe_outof_read, buf, SIMPLE_ARCHIVER_BUFFER_SIZE);
      if (read_ret > 0) {
        fprintf(stderr, "WARNING decompressor didn't reach EOF!\n");
      }
    } else {
#else
    // } (This comment exists so that vim can correctly match curly-braces).
    if (!is_compressed) {
#endif
      while (node->next != file_info_list->tail) {
        if (is_sig_int_occurred) {
          return SDAS_SIGINT;
        }
        node = node->next;
        const SDArchiverInternalFileInfo *file_info = node->data;
        if (file_info->other_flags & 2) {
          fprintf(stderr,
                  "  FILE %3" PRIu16 " of %3" PRIu32 ": %s\n",
                  file_idx,
                  file_count,
                  file_info->filename);
        }
        ++file_idx;
        chunk_idx += file_info->file_size;
        if (chunk_idx > chunk_size) {
          fprintf(stderr, "ERROR Files in chunk is larger than chunk!\n");
          return SDAS_INTERNAL_ERROR;
        }

        const size_t filename_length = strlen(file_info->filename);

        __attribute__((cleanup(simple_archiver_helper_cleanup_c_string)))
        char *filename_prefixed = NULL;
        if (do_extract && state && state->parsed->prefix) {
          filename_prefixed = malloc(prefix_length + filename_length + 1);
          memcpy(filename_prefixed, state->parsed->prefix, prefix_length);
          memcpy(filename_prefixed + prefix_length,
                 file_info->filename,
                 filename_length + 1);
          filename_prefixed[prefix_length + filename_length] = 0;
        }

        uint_fast8_t skip_due_to_map = 0;
        if (do_extract
            && working_files_map
            && simple_archiver_hash_map_get(working_files_map,
                                            file_info->filename,
                                            filename_length + 1) == NULL) {
          skip_due_to_map = 1;
          fprintf(stderr, "    Skipping not specified in args...\n");
        } else if ((file_info->other_flags & 1) != 0
            && (file_info->other_flags & 2) != 0) {
          fprintf(stderr, "    Skipping invalid filename...\n");
        }

        if (do_extract
            && !skip_due_to_map
            && (file_info->other_flags & 1) == 0
            && (file_info->other_flags & 2) != 0) {
#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
          mode_t permissions;

          if (state->parsed->flags & 0x1000) {
            permissions = simple_archiver_internal_permissions_to_mode_t(
              state->parsed->file_permissions);
            fprintf(stderr,
                    "NOTICE: Forcing permissions as specified by "
                    "\"--force-file-permissions\"!\n");
          } else {
            permissions =
              permissions_from_bits_version_1(file_info->bit_flags, 0);
          }
#endif
          if ((state->parsed->flags & 8) == 0) {
            // Check if file already exists.
            __attribute__((cleanup(simple_archiver_helper_cleanup_FILE)))
            FILE *temp_fd = fopen(filename_prefixed
                                    ? filename_prefixed
                                    : file_info->filename,
                                  "r");
            if (temp_fd) {
              fprintf(stderr,
                      "  WARNING: File already exists and "
                      "\"--overwrite-extract\" is not specified, skipping!\n");
              int ret = read_buf_full_from_fd(in_f, (char *)buf,
                                              SIMPLE_ARCHIVER_BUFFER_SIZE,
                                              file_info->file_size, NULL);
              if (ret != SDAS_SUCCESS) {
                return ret;
              }
              continue;
            }
          }
          simple_archiver_helper_make_dirs_perms(
            filename_prefixed ? filename_prefixed : file_info->filename,
            (state->parsed->flags & 0x2000)
              ? simple_archiver_internal_permissions_to_mode_t(
                  state->parsed->dir_permissions)
              : (S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH),
            (state->parsed->flags & 0x400) ? state->parsed->uid : file_info->uid,
            (state->parsed->flags & 0x800) ? state->parsed->gid : file_info->gid);
          __attribute__((cleanup(simple_archiver_helper_cleanup_FILE)))
          FILE *out_fd = fopen(filename_prefixed
                                 ? filename_prefixed
                                 : file_info->filename,
                               "wb");
          int ret = read_fd_to_out_fd(in_f, out_fd, (char *)buf,
                                      SIMPLE_ARCHIVER_BUFFER_SIZE,
                                      file_info->file_size);
          if (ret != SDAS_SUCCESS) {
            return ret;
          }
          simple_archiver_helper_cleanup_FILE(&out_fd);
#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
          if (chmod(filename_prefixed ? filename_prefixed : file_info->filename,
                    permissions)
                == -1) {
            fprintf(
              stderr,
              "ERROR Failed to set permissions of file \"%s\"!\n",
              filename_prefixed ? filename_prefixed : file_info->filename);
            return SDAS_INTERNAL_ERROR;
          } else if (geteuid() == 0 &&
                     chown(filename_prefixed
                             ? filename_prefixed
                             : file_info->filename,
                           file_info->uid,
                           file_info->gid) != 0) {
            fprintf(
              stderr,
              "    ERROR Failed to set UID/GID of file \"%s\"!\n",
              filename_prefixed ? filename_prefixed : file_info->filename);
            return SDAS_INTERNAL_ERROR;
          }
#endif
        } else if (!skip_due_to_map
            && (file_info->other_flags & 1) == 0
            && (file_info->other_flags & 2) != 0) {
          fprintf(stderr, "    Permissions:");
          permissions_from_bits_version_1(file_info->bit_flags, 1);
          fprintf(stderr,
                  "\n    UID: %" PRIu32 "\n    GID: %" PRIu32 "\n",
                  file_info->uid,
                  file_info->gid);
          if (file_info->username) {
            fprintf(stderr, "    Username: %s\n", file_info->username);
          } else {
            fprintf(stderr, "    Username not in archive\n");
          }
          if (file_info->groupname) {
            fprintf(stderr, "    Groupname: %s\n", file_info->groupname);
          } else {
            fprintf(stderr, "    Groupname not in archive\n");
          }
          if (is_compressed) {
            fprintf(stderr,
                    "    File size (compressed): %" PRIu64 "\n",
                    file_info->file_size);
          } else {
            fprintf(stderr,
                    "    File size: %" PRIu64 "\n",
                    file_info->file_size);
          }
          int ret = read_buf_full_from_fd(in_f, (char *)buf,
                                          SIMPLE_ARCHIVER_BUFFER_SIZE,
                                          file_info->file_size, NULL);
          if (ret != SDAS_SUCCESS) {
            return ret;
          }
        } else {
          int ret = read_buf_full_from_fd(in_f, (char *)buf,
                                          SIMPLE_ARCHIVER_BUFFER_SIZE,
                                          file_info->file_size, NULL);
          if (ret != SDAS_SUCCESS) {
            return ret;
          }
        }
      }
    }
  }

  if (do_extract && links_list && files_map) {
    simple_archiver_safe_links_enforce(links_list, files_map);
  }

  if (fread(&u32, 4, 1, in_f) != 1) {
    fprintf(stderr, "ERROR: Failed to read directory count!\n");
    return SDAS_INVALID_FILE;
  }

  simple_archiver_helper_32_bit_be(&u32);

  const uint32_t size = u32;
  for (uint32_t idx = 0; idx < size; ++idx) {
    if (fread(&u16, 2, 1, in_f) != 1) {
      fprintf(stderr, "ERROR: Failed to read directory name length!\n");
      return SDAS_INVALID_FILE;
    }

    simple_archiver_helper_16_bit_be(&u16);

    __attribute__((cleanup(simple_archiver_helper_cleanup_c_string)))
    char *archive_dir_name = malloc(u16 + 1);

    if (fread(archive_dir_name, 1, u16 + 1, in_f) != (size_t)u16 + 1) {
      fprintf(stderr, "ERROR: Failed to read directory name!\n");
      return SDAS_INVALID_FILE;
    }

    archive_dir_name[u16] = 0;

    const uint_fast8_t lists_allowed =
      simple_archiver_helper_string_allowed_lists(
        archive_dir_name,
        state->parsed->whitelist_contains,
        state->parsed->whitelist_begins,
        state->parsed->whitelist_ends,
        state->parsed->blacklist_contains,
        state->parsed->blacklist_begins,
        state->parsed->blacklist_ends);

    uint8_t perms_flags[4];
    if (fread(perms_flags, 1, 2, in_f) != 2) {
      fprintf(stderr,
              "ERROR: Failed to read permission flags for \"%s\"!\n",
              archive_dir_name);
      return SDAS_INVALID_FILE;
    }
    perms_flags[2] = 0;
    perms_flags[3] = 0;

    uint32_t uid;
    if (fread(&uid, 4, 1, in_f) != 1) {
      fprintf(stderr,
              "ERROR: Failed to read UID for \"%s\"!\n", archive_dir_name);
      return SDAS_INVALID_FILE;
    }
    simple_archiver_helper_32_bit_be(&uid);

    uint32_t gid;
    if (fread(&gid, 4, 1, in_f) != 1) {
      fprintf(stderr,
              "ERROR: Failed to read GID for \"%s\"!\n", archive_dir_name);
      return SDAS_INVALID_FILE;
    }
    simple_archiver_helper_32_bit_be(&gid);

    if (fread(&u16, 2, 1, in_f) != 1) {
      return SDAS_INVALID_FILE;
    }
    simple_archiver_helper_16_bit_be(&u16);

    __attribute__((cleanup(simple_archiver_helper_cleanup_c_string)))
    char *username = malloc(u16 + 1);

    if (u16 != 0) {
      if (fread(username, 1, u16 + 1, in_f) != (size_t)u16 + 1) {
        return SDAS_INVALID_FILE;
      }
      username[u16] = 0;
    } else {
      free(username);
      username = NULL;
    }

    if (fread(&u16, 2, 1, in_f) != 1) {
      return SDAS_INVALID_FILE;
    }
    simple_archiver_helper_16_bit_be(&u16);

    __attribute__((cleanup(simple_archiver_helper_cleanup_c_string)))
    char *groupname = malloc(u16 + 1);

    if (u16 != 0) {
      if (fread(groupname, 1, u16 + 1, in_f) != (size_t)u16 + 1) {
        return SDAS_INVALID_FILE;
      }
      groupname[u16] = 0;
    } else {
      free(groupname);
      groupname = NULL;
    }

    __attribute__((cleanup(simple_archiver_helper_cleanup_uint32)))
    uint32_t *remapped_uid = NULL;
    __attribute__((cleanup(simple_archiver_helper_cleanup_uint32)))
    uint32_t *remapped_user_uid = NULL;
    if (do_extract && state) {
      uint32_t out_uid;
      if (simple_archiver_get_uid_mapping(state->parsed->mappings,
                                          state->parsed->users_infos,
                                          uid,
                                          &out_uid,
                                          NULL) == 0) {
        remapped_uid = malloc(sizeof(uint32_t));
        *remapped_uid = out_uid;
      }
      if (username
          && simple_archiver_get_user_mapping(state->parsed->mappings,
                                              state->parsed->users_infos,
                                              username,
                                              &out_uid,
                                              NULL) == 0) {
        remapped_user_uid = malloc(sizeof(uint32_t));
        *remapped_user_uid = out_uid;
      }
    }

    __attribute__((cleanup(simple_archiver_helper_cleanup_uint32)))
    uint32_t *remapped_gid = NULL;
    __attribute__((cleanup(simple_archiver_helper_cleanup_uint32)))
    uint32_t *remapped_group_gid = NULL;
    if (do_extract && state) {
      uint32_t out_gid;
      if (simple_archiver_get_gid_mapping(state->parsed->mappings,
                                          state->parsed->users_infos,
                                          gid,
                                          &out_gid,
                                          NULL) == 0) {
        remapped_gid = malloc(sizeof(uint32_t));
        *remapped_gid = out_gid;
      }
      if (groupname
          && simple_archiver_get_group_mapping(state->parsed->mappings,
                                               state->parsed->users_infos,
                                               groupname,
                                               &out_gid,
                                               NULL) == 0) {
        remapped_group_gid = malloc(sizeof(uint32_t));
        *remapped_group_gid = out_gid;
      }
    }

    if (do_extract && lists_allowed) {
      fprintf(stderr, "Creating dir \"%s\"\n", archive_dir_name);
      // Use UID derived from Username by default.
      if ((state->parsed->flags & 0x4000) == 0 && username) {
        uint32_t *username_uid = simple_archiver_hash_map_get(
          state->parsed->users_infos.UnameToUid,
          username,
          strlen(username) + 1);
        if (username_uid) {
          uid = *username_uid;
        }
      }
      // Apply UID/Username remapping.
      if (state->parsed->flags & 0x4000) {
        // Prefer UID first.
        if (remapped_uid) {
          uid = *remapped_uid;
        } else if (remapped_user_uid) {
          uid = *remapped_user_uid;
        }
      } else {
        // Prefer Username first.
        if (remapped_user_uid) {
          uid = *remapped_user_uid;
        } else if (remapped_uid) {
          uid = *remapped_uid;
        }
      }
      // Use GID derived from Group by default.
      if ((state->parsed->flags & 0x8000) == 0 && groupname) {
        uint32_t *group_gid = simple_archiver_hash_map_get(
          state->parsed->users_infos.GnameToGid,
          groupname,
          strlen(groupname) + 1);
        if (group_gid) {
          gid = *group_gid;
        }
      }
      // Apply GID/Groupname remapping.
      if (state->parsed->flags & 0x8000) {
        // Prefer GID first.
        if (remapped_gid) {
          gid = *remapped_gid;
        } else if (remapped_group_gid) {
          gid = *remapped_group_gid;
        }
      } else {
        // Prefer Groupname first.
        if (remapped_group_gid) {
          gid = *remapped_group_gid;
        } else if (remapped_gid) {
          gid = *remapped_gid;
        }
      }
    } else if (lists_allowed) {
      fprintf(stderr, "Dir entry \"%s\"\n", archive_dir_name);
      fprintf(stderr, "  Permissions: ");
      fprintf(stderr, "%s", (perms_flags[0] & 1)    ? "r" : "-");
      fprintf(stderr, "%s", (perms_flags[0] & 2)    ? "w" : "-");
      fprintf(stderr, "%s", (perms_flags[0] & 4)    ? "x" : "-");
      fprintf(stderr, "%s", (perms_flags[0] & 8)    ? "r" : "-");
      fprintf(stderr, "%s", (perms_flags[0] & 0x10) ? "w" : "-");
      fprintf(stderr, "%s", (perms_flags[0] & 0x20) ? "x" : "-");
      fprintf(stderr, "%s", (perms_flags[0] & 0x40) ? "r" : "-");
      fprintf(stderr, "%s", (perms_flags[0] & 0x80) ? "w" : "-");
      fprintf(stderr, "%s", (perms_flags[1] & 1)    ? "x" : "-");
      fprintf(stderr, "\n");

      fprintf(stderr,
              "  UID: %" PRIu32 ", GID: %" PRIu32 "\n",
              uid,
              gid);

      if (username) {
        fprintf(stderr, "  Username: %s\n", username);
      } else {
        fprintf(stderr, "  Username not in archive\n");
      }

      if (groupname) {
        fprintf(stderr, "  Groupname: %s\n", groupname);
      } else {
        fprintf(stderr, "  Groupname not in archive\n");
      }
    }

    __attribute__((cleanup(simple_archiver_helper_cleanup_c_string)))
    char *abs_path_dir = realpath(".", NULL);
    if (!abs_path_dir) {
      fprintf(
        stderr,
        "ERROR: Failed to get abs_path_dir of current working directory!\n");
      return SDAS_INTERNAL_ERROR;
    }

    __attribute__((cleanup(simple_archiver_helper_string_parts_free)))
    SAHelperStringParts string_parts =
      simple_archiver_helper_string_parts_init();

    simple_archiver_helper_string_parts_add(string_parts, abs_path_dir);

    if (abs_path_dir[strlen(abs_path_dir) - 1] != '/') {
      simple_archiver_helper_string_parts_add(string_parts, "/");
    }

    if (state && state->parsed->prefix) {
      simple_archiver_helper_string_parts_add(string_parts,
                                              state->parsed->prefix);
    }

    simple_archiver_helper_string_parts_add(string_parts, archive_dir_name);

    __attribute__((cleanup(simple_archiver_helper_cleanup_c_string)))
    char *abs_dir_path =
      simple_archiver_helper_string_parts_combine(string_parts);

    simple_archiver_helper_string_parts_add(string_parts, "/UNUSED");

    __attribute__((cleanup(simple_archiver_helper_cleanup_c_string)))
    char *abs_dir_path_with_suffix =
      simple_archiver_helper_string_parts_combine(string_parts);

    if (do_extract && lists_allowed) {
      int ret = simple_archiver_helper_make_dirs_perms(
        abs_dir_path_with_suffix,
        state && (state->parsed->flags & 0x2000)
          ? simple_archiver_internal_permissions_to_mode_t(
              state->parsed->dir_permissions)
          : (S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH),
        state && (state->parsed->flags & 0x400) ? state->parsed->uid : uid,
        state && (state->parsed->flags & 0x800) ? state->parsed->gid : gid);
      if (ret != 0) {
        fprintf(stderr, "ERROR: Failed to make dirs (%d)!\n", ret);
        return SDAS_INTERNAL_ERROR;
      }
#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
      mode_t perms = simple_archiver_internal_bits_to_mode_t(perms_flags);
      ret = chmod(abs_dir_path,
                  state && (state->parsed->flags & 0x10000)
                    ? simple_archiver_internal_permissions_to_mode_t(
                        state->parsed->empty_dir_permissions)
                    : perms);
      if (ret != 0) {
        fprintf(stderr,
                "WARNING: Failed to set permissions on dir \"%s\"!\n",
                abs_dir_path);
      }
#endif
    }
  }

  return SDAS_SUCCESS;
}

int simple_archiver_de_compress(int pipe_fd_in[2], int pipe_fd_out[2],
                                const char *cmd, void *pid_out) {
#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
  posix_spawn_file_actions_t file_actions;
  memset(&file_actions, 0, sizeof(file_actions));
  if (posix_spawn_file_actions_init(&file_actions) != 0) {
    close(pipe_fd_in[0]);
    close(pipe_fd_out[1]);
    return 1;
  } else if (posix_spawn_file_actions_adddup2(&file_actions, pipe_fd_in[0],
                                              STDIN_FILENO) != 0) {
    posix_spawn_file_actions_destroy(&file_actions);
    close(pipe_fd_in[0]);
    close(pipe_fd_out[1]);
    return 2;
  } else if (posix_spawn_file_actions_adddup2(&file_actions, pipe_fd_out[1],
                                              STDOUT_FILENO) != 0) {
    posix_spawn_file_actions_destroy(&file_actions);
    close(pipe_fd_in[0]);
    close(pipe_fd_out[1]);
    return 3;
  } else if (posix_spawn_file_actions_addclose(&file_actions, pipe_fd_in[1]) !=
             0) {
    posix_spawn_file_actions_destroy(&file_actions);
    close(pipe_fd_in[0]);
    close(pipe_fd_out[1]);
    return 4;
  } else if (posix_spawn_file_actions_addclose(&file_actions, pipe_fd_out[0]) !=
             0) {
    posix_spawn_file_actions_destroy(&file_actions);
    close(pipe_fd_in[0]);
    close(pipe_fd_out[1]);
    return 5;
  }

  __attribute__((cleanup(
      simple_archiver_helper_cmd_string_argv_free_ptr))) char **cmd_argv =
      simple_archiver_helper_cmd_string_to_argv(cmd);

  pid_t *pid_t_out = pid_out;
  if (posix_spawnp(pid_t_out, cmd_argv[0], &file_actions, NULL, cmd_argv,
                   NULL) != 0) {
    close(pipe_fd_in[0]);
    close(pipe_fd_out[1]);
    posix_spawn_file_actions_destroy(&file_actions);
    return 6;
  }

  posix_spawn_file_actions_destroy(&file_actions);

  return 0;
#else
  return 1;
#endif
}

char *simple_archiver_filenames_to_relative_path(const char *from_abs,
                                                 const char *to_abs) {
  if (!from_abs || !to_abs) {
    return NULL;
  }

  // Get first non-common char and last slash before it.
  uint_fast32_t idx;
  uint_fast32_t last_slash;
  for (idx = 0, last_slash = 0; idx < strlen(from_abs) && idx < strlen(to_abs);
       ++idx) {
    if (((const char *)to_abs)[idx] != ((const char *)from_abs)[idx]) {
      break;
    } else if (((const char *)to_abs)[idx] == '/') {
      last_slash = idx + 1;
    }
  }

  // Get substrings of both paths.
  char *link_substr = (char *)from_abs + last_slash;
  char *dest_substr = (char *)to_abs + last_slash;
  char *rel_path = malloc(strlen(dest_substr) + 1);
  strncpy(rel_path, dest_substr, strlen(dest_substr) + 1);

  // fprintf(stderr, "DEBUG: link_substr \"%s\", dest_substr \"%s\"\n",
  //     link_substr, dest_substr);

  // Get the relative path finally.
  int_fast8_t has_slash = 0;
  idx = 0;
  do {
    for (; link_substr[idx] != '/' && link_substr[idx] != 0; ++idx);
    if (link_substr[idx] == 0) {
      has_slash = 0;
    } else {
      has_slash = 1;
      size_t new_rel_path_size = strlen(rel_path) + 1 + 3;
      char *new_rel_path = malloc(new_rel_path_size);
      new_rel_path[0] = '.';
      new_rel_path[1] = '.';
      new_rel_path[2] = '/';
      strncpy(new_rel_path + 3, rel_path, new_rel_path_size - 3);
      free(rel_path);
      rel_path = new_rel_path;
      ++idx;
    }
  } while (has_slash);

  return rel_path;
}

int simple_archiver_validate_file_path(const char *filepath) {
  if (!filepath) {
    return 5;
  }

  const size_t len = strlen(filepath);

  if (len >= 1 && filepath[0] == '/') {
    return 1;
  } else if (len >= 3 && filepath[0] == '.' && filepath[1] == '.' &&
             filepath[2] == '/') {
    return 2;
  } else if (len >= 3 && filepath[len - 1] == '.' && filepath[len - 2] == '.' &&
             filepath[len - 3] == '/') {
    return 4;
  }

  for (size_t idx = 0; idx < len; ++idx) {
    if (len - idx < 4) {
      break;
    } else if (strncmp(filepath + idx, "/../", 4) == 0) {
      return 3;
    }
  }

  return 0;
}

void simple_archiver_safe_links_enforce(SDArchiverLinkedList *links_list,
                                        SDArchiverHashMap *files_map) {
  uint_fast8_t need_to_print_note = 1;
  // safe-links: Check that every link maps to a file in the files_map.
  __attribute__((
      cleanup(simple_archiver_helper_cleanup_c_string))) char *path_to_cwd =
      realpath(".", NULL);

  // Ensure path_to_cwd ends with '/'.
  uint32_t idx = 0;
  while (path_to_cwd[idx] != 0) {
    ++idx;
  }
  if (path_to_cwd[idx - 1] != '/') {
    char *temp = malloc(idx + 2);
    memcpy(temp, path_to_cwd, idx);
    temp[idx] = '/';
    temp[idx + 1] = 0;
    free(path_to_cwd);
    path_to_cwd = temp;
  }

  // Check every link to make sure it points to an existing file.
  SDArchiverLLNode *links_node = links_list->head;
  while (links_node->next != links_list->tail) {
    links_node = links_node->next;
    __attribute__((
        cleanup(simple_archiver_helper_cleanup_c_string))) char *link_realpath =
        realpath(links_node->data, NULL);
    if (link_realpath) {
      // Get local path.
      __attribute__((cleanup(
          simple_archiver_helper_cleanup_c_string))) char *link_localpath =
          simple_archiver_filenames_to_relative_path(path_to_cwd,
                                                     link_realpath);
      if (!simple_archiver_hash_map_get(files_map, link_localpath,
                                        strlen(link_localpath) + 1)) {
        // Invalid symlink.
        fprintf(stderr,
                "Symlink \"%s\" is invalid (not pointing to archived file), "
                "removing...\n",
                (const char *)links_node->data);
        unlink(links_node->data);
        if (need_to_print_note) {
          fprintf(stderr,
                  "NOTE: Disable this behavior with \"--no-safe-links\" if "
                  "needed.\n");
          need_to_print_note = 0;
        }
      }
    } else {
      // Invalid symlink.
      fprintf(stderr,
              "Symlink \"%s\" is invalid (failed to resolve), removing...\n",
              (const char *)links_node->data);
      unlink(links_node->data);
      if (need_to_print_note) {
        fprintf(stderr,
                "NOTE: Disable this behavior with \"--no-safe-links\" if "
                "needed.\n");
        need_to_print_note = 0;
      }
    }
  }
}
