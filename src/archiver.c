// ISC License
//
// Copyright (c) 2024 Stephen Seo
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
#include <unistd.h>
#endif

#include "helpers.h"

#define TEMP_FILENAME_CMP "%s%ssimple_archiver_compressed_%lu.tmp"
#define FILE_COUNTS_OUTPUT_FORMAT_STR_0 "\nFile %%%lulu of %%%lulu.\n"
#define FILE_COUNTS_OUTPUT_FORMAT_STR_1 "[%%%lulu/%%%lulu]\n"

#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
volatile int is_sig_pipe_occurred = 0;

void handle_sig_pipe(int sig) {
  if (sig == SIGPIPE) {
    is_sig_pipe_occurred = 1;
  }
}
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
  uint64_t file_size;
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

int write_files_fn(void *data, void *ud) {
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
      if (!tmp_fd) {
        fprintf(stderr, "ERROR: Unable to create temp file for compressing!\n");
#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
        __attribute__((
            cleanup(simple_archiver_helper_cleanup_malloced))) void *real_cwd =
            realpath(".", NULL);
        if (real_cwd) {
          fprintf(stderr, "Tried to create temp file(s) in \"%s\"!\n",
                  (char *)real_cwd);
        }
#endif
        fprintf(stderr,
                "(Use \"--temp-files-dir <dir>\" to change where to write temp "
                "files.)\n");
        return 1;
      }
      __attribute__((cleanup(cleanup_temp_filename_delete))) void **ptrs_array =
          malloc(sizeof(void *) * 2);
      ptrs_array[0] = temp_filename;
      ptrs_array[1] = &tmp_fd;

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
      char write_buf[1024];
      char read_buf[1024];
      int_fast8_t write_again = 0;
      int_fast8_t write_done = 0;
      int_fast8_t read_done = 0;
      size_t write_count;
      size_t read_count;
      ssize_t ret;
      while (!write_done || !read_done) {
        if (is_sig_pipe_occurred) {
          fprintf(stderr,
                  "WARNING: Failed to write to compressor (SIGPIPE)! Invalid "
                  "compressor cmd?\n");
          return 1;
        }

        // Read from file.
        if (!write_done) {
          if (!write_again) {
            write_count = fread(write_buf, 1, 1024, file_fd);
          }
          if (write_count > 0) {
            ret = write(pipe_into_cmd[1], write_buf, write_count);
            if (ret == -1) {
              if (errno == EAGAIN || errno == EWOULDBLOCK) {
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
          ret = read(pipe_outof_cmd[0], read_buf, 1024);
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
            // fprintf(stderr, "read_done\n");
          } else if (ret == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
              // Nop.
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
      }

      waitpid(compressor_pid, NULL, 0);

      uint16_t u16;
      uint64_t u64;

      size_t temp_size = strlen(file_info->filename);
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
      memcpy(temp_to_write->buf, file_info->filename, u16 + 1);
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
        write_count = fread(write_buf, 1, 1024, tmp_fd);
        if (write_count == 1024) {
          fwrite(write_buf, 1, 1024, state->out_f);
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
      memcpy(temp_to_write->buf, file_info->filename, u16 + 1);
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

      // Write file.
      fprintf(stderr, "Writing file: %s\n", file_info->filename);
      char buf[1024];
      size_t ret;
      do {
        ret = fread(buf, 1, 1024, fd);
        if (ret == 1024) {
          fwrite(buf, 1, 1024, state->out_f);
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
    memcpy(temp_to_write->buf, file_info->filename, u16 + 1);
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
#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
    abs_path = realpath(file_info->filename, NULL);
#endif
    __attribute__((cleanup(
        simple_archiver_helper_cleanup_malloced))) void *rel_path = NULL;
    if (abs_path) {
      // Get relative path.
      // First get absolute path of link.
      __attribute__((cleanup(
          simple_archiver_helper_cleanup_malloced))) void *link_abs_path =
          simple_archiver_file_abs_path(file_info->filename);
      if (!link_abs_path) {
        fprintf(stderr, "WARNING: Failed to get absolute path of link!\n");
      } else {
        // fprintf(stderr, "DEBUG: abs_path: %s\nDEBUG: link_abs_path: %s\n",
        //                 (char*)abs_path, (char*)link_abs_path);

        rel_path =
            simple_archiver_filenames_to_relative_path(link_abs_path, abs_path);
      }
    }

    // Check if absolute path refers to one of the filenames.
    if (abs_path && (state->parsed->flags & 0x20) == 0 &&
        !simple_archiver_hash_map_get(state->map, abs_path,
                                      strlen(abs_path) + 1)) {
      // Is not a filename being archived, set preference to absolute path.
      fprintf(stderr,
              "NOTICE: abs_path exists, \"--no-abs-symlink\" not specified, "
              "and link refers to file NOT in archive; preferring abs_path.\n");
      ((uint8_t *)temp_to_write->buf)[1] |= 0x4;
    }

    // Store the 4 byte bit-flags for file.
    simple_archiver_list_add(to_write, temp_to_write, free_internal_to_write);

    // Store the absolute and relative paths.
    if (!abs_path) {
      fprintf(stderr,
              "WARNING: Failed to get absolute path of link destination!\n");
      temp_to_write = malloc(sizeof(SDArchiverInternalToWrite));
      temp_to_write->buf = malloc(2);
      temp_to_write->size = 2;
      memset(temp_to_write->buf, 0, 2);
      simple_archiver_list_add(to_write, temp_to_write, free_internal_to_write);
    } else if ((state->parsed->flags & 0x20) == 0) {
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
      fprintf(stderr, "  abs path: %s\n", (char *)abs_path);
    }
    fprintf(stderr, "  rel path: %s\n", (char *)rel_path);
    simple_archiver_list_get(to_write, write_list_datas_fn, state->out_f);
    simple_archiver_list_free(&to_write);
  }

  char format_str[64];
  snprintf(format_str, 64, FILE_COUNTS_OUTPUT_FORMAT_STR_1, state->digits,
           state->digits);
  fprintf(stderr, format_str, ++(state->count), state->max);
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
  char *fullpath = simple_archiver_file_abs_path(file_info->filename);
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
      simple_archiver_hash_map_insert(
          abs_filenames, fullpath_dirname_copy, fullpath_dirname_copy,
          strlen(fullpath_dirname_copy) + 1,
          simple_archiver_helper_datastructure_cleanup_nop, NULL);
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

/// Returns SDAS_SUCCESS on success.
int read_decomp_to_out_file(const char *out_filename, int in_pipe,
                            char *read_buf, const size_t read_buf_size,
                            const uint64_t file_size) {
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
    if (file_size - written_amt >= read_buf_size) {
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
        // Error.
        fprintf(stderr, "ERROR Failed to read from decompressor! (%lu)\n",
                read_ret);
        return SDAS_INTERNAL_ERROR;
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
        // Error.
        fprintf(stderr, "ERROR Failed to read from decompressor! (%d)\n",
                errno);
        return SDAS_INTERNAL_ERROR;
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
    free(file_info);
  }
}

void cleanup_internal_file_info(SDArchiverInternalFileInfo **file_info) {
  if (file_info && *file_info) {
    if ((*file_info)->filename) {
      free((*file_info)->filename);
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
void simple_archiver_internal_cleanup_decomp(pid_t *decomp_pid) {
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
  }
}
#endif

int symlinks_and_files_from_files(void *data, void *ud) {
  SDArchiverFileInfo *file_info = data;
  void **ptr_array = ud;
  SDArchiverLinkedList *symlinks_list = ptr_array[0];
  SDArchiverLinkedList *files_list = ptr_array[1];
  const char *user_cwd = ptr_array[2];

  if (file_info->filename) {
    if (file_info->link_dest) {
      simple_archiver_list_add(
          symlinks_list, file_info->filename,
          simple_archiver_helper_datastructure_cleanup_nop);
    } else {
      SDArchiverInternalFileInfo *file_info_struct =
          malloc(sizeof(SDArchiverInternalFileInfo));
      file_info_struct->filename = strdup(file_info->filename);
      file_info_struct->bit_flags[0] = 0xFF;
      file_info_struct->bit_flags[1] = 1;
      file_info_struct->bit_flags[2] = 0;
      file_info_struct->bit_flags[3] = 0;
      file_info_struct->uid = 0;
      file_info_struct->gid = 0;
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
      simple_archiver_list_add(files_list, file_info_struct,
                               free_internal_file_info);
#endif
    }
  }

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
  switch (state->parsed->write_version) {
    case 0:
      return simple_archiver_write_v0(out_f, state, filenames);
    case 1:
      return simple_archiver_write_v1(out_f, state, filenames);
    default:
      fprintf(stderr, "ERROR: Unsupported write version %u!\n",
              state->parsed->write_version);
      return SDAS_INVALID_WRITE_VERSION;
  }
}

int simple_archiver_write_v0(FILE *out_f, SDArchiverState *state,
                             const SDArchiverLinkedList *filenames) {
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
  if (simple_archiver_list_get(filenames, write_files_fn, state)) {
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

  ptr_array = malloc(sizeof(void *) * 3);
  ptr_array[0] = symlinks_list;
  ptr_array[1] = files_list;
  ptr_array[2] = (void *)state->parsed->user_cwd;

  if (simple_archiver_list_get(filenames, symlinks_and_files_from_files,
                               ptr_array)) {
    free(ptr_array);
    return SDAS_INTERNAL_ERROR;
  }
  free(ptr_array);

  if (fwrite("SIMPLE_ARCHIVE_VER", 1, 18, out_f) != 18) {
    return SDAS_FAILED_TO_WRITE;
  }

  char buf[1024];
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

  {
    const SDArchiverLLNode *node = symlinks_list->head;
    for (u32 = 0;
         u32 < (uint32_t)symlinks_list->count && node != symlinks_list->tail;) {
      node = node->next;
      ++u32;
      memset(buf, 0, 2);
#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
      // Check if symlink points to thing to be stored into archive.
      __attribute__((
          cleanup(simple_archiver_helper_cleanup_malloced))) void *abs_path =
          realpath(node->data, NULL);
      __attribute__((cleanup(
          simple_archiver_helper_cleanup_malloced))) void *rel_path = NULL;
      if (abs_path) {
        __attribute__((cleanup(
            simple_archiver_helper_cleanup_malloced))) void *link_abs_path =
            simple_archiver_file_abs_path(node->data);
        if (!link_abs_path) {
          fprintf(stderr, "WARNING: Failed to get absolute path to link!\n");
        } else {
          rel_path = simple_archiver_filenames_to_relative_path(link_abs_path,
                                                                abs_path);
        }
      }
      if (abs_path && (state->parsed->flags & 0x20) == 0 &&
          !simple_archiver_hash_map_get(abs_filenames, abs_path,
                                        strlen(abs_path) + 1)) {
        // Is not a filename being archived, set preference to absolute path.
        buf[0] |= 1;
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
      buf[1] = 3;
#endif
      if (fwrite(buf, 1, 2, out_f) != 2) {
        return SDAS_FAILED_TO_WRITE;
      }

      size_t len = strlen(node->data);
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
      if (fwrite(node->data, 1, u16 + 1, out_f) != (size_t)u16 + 1) {
        return SDAS_FAILED_TO_WRITE;
      }

      if (abs_path) {
        len = strlen(abs_path);
        if (len >= 0xFFFF) {
          fprintf(stderr,
                  "ERROR: Symlink destination absolute path is too long!\n");
          return SDAS_INVALID_PARSED_STATE;
        }

        u16 = (uint16_t)len;
        simple_archiver_helper_16_bit_be(&u16);
        if (fwrite(&u16, 2, 1, out_f) != 1) {
          return SDAS_FAILED_TO_WRITE;
        }
        simple_archiver_helper_16_bit_be(&u16);
        if (fwrite(abs_path, 1, u16 + 1, out_f) != (size_t)u16 + 1) {
          return SDAS_FAILED_TO_WRITE;
        }
      } else {
        u16 = 0;
        if (fwrite(&u16, 2, 1, out_f) != 1) {
          return SDAS_FAILED_TO_WRITE;
        }
      }

      if (rel_path) {
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
      } else {
        u16 = 0;
        if (fwrite(&u16, 2, 1, out_f) != 1) {
          return SDAS_FAILED_TO_WRITE;
        }
      }
    }
    if (u32 != (uint32_t)symlinks_list->count) {
      fprintf(stderr, "ERROR: Iterated through %u symlinks out of %u total!\n",
              u32, (uint32_t)symlinks_list->count);
      return SDAS_INTERNAL_ERROR;
    }
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
    if (current_size > 0 && current_count > 0) {
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
  for (SDArchiverLLNode *chunk_c_node = chunk_counts->head->next;
       chunk_c_node != chunk_counts->tail; chunk_c_node = chunk_c_node->next) {
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
      size_t len = strlen(file_info_struct->filename);
      if (len >= 0xFFFF) {
        fprintf(stderr, "ERROR: Filename is too large!\n");
        return SDAS_INVALID_FILE;
      }
      u16 = (uint16_t)len;
      simple_archiver_helper_16_bit_be(&u16);
      if (fwrite(&u16, 2, 1, out_f) != 1) {
        return SDAS_FAILED_TO_WRITE;
      }
      simple_archiver_helper_16_bit_be(&u16);
      if (fwrite(file_info_struct->filename, 1, u16 + 1, out_f) !=
          (size_t)u16 + 1) {
        return SDAS_FAILED_TO_WRITE;
      } else if (fwrite(file_info_struct->bit_flags, 1, 4, out_f) != 4) {
        return SDAS_FAILED_TO_WRITE;
      }
      // UID and GID.
      u32 = file_info_struct->uid;
      simple_archiver_helper_32_bit_be(&u32);
      if (fwrite(&u32, 4, 1, out_f) != 1) {
        return SDAS_FAILED_TO_WRITE;
      }
      u32 = file_info_struct->gid;
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

      __attribute__((cleanup(cleanup_temp_filename_delete))) void **ptrs_array =
          malloc(sizeof(void *) * 2);
      ptrs_array[0] = NULL;
      ptrs_array[1] = NULL;

      __attribute__((cleanup(simple_archiver_helper_cleanup_FILE)))
      FILE *temp_fd = NULL;

      size_t temp_filename_size = strlen(state->parsed->temp_dir) + 1 + 64;
      __attribute__((cleanup(
          simple_archiver_helper_cleanup_c_string))) char *temp_filename =
          malloc(temp_filename_size);

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
        ptrs_array[0] = temp_filename;
      } else {
        temp_fd = tmpfile();
      }

      if (!temp_fd) {
        return SDAS_INTERNAL_ERROR;
      }

      // Handle SIGPIPE.
      is_sig_pipe_occurred = 0;
      signal(SIGPIPE, handle_sig_pipe);

      int pipe_into_cmd[2];
      int pipe_outof_cmd[2];
      pid_t compressor_pid;

      if (pipe(pipe_into_cmd) != 0) {
        // Unable to create pipes.
        return SDAS_INTERNAL_ERROR;
      } else if (pipe(pipe_outof_cmd) != 0) {
        // Unable to create second set of pipes.
        close(pipe_into_cmd[0]);
        close(pipe_into_cmd[1]);
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
          simple_archiver_internal_cleanup_int_fd))) int pipe_into_write =
          pipe_into_cmd[1];
      __attribute__((cleanup(
          simple_archiver_internal_cleanup_int_fd))) int pipe_outof_read =
          pipe_outof_cmd[0];

      for (uint64_t file_idx = 0; file_idx < *((uint64_t *)chunk_c_node->data);
           ++file_idx) {
        file_node = file_node->next;
        if (file_node == files_list->tail) {
          return SDAS_INTERNAL_ERROR;
        }
        const SDArchiverInternalFileInfo *file_info_struct = file_node->data;
        __attribute__((cleanup(simple_archiver_helper_cleanup_FILE))) FILE *fd =
            fopen(file_info_struct->filename, "rb");
        while (!feof(fd)) {
          if (ferror(fd)) {
            fprintf(stderr, "ERROR: Writing to chunk, file read error!\n");
            return SDAS_INTERNAL_ERROR;
          }
          size_t fread_ret = fread(buf, 1, 1024, fd);
          if (fread_ret > 0) {
            ssize_t write_ret = write(pipe_into_write, buf, fread_ret);
            if (write_ret < 0) {
              fprintf(stderr,
                      "ERROR: Writing to compressor, pipe write error!\n");
              return SDAS_FAILED_TO_WRITE;
            } else if ((size_t)write_ret != fread_ret) {
              fprintf(stderr,
                      "ERROR: Writing to compressor, unable to write bytes!\n");
              return SDAS_FAILED_TO_WRITE;
            }
          }
        }
      }

      // Close write to pipe to compressor as the chunk is written.
      simple_archiver_internal_cleanup_int_fd(&pipe_into_write);

      // Read compressed data into temporary file.
      do {
        ssize_t read_ret = read(pipe_outof_read, buf, 1024);
        if (read_ret < 0) {
          fprintf(stderr, "ERROR: Reading from compressor, pipe read error!\n");
          return SDAS_INTERNAL_ERROR;
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
      } while (1);

      // Close read from pipe from compressor as chunk is fully compressed.
      simple_archiver_internal_cleanup_int_fd(&pipe_outof_read);

      // Wait on compressor to stop.
      waitpid(compressor_pid, NULL, 0);

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

      // Write compressed chunk.
      while (!feof(temp_fd)) {
        if (ferror(temp_fd)) {
          return SDAS_INTERNAL_ERROR;
        }
        size_t fread_ret = fread(buf, 1, 1024, temp_fd);
        if (fread_ret > 0) {
          size_t fwrite_ret = fwrite(buf, 1, fread_ret, out_f);
          if (fwrite_ret != fread_ret) {
            fprintf(stderr,
                    "ERROR: Partial write of read bytes from temp file to "
                    "output file!\n");
            return SDAS_FAILED_TO_WRITE;
          }
        }
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
        file_node = file_node->next;
        if (file_node == files_list->tail) {
          return SDAS_INTERNAL_ERROR;
        }
        const SDArchiverInternalFileInfo *file_info_struct = file_node->data;
        __attribute__((cleanup(simple_archiver_helper_cleanup_FILE))) FILE *fd =
            fopen(file_info_struct->filename, "rb");
        while (!feof(fd)) {
          if (ferror(fd)) {
            fprintf(stderr, "ERROR: Writing to chunk, file read error!\n");
            return SDAS_INTERNAL_ERROR;
          }
          size_t fread_ret = fread(buf, 1, 1024, fd);
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

int simple_archiver_parse_archive_info(FILE *in_f, int_fast8_t do_extract,
                                       const SDArchiverState *state) {
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
    return simple_archiver_parse_archive_version_0(in_f, do_extract, state);
  } else if (u16 == 1) {
    return simple_archiver_parse_archive_version_1(in_f, do_extract, state);
  } else {
    fprintf(stderr, "ERROR Unsupported archive version %u!\n", u16);
    return SDAS_INVALID_FILE;
  }
}

int simple_archiver_parse_archive_version_0(FILE *in_f, int_fast8_t do_extract,
                                            const SDArchiverState *state) {
  uint8_t buf[1024];
  memset(buf, 0, 1024);
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
    fprintf(stderr, "Compressor size is %u\n", u16);
    if (u16 < 1024) {
      if (fread(buf, 1, u16 + 1, in_f) != (size_t)u16 + 1) {
        return SDAS_INVALID_FILE;
      }
      buf[1023] = 0;
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
    fprintf(stderr, "Decompressor size is %u\n", u16);
    if (u16 < 1024) {
      if (fread(buf, 1, u16 + 1, in_f) != (size_t)u16 + 1) {
        return SDAS_INVALID_FILE;
      }
      buf[1023] = 0;
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
  fprintf(stderr, "File count is %u\n", u32);

  const uint32_t size = u32;
  const size_t digits = simple_archiver_helper_num_digits(size);
  char format_str[128];
  snprintf(format_str, 128, FILE_COUNTS_OUTPUT_FORMAT_STR_0, digits, digits);
  int_fast8_t skip = 0;
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
  for (uint32_t idx = 0; idx < size; ++idx) {
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
    if (u16 < 1024) {
      if (fread(buf, 1, u16 + 1, in_f) != (size_t)u16 + 1) {
        return SDAS_INVALID_FILE;
      }
      buf[1023] = 0;
      fprintf(stderr, "  Filename: %s\n", buf);
      if (do_extract) {
        if ((state->parsed->flags & 0x8) == 0) {
          __attribute__((cleanup(simple_archiver_helper_cleanup_FILE)))
          FILE *test_fd = fopen((const char *)buf, "rb");
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
          int fd = open((const char *)buf, O_RDONLY | O_NOFOLLOW);
          if (fd == -1) {
            if (errno == ELOOP) {
              // Is an existing symbolic file.
              unlink((const char *)buf);
            }
          } else {
            close(fd);
            // Is an existing file.
            unlink((const char *)buf);
          }
        }
        if (!skip) {
          out_f_name = malloc(strlen((const char *)buf) + 1);
          memcpy(out_f_name, buf, strlen((const char *)buf) + 1);
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
      if (do_extract) {
        if ((state->parsed->flags & 0x8) == 0) {
          __attribute__((cleanup(simple_archiver_helper_cleanup_FILE)))
          FILE *test_fd = fopen((const char *)uc_heap_buf, "rb");
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
          int fd = open((const char *)uc_heap_buf, O_RDONLY | O_NOFOLLOW);
          if (fd == -1) {
            if (errno == ELOOP) {
              // Is an existing symbolic file.
              unlink((const char *)uc_heap_buf);
            }
          } else {
            close(fd);
            // Is an existing file.
            unlink((const char *)uc_heap_buf);
          }
        }
        if (!skip) {
          out_f_name = malloc(strlen((const char *)uc_heap_buf) + 1);
          memcpy(out_f_name, uc_heap_buf,
                 strlen((const char *)uc_heap_buf) + 1);
        }
      }
    }

    if (fread(buf, 1, 4, in_f) != 4) {
      return SDAS_INVALID_FILE;
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

    if ((buf[0] & 1) == 0) {
      // Not a sybolic link.
      if (fread(&u64, 8, 1, in_f) != 1) {
        return SDAS_INVALID_FILE;
      }
      simple_archiver_helper_64_bit_be(&u64);
      if (is_compressed) {
        fprintf(stderr, "  File size (compressed): %lu\n", u64);
      } else {
        fprintf(stderr, "  File size: %lu\n", u64);
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

        simple_archiver_helper_make_dirs((const char *)out_f_name);
        out_f = fopen(out_f_name, "wb");
        if (!out_f) {
          fprintf(stderr,
                  "WARNING: Failed to open \"%s\" for writing! (No write "
                  "permissions?)\n",
                  (char *)out_f_name);
        }
        __attribute__((
            cleanup(cleanup_temp_filename_delete))) void **ptrs_array =
            malloc(sizeof(void *) * 2);
        ptrs_array[0] = out_f_name;
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
          size_t fread_ret;
          char recv_buf[1024];
          size_t amount_to_read;
          while (!write_pipe_done || !read_pipe_done) {
            if (is_sig_pipe_occurred) {
              fprintf(stderr,
                      "WARNING: Failed to write to decompressor (SIGPIPE)! "
                      "Invalid decompressor cmd?\n");
              return 1;
            }

            // Read from file.
            if (!write_pipe_done) {
              if (!write_again && compressed_file_size != 0) {
                if (compressed_file_size > 1024) {
                  amount_to_read = 1024;
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
                    write_pipe_done = 1;
                  }
                } else if (write_ret == -1) {
                  if (errno == EAGAIN || errno == EWOULDBLOCK) {
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
              ssize_t read_ret = read(pipe_outof_cmd[0], recv_buf, 1024);
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
            if (compressed_file_size > 1024) {
              fread_ret = fread(buf, 1, 1024, in_f);
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

        if (chmod((const char *)out_f_name, permissions) == -1) {
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
          if (u64 > 1024) {
            size_t read_ret = fread(buf, 1, 1024, in_f);
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
      } else if (u16 < 1024) {
        if (fread(buf, 1, u16 + 1, in_f) != (size_t)u16 + 1) {
          return SDAS_INVALID_FILE;
        }
        buf[1023] = 0;
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
      } else if (u16 < 1024) {
        if (fread(buf, 1, u16 + 1, in_f) != (size_t)u16 + 1) {
          return SDAS_INVALID_FILE;
        }
        buf[1023] = 0;
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
        simple_archiver_helper_make_dirs((const char *)out_f_name);
        if (abs_path && rel_path) {
          if (abs_preferred) {
#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
            int_fast8_t retry_symlink = 0;
            int ret;
          V0_SYMLINK_CREATE_RETRY_0:
            ret = symlink(abs_path, out_f_name);
            if (ret == -1) {
              if (retry_symlink) {
                fprintf(stderr,
                        "WARNING: Failed to create symlink after removing "
                        "existing symlink!\n");
                goto V0_SYMLINK_CREATE_AFTER_0;
              } else if (errno == EEXIST) {
                if ((state->parsed->flags & 8) == 0) {
                  fprintf(
                      stderr,
                      "WARNING: Symlink already exists and "
                      "\"--overwrite-extract\" is not specified, skipping!\n");
                  goto V0_SYMLINK_CREATE_AFTER_0;
                } else {
                  fprintf(stderr,
                          "NOTICE: Symlink already exists and "
                          "\"--overwrite-extract\" specified, attempting to "
                          "overwrite...\n");
                  unlink(out_f_name);
                  retry_symlink = 1;
                  goto V0_SYMLINK_CREATE_RETRY_0;
                }
              } else {
                return SDAS_FAILED_TO_EXTRACT_SYMLINK;
              }
            }
            ret = fchmodat(AT_FDCWD, out_f_name, permissions,
                           AT_SYMLINK_NOFOLLOW);
            if (ret == -1) {
              if (errno == EOPNOTSUPP) {
                fprintf(stderr,
                        "NOTICE: Setting permissions of symlink is not "
                        "supported by FS/OS!\n");
              } else {
                fprintf(stderr,
                        "WARNING: Failed to set permissions of symlink (%d)!\n",
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
          V0_SYMLINK_CREATE_RETRY_1:
            ret = symlink(rel_path, out_f_name);
            if (ret == -1) {
              if (retry_symlink) {
                fprintf(stderr,
                        "WARNING: Failed to create symlink after removing "
                        "existing symlink!\n");
                goto V0_SYMLINK_CREATE_AFTER_1;
              } else if (errno == EEXIST) {
                if ((state->parsed->flags & 8) == 0) {
                  fprintf(
                      stderr,
                      "WARNING: Symlink already exists and "
                      "\"--overwrite-extract\" is not specified, skipping!\n");
                  goto V0_SYMLINK_CREATE_AFTER_1;
                } else {
                  fprintf(stderr,
                          "NOTICE: Symlink already exists and "
                          "\"--overwrite-extract\" specified, attempting to "
                          "overwrite...\n");
                  unlink(out_f_name);
                  retry_symlink = 1;
                  goto V0_SYMLINK_CREATE_RETRY_1;
                }
              } else {
                return SDAS_FAILED_TO_EXTRACT_SYMLINK;
              }
            }
            ret = fchmodat(AT_FDCWD, out_f_name, permissions,
                           AT_SYMLINK_NOFOLLOW);
            if (ret == -1) {
              if (errno == EOPNOTSUPP) {
                fprintf(stderr,
                        "NOTICE: Setting permissions of symlink is not "
                        "supported by FS/OS!\n");
              } else {
                fprintf(stderr,
                        "WARNING: Failed to set permissions of symlink (%d)!\n",
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
          int ret = symlink(abs_path, out_f_name);
          if (ret == -1) {
            return SDAS_FAILED_TO_EXTRACT_SYMLINK;
          }
          ret =
              fchmodat(AT_FDCWD, out_f_name, permissions, AT_SYMLINK_NOFOLLOW);
          if (ret == -1) {
            if (errno == EOPNOTSUPP) {
              fprintf(stderr,
                      "NOTICE: Setting permissions of symlink is not supported "
                      "by FS/OS!\n");
            } else {
              fprintf(stderr,
                      "WARNING: Failed to set permissions of symlink (%d)!\n",
                      errno);
            }
          }
#endif
        } else if (rel_path) {
#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
          int ret = symlink(rel_path, out_f_name);
          if (ret == -1) {
            return SDAS_FAILED_TO_EXTRACT_SYMLINK;
          }
          ret =
              fchmodat(AT_FDCWD, out_f_name, permissions, AT_SYMLINK_NOFOLLOW);
          if (ret == -1) {
            if (errno == EOPNOTSUPP) {
              fprintf(stderr,
                      "NOTICE: Setting permissions of symlink is not supported "
                      "by FS/OS!\n");
            } else {
              fprintf(stderr,
                      "WARNING: Failed to set permissions of symlink (%d)!\n",
                      errno);
            }
          }
#endif
        } else {
          fprintf(
              stderr,
              "WARNING: Symlink entry in archive has no paths to link to!\n");
        }
      }
    }
  }

  return SDAS_SUCCESS;
}

int simple_archiver_parse_archive_version_1(FILE *in_f, int_fast8_t do_extract,
                                            const SDArchiverState *state) {
  uint8_t buf[1024];
  memset(buf, 0, 1024);
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
        read_buf_full_from_fd(in_f, (char *)buf, 1024, u16 + 1, compressor_cmd);
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
    ret = read_buf_full_from_fd(in_f, (char *)buf, 1024, u16 + 1,
                                decompressor_cmd);
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

  // Link count.
  if (fread(buf, 1, 4, in_f) != 4) {
    return SDAS_INVALID_FILE;
  }
  memcpy(&u32, buf, 4);
  simple_archiver_helper_32_bit_be(&u32);

  for (uint32_t idx = 0; idx < u32; ++idx) {
    fprintf(stderr, "SYMLINK %3u of %3u\n", idx + 1, u32);
    if (fread(buf, 1, 2, in_f) != 2) {
      return SDAS_INVALID_FILE;
    }
    const uint_fast8_t absolute_preferred = (buf[0] & 1) ? 1 : 0;

#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
    mode_t permissions = permissions_from_bits_v1_symlink(buf, 0);
#endif

    uint_fast8_t link_extracted = 0;
    uint_fast8_t skip_due_to_map = 0;

    if (fread(buf, 1, 2, in_f) != 2) {
      return SDAS_INVALID_FILE;
    }
    memcpy(&u16, buf, 2);
    simple_archiver_helper_16_bit_be(&u16);

    __attribute__((
        cleanup(simple_archiver_helper_cleanup_c_string))) char *link_name =
        malloc(u16 + 1);

    int ret =
        read_buf_full_from_fd(in_f, (char *)buf, 1024, u16 + 1, link_name);
    if (ret != SDAS_SUCCESS) {
      return ret;
    }

    if (!do_extract) {
      fprintf(stderr, "  Link name: %s\n", link_name);
#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
      fprintf(stderr, "  Link Permissions: ");
      print_permissions(permissions);
      fprintf(stderr, "\n");
#endif
    }

    if (working_files_map &&
        simple_archiver_hash_map_get(working_files_map, link_name, u16 + 1) ==
            NULL) {
      skip_due_to_map = 1;
      fprintf(stderr, "  Skipping not specified in args...\n");
    }

    if (fread(buf, 1, 2, in_f) != 2) {
      return SDAS_INVALID_FILE;
    }
    memcpy(&u16, buf, 2);
    simple_archiver_helper_16_bit_be(&u16);
    if (u16 != 0) {
      __attribute__((
          cleanup(simple_archiver_helper_cleanup_c_string))) char *path =
          malloc(u16 + 1);
      ret = read_buf_full_from_fd(in_f, (char *)buf, 1024, u16 + 1, path);
      if (ret != SDAS_SUCCESS) {
        return ret;
      }
      path[u16] = 0;
      if (do_extract && !skip_due_to_map && absolute_preferred) {
#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
        simple_archiver_helper_make_dirs(link_name);
        int_fast8_t link_create_retry = 0;
      V1_SYMLINK_CREATE_RETRY_0:
        ret = symlink(path, link_name);
        if (ret == -1) {
          if (link_create_retry) {
            fprintf(stderr,
                    "WARNING: Failed to create symlink after removing existing "
                    "symlink!\n");
            goto V1_SYMLINK_CREATE_AFTER_0;
          } else if (errno == EEXIST) {
            if ((state->parsed->flags & 8) == 0) {
              fprintf(stderr,
                      "WARNING: Symlink already exists and "
                      "\"--overwrite-extract\" is not specified, skipping!\n");
              goto V1_SYMLINK_CREATE_AFTER_0;
            } else {
              fprintf(
                  stderr,
                  "NOTICE: Symlink already exists and \"--overwrite-extract\" "
                  "specified, attempting to overwrite...\n");
              unlink(link_name);
              link_create_retry = 1;
              goto V1_SYMLINK_CREATE_RETRY_0;
            }
          }
          return SDAS_FAILED_TO_EXTRACT_SYMLINK;
        }
        ret = fchmodat(AT_FDCWD, link_name, permissions, AT_SYMLINK_NOFOLLOW);
        if (ret == -1) {
          if (errno == EOPNOTSUPP) {
            fprintf(stderr,
                    "NOTICE: Setting permissions of symlink is not supported "
                    "by FS/OS!\n");
          } else {
            fprintf(stderr,
                    "WARNING: Failed to set permissions of symlink (%d)!\n",
                    errno);
          }
        }
        link_extracted = 1;
        fprintf(stderr, "  %s -> %s\n", link_name, path);
      V1_SYMLINK_CREATE_AFTER_0:
        link_create_retry = 1;
#endif
      } else {
        fprintf(stderr, "  Abs path: %s\n", path);
      }
    } else if (!do_extract) {
      fprintf(stderr, "  No Absolute path.\n");
    }

    if (fread(buf, 1, 2, in_f) != 2) {
      return SDAS_INVALID_FILE;
    }
    memcpy(&u16, buf, 2);
    simple_archiver_helper_16_bit_be(&u16);
    if (u16 != 0) {
      __attribute__((
          cleanup(simple_archiver_helper_cleanup_c_string))) char *path =
          malloc(u16 + 1);
      ret = read_buf_full_from_fd(in_f, (char *)buf, 1024, u16 + 1, path);
      if (ret != SDAS_SUCCESS) {
        return ret;
      }
      path[u16] = 0;
      if (do_extract && !skip_due_to_map && !absolute_preferred) {
#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
        simple_archiver_helper_make_dirs(link_name);
        int_fast8_t link_create_retry = 0;
      V1_SYMLINK_CREATE_RETRY_1:
        ret = symlink(path, link_name);
        if (ret == -1) {
          if (link_create_retry) {
            fprintf(stderr,
                    "WARNING: Failed to create symlink after removing existing "
                    "symlink!\n");
            goto V1_SYMLINK_CREATE_AFTER_1;
          } else if (errno == EEXIST) {
            if ((state->parsed->flags & 8) == 0) {
              fprintf(stderr,
                      "WARNING: Symlink already exists and "
                      "\"--overwrite-extract\" is not specified, skipping!\n");
              goto V1_SYMLINK_CREATE_AFTER_1;
            } else {
              fprintf(
                  stderr,
                  "NOTICE: Symlink already exists and \"--overwrite-extract\" "
                  "specified, attempting to overwrite...\n");
              unlink(link_name);
              link_create_retry = 1;
              goto V1_SYMLINK_CREATE_RETRY_1;
            }
          }
          return SDAS_FAILED_TO_EXTRACT_SYMLINK;
        }
        ret = fchmodat(AT_FDCWD, link_name, permissions, AT_SYMLINK_NOFOLLOW);
        if (ret == -1) {
          if (errno == EOPNOTSUPP) {
            fprintf(stderr,
                    "NOTICE: Setting permissions of symlink is not supported "
                    "by FS/OS!\n");
          } else {
            fprintf(stderr,
                    "WARNING: Failed to set permissions of symlink (%d)!\n",
                    errno);
          }
        }
        link_extracted = 1;
        fprintf(stderr, "  %s -> %s\n", link_name, path);
      V1_SYMLINK_CREATE_AFTER_1:
        link_create_retry = 1;
#endif
      } else {
        fprintf(stderr, "  Rel path: %s\n", path);
      }
    } else if (!do_extract) {
      fprintf(stderr, "  No Relative path.\n");
    }

    if (do_extract && !link_extracted && !skip_due_to_map) {
      fprintf(stderr, "WARNING Symlink \"%s\" was not created!\n", link_name);
    }
  }

  if (fread(buf, 1, 4, in_f) != 4) {
    return SDAS_INVALID_FILE;
  }
  memcpy(&u32, buf, 4);
  simple_archiver_helper_32_bit_be(&u32);

  const uint32_t chunk_count = u32;
  for (uint32_t chunk_idx = 0; chunk_idx < chunk_count; ++chunk_idx) {
    fprintf(stderr, "CHUNK %3u of %3u\n", chunk_idx + 1, chunk_count);

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
      int ret = read_buf_full_from_fd(in_f, (char *)buf, 1024, u16 + 1,
                                      file_info->filename);
      if (ret != SDAS_SUCCESS) {
        return ret;
      }
      file_info->filename[u16] = 0;

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
      file_info->uid = u32;

      if (fread(buf, 1, 4, in_f) != 4) {
        return SDAS_INVALID_FILE;
      }
      memcpy(&u32, buf, 4);
      simple_archiver_helper_32_bit_be(&u32);
      file_info->gid = u32;

      if (fread(buf, 1, 8, in_f) != 8) {
        return SDAS_INVALID_FILE;
      }
      memcpy(&u64, buf, 8);
      simple_archiver_helper_64_bit_be(&u64);
      file_info->file_size = u64;

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
    uint64_t chunk_idx = 0;

    SDArchiverLLNode *node = file_info_list->head;
    uint16_t file_idx = 0;

#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
    if (is_compressed) {
      // Start the decompressing process and read into files.

      // Handle SIGPIPE.
      signal(SIGPIPE, handle_sig_pipe);

      int pipe_into_cmd[2];
      int pipe_outof_cmd[2];
      __attribute__((cleanup(
          simple_archiver_internal_cleanup_decomp))) pid_t decompressor_pid;
      if (pipe(pipe_into_cmd) != 0) {
        // Unable to create pipes.
        break;
      } else if (pipe(pipe_outof_cmd) != 0) {
        // Unable to create second set of pipes.
        close(pipe_into_cmd[0]);
        close(pipe_into_cmd[1]);
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
          simple_archiver_internal_cleanup_int_fd))) int pipe_into_write =
          pipe_into_cmd[1];
      __attribute__((cleanup(
          simple_archiver_internal_cleanup_int_fd))) int pipe_outof_read =
          pipe_outof_cmd[0];

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

      // Write all of chunk into decompressor.
      uint64_t chunk_written = 0;
      while (chunk_written < chunk_size) {
        if (is_sig_pipe_occurred) {
          fprintf(stderr,
                  "WARNING: Failed to write to decompressor (SIGPIPE)! Invalid "
                  "decompressor cmd?\n");
          return SDAS_INTERNAL_ERROR;
        } else if (chunk_size - chunk_written >= 1024) {
          if (fread(buf, 1, 1024, in_f) != 1024) {
            fprintf(stderr, "ERROR Failed to read chunk for decompressing!\n");
            return SDAS_INTERNAL_ERROR;
          }
          ssize_t write_ret = write(pipe_into_cmd[1], buf, 1024);
          if (write_ret > 0 && (size_t)write_ret == 1024) {
            // Successful write.
          } else if (write_ret == -1) {
            fprintf(stderr,
                    "WARNING: Failed to write chunk data into decompressor! "
                    "Invalid decompressor cmd? (errno %d)\n",
                    errno);
            return SDAS_INTERNAL_ERROR;
          } else {
            fprintf(stderr,
                    "WARNING: Failed to write chunk data into decompressor! "
                    "Invalid decompressor cmd?\n");
            return SDAS_INTERNAL_ERROR;
          }
          chunk_written += 1024;
        } else {
          if (fread(buf, 1, chunk_size - chunk_written, in_f) !=
              chunk_size - chunk_written) {
            fprintf(stderr, "ERROR Failed to read chunk for decompressing!\n");
            return SDAS_INTERNAL_ERROR;
          }
          ssize_t write_ret =
              write(pipe_into_cmd[1], buf, chunk_size - chunk_written);
          if (write_ret > 0 &&
              (size_t)write_ret == chunk_size - chunk_written) {
            // Successful write.
          } else if (write_ret == -1) {
            fprintf(stderr,
                    "WARNING: Failed to write chunk data into decompressor! "
                    "Invalid decompressor cmd?\n");
            return SDAS_INTERNAL_ERROR;
          } else {
            fprintf(stderr,
                    "WARNING: Failed to write chunk data into decompressor! "
                    "Invalid decompressor cmd?\n");
            return SDAS_INTERNAL_ERROR;
          }
          chunk_written = chunk_size;
        }
      }

      simple_archiver_internal_cleanup_int_fd(&pipe_into_write);

      while (node->next != file_info_list->tail) {
        node = node->next;
        const SDArchiverInternalFileInfo *file_info = node->data;
        fprintf(stderr, "  FILE %3u of %3u\n", ++file_idx, file_count);
        fprintf(stderr, "    Filename: %s\n", file_info->filename);

        uint_fast8_t skip_due_to_map = 0;
        if (working_files_map && simple_archiver_hash_map_get(
                                     working_files_map, file_info->filename,
                                     strlen(file_info->filename) + 1) == NULL) {
          skip_due_to_map = 1;
          fprintf(stderr, "    Skipping not specified in args...\n");
        }

        if (do_extract && !skip_due_to_map) {
#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
          mode_t permissions =
              permissions_from_bits_version_1(file_info->bit_flags, 0);
#endif
          if ((state->parsed->flags & 8) == 0) {
            // Check if file already exists.
            __attribute__((cleanup(simple_archiver_helper_cleanup_FILE)))
            FILE *temp_fd = fopen(file_info->filename, "r");
            if (temp_fd) {
              fprintf(stderr,
                      "  WARNING: File already exists and "
                      "\"--overwrite-extract\" is not specified, skipping!\n");
              read_decomp_to_out_file(NULL, pipe_outof_cmd[0], (char *)buf,
                                      1024, file_info->file_size);
              continue;
            }
          }

          simple_archiver_helper_make_dirs(file_info->filename);
          int ret =
              read_decomp_to_out_file(file_info->filename, pipe_outof_cmd[0],
                                      (char *)buf, 1024, file_info->file_size);
          if (ret != SDAS_SUCCESS) {
            return ret;
          }
#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
          if (chmod(file_info->filename, permissions) == -1) {
            return SDAS_INTERNAL_ERROR;
          } else if (geteuid() == 0 &&
                     chown(file_info->filename, file_info->uid,
                           file_info->gid) != 0) {
            fprintf(stderr,
                    "ERROR Failed to set UID/GID as EUID 0 of file \"%s\"!\n",
                    file_info->filename);
            return SDAS_INTERNAL_ERROR;
          }
#endif
        } else if (!skip_due_to_map) {
          fprintf(stderr, "    Permissions: ");
          permissions_from_bits_version_1(file_info->bit_flags, 1);
          fprintf(stderr, "\n    UID: %u\n    GID: %u\n", file_info->uid,
                  file_info->gid);
          if (is_compressed) {
            fprintf(stderr, "    File size (uncompressed): %lu\n",
                    file_info->file_size);
          } else {
            fprintf(stderr, "    File size: %lu\n", file_info->file_size);
          }
          int ret = read_decomp_to_out_file(
              NULL, pipe_outof_cmd[0], (char *)buf, 1024, file_info->file_size);
          if (ret != SDAS_SUCCESS) {
            return ret;
          }
        } else {
          int ret = read_decomp_to_out_file(
              NULL, pipe_outof_cmd[0], (char *)buf, 1024, file_info->file_size);
          if (ret != SDAS_SUCCESS) {
            return ret;
          }
        }
      }

      // Ensure EOF is left from pipe.
      ssize_t read_ret = read(pipe_outof_cmd[0], buf, 1024);
      if (read_ret != 0) {
        fprintf(stderr, "WARNING decompressor didn't reach EOF!\n");
      }
    } else {
#else
    // } (This comment exists so that vim can correctly match curly-braces.
    if (!is_compressed) {
#endif
      while (node->next != file_info_list->tail) {
        node = node->next;
        const SDArchiverInternalFileInfo *file_info = node->data;
        fprintf(stderr, "  FILE %3u of %3u\n", ++file_idx, file_count);
        fprintf(stderr, "    Filename: %s\n", file_info->filename);
        chunk_idx += file_info->file_size;
        if (chunk_idx > chunk_size) {
          fprintf(stderr, "ERROR Files in chunk is larger than chunk!\n");
          return SDAS_INTERNAL_ERROR;
        }

        uint_fast8_t skip_due_to_map = 0;
        if (working_files_map && simple_archiver_hash_map_get(
                                     working_files_map, file_info->filename,
                                     strlen(file_info->filename) + 1) == NULL) {
          skip_due_to_map = 1;
          fprintf(stderr, "    Skipping not specified in args...\n");
        }

        if (do_extract && !skip_due_to_map) {
#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
          mode_t permissions =
              permissions_from_bits_version_1(file_info->bit_flags, 0);
#endif
          if ((state->parsed->flags & 8) == 0) {
            // Check if file already exists.
            __attribute__((cleanup(simple_archiver_helper_cleanup_FILE)))
            FILE *temp_fd = fopen(file_info->filename, "r");
            if (temp_fd) {
              fprintf(stderr,
                      "  WARNING: File already exists and "
                      "\"--overwrite-extract\" is not specified, skipping!\n");
              int ret = read_buf_full_from_fd(in_f, (char *)buf, 1024,
                                              file_info->file_size, NULL);
              if (ret != SDAS_SUCCESS) {
                return ret;
              }
              continue;
            }
          }
          simple_archiver_helper_make_dirs(file_info->filename);
          __attribute__((cleanup(simple_archiver_helper_cleanup_FILE)))
          FILE *out_fd = fopen(file_info->filename, "wb");
          int ret = read_fd_to_out_fd(in_f, out_fd, (char *)buf, 1024,
                                      file_info->file_size);
          if (ret != SDAS_SUCCESS) {
            return ret;
          }
          simple_archiver_helper_cleanup_FILE(&out_fd);
#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
          if (chmod(file_info->filename, permissions) == -1) {
            return SDAS_INTERNAL_ERROR;
          } else if (geteuid() == 0 &&
                     chown(file_info->filename, file_info->uid,
                           file_info->gid) != 0) {
            fprintf(stderr,
                    "ERROR Failed to set UID/GID as EUID 0 of file \"%s\"!\n",
                    file_info->filename);
            return SDAS_INTERNAL_ERROR;
          }
#endif
        } else if (!skip_due_to_map) {
          fprintf(stderr, "    Permissions: ");
          permissions_from_bits_version_1(file_info->bit_flags, 1);
          fprintf(stderr, "\n    UID: %u\n    GID: %u\n", file_info->uid,
                  file_info->gid);
          if (is_compressed) {
            fprintf(stderr, "    File size (compressed): %lu\n",
                    file_info->file_size);
          } else {
            fprintf(stderr, "    File size: %lu\n", file_info->file_size);
          }
          int ret = read_buf_full_from_fd(in_f, (char *)buf, 1024,
                                          file_info->file_size, NULL);
          if (ret != SDAS_SUCCESS) {
            return ret;
          }
        } else {
          int ret = read_buf_full_from_fd(in_f, (char *)buf, 1024,
                                          file_info->file_size, NULL);
          if (ret != SDAS_SUCCESS) {
            return ret;
          }
        }
      }
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
      char *new_rel_path = malloc(strlen(rel_path) + 1 + 3);
      new_rel_path[0] = '.';
      new_rel_path[1] = '.';
      new_rel_path[2] = '/';
      strncpy(new_rel_path + 3, rel_path, strlen(rel_path) + 1);
      free(rel_path);
      rel_path = new_rel_path;
      ++idx;
    }
  } while (has_slash);

  return rel_path;
}

char *simple_archiver_file_abs_path(const char *filename) {
#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
  __attribute__((cleanup(simple_archiver_helper_cleanup_malloced))) void *path =
      malloc(strlen(filename) + 1);
  strncpy(path, filename, strlen(filename) + 1);

  char *path_dir = dirname(path);
  if (!path_dir) {
    return NULL;
  }

  __attribute__((
      cleanup(simple_archiver_helper_cleanup_malloced))) void *dir_realpath =
      realpath(path_dir, NULL);
  if (!dir_realpath) {
    return NULL;
  }

  // Recreate "path" since it may have been modified by dirname().
  simple_archiver_helper_cleanup_malloced(&path);
  path = malloc(strlen(filename) + 1);
  strncpy(path, filename, strlen(filename) + 1);

  char *filename_basename = basename(path);
  if (!filename_basename) {
    return NULL;
  }

  // Get combined full path to file.
  char *fullpath =
      malloc(strlen(dir_realpath) + 1 + strlen(filename_basename) + 1);
  strncpy(fullpath, dir_realpath, strlen(dir_realpath) + 1);
  fullpath[strlen(dir_realpath)] = '/';
  strncpy(fullpath + strlen(dir_realpath) + 1, filename_basename,
          strlen(filename_basename) + 1);

  return fullpath;
#endif
  return NULL;
}
