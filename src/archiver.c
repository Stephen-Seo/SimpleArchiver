/*
 * Copyright 2024 Stephen Seo
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * `archiver.c` is the source for an interface to creating an archive file.
 */

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
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "data_structures/hash_map.h"
#include "helpers.h"

#define TEMP_FILENAME_CMP "simple_archiver_compressed_%u.tmp"

typedef struct SDArchiverInternalToWrite {
  void *buf;
  uint64_t size;
} SDArchiverInternalToWrite;

void free_FILE_helper(FILE **fd) {
  if (fd && *fd) {
    fclose(*fd);
    *fd = NULL;
  }
}

void free_malloced_memory(void **data) {
  if (data && *data) {
    free(*data);
    *data = NULL;
  }
}

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
      free_FILE_helper((FILE **)(*ptrs_array)[1]);
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
      char temp_filename[128];
      unsigned int idx = 0;
      snprintf(temp_filename, 128, TEMP_FILENAME_CMP, idx);
      do {
        FILE *test_fd = fopen(temp_filename, "rb");
        if (test_fd) {
          // File exists.
          fclose(test_fd);
          snprintf(temp_filename, 128, TEMP_FILENAME_CMP, ++idx);
        } else if (idx > 0xFFFF) {
          // Sanity check.
          return 1;
        } else {
          break;
        }
      } while (1);
      __attribute__((cleanup(free_FILE_helper))) FILE *file_fd =
          fopen(file_info->filename, "rb");
      if (!file_fd) {
        // Unable to open file for compressing and archiving.
        return 1;
      }
      __attribute__((cleanup(free_FILE_helper))) FILE *tmp_fd =
          fopen(temp_filename, "wb");
      __attribute__((cleanup(cleanup_temp_filename_delete))) void **ptrs_array =
          malloc(sizeof(void *) * 2);
      ptrs_array[0] = temp_filename;
      ptrs_array[1] = &tmp_fd;
      if (!tmp_fd) {
        // Unable to create temp file.
        return 1;
      }

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
      int write_again = 0;
      int write_done = 0;
      int read_done = 0;
      size_t write_count;
      size_t read_count;
      ssize_t ret;
      while (!write_done || !read_done) {
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
                return 1;
              }
            } else if ((size_t)ret != write_count) {
              // Error during write.
              return 1;
            } else {
              write_again = 0;
              // fprintf(stderr, "Written %zd bytes to comp.\n", ret);
            }
          } else {
            if (feof(file_fd)) {
              free_FILE_helper(&file_fd);
              write_done = 1;
              close(pipe_into_cmd[1]);
              // fprintf(stderr, "write_done\n");
            } else if (ferror(file_fd)) {
              // Error during read file.
              return 1;
            }
          }
        }

        // Read from compressor.
        if (!read_done) {
          ret = read(pipe_outof_cmd[0], read_buf, 1024);
          if (ret > 0) {
            read_count = fwrite(read_buf, 1, ret, tmp_fd);
            if (read_count != (size_t)ret) {
              // Write to tmp_fd error.
              return 1;
            } else {
              // fprintf(stderr, "Written %zd bytes to tmp_fd.\n", read_count);
            }
          } else if (ret == 0) {
            read_done = 1;
            free_FILE_helper(&tmp_fd);
            close(pipe_outof_cmd[0]);
            // fprintf(stderr, "read_done\n");
          } else if (ret == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
              // Nop.
            } else {
              // Read error.
              return 1;
            }
          } else {
            // Nop. Technically this branch should be unreachable.
          }
        }
      }

      waitpid(compressor_pid, NULL, 0);

      uint16_t u16;
      uint64_t u64;

      u16 = strlen(file_info->filename);

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
      for (unsigned int idx = 0; idx < temp_to_write->size; ++idx) {
        ((unsigned char *)temp_to_write->buf)[idx] = 0;
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
        ((unsigned char *)temp_to_write->buf)[0] |= 0x2;
      }
      if ((stat_buf.st_mode & S_IWUSR) != 0) {
        ((unsigned char *)temp_to_write->buf)[0] |= 0x4;
      }
      if ((stat_buf.st_mode & S_IXUSR) != 0) {
        ((unsigned char *)temp_to_write->buf)[0] |= 0x8;
      }
      if ((stat_buf.st_mode & S_IRGRP) != 0) {
        ((unsigned char *)temp_to_write->buf)[0] |= 0x10;
      }
      if ((stat_buf.st_mode & S_IWGRP) != 0) {
        ((unsigned char *)temp_to_write->buf)[0] |= 0x20;
      }
      if ((stat_buf.st_mode & S_IXGRP) != 0) {
        ((unsigned char *)temp_to_write->buf)[0] |= 0x40;
      }
      if ((stat_buf.st_mode & S_IROTH) != 0) {
        ((unsigned char *)temp_to_write->buf)[0] |= 0x80;
      }
      if ((stat_buf.st_mode & S_IWOTH) != 0) {
        ((unsigned char *)temp_to_write->buf)[1] |= 0x1;
      }
      if ((stat_buf.st_mode & S_IXOTH) != 0) {
        ((unsigned char *)temp_to_write->buf)[1] |= 0x2;
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
      u64 = end;
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
      free_FILE_helper(&tmp_fd);
#endif
    } else {
      uint16_t u16;
      uint64_t u64;

      u16 = strlen(file_info->filename);

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
      for (unsigned int idx = 0; idx < temp_to_write->size; ++idx) {
        ((unsigned char *)temp_to_write->buf)[idx] = 0;
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
        return 1;
      }
      int stat_status = fstat(stat_fd, &stat_buf);
      close(stat_fd);
      if (stat_status != 0) {
        // Error.
        return 1;
      }

      if ((stat_buf.st_mode & S_IRUSR) != 0) {
        ((unsigned char *)temp_to_write->buf)[0] |= 0x2;
      }
      if ((stat_buf.st_mode & S_IWUSR) != 0) {
        ((unsigned char *)temp_to_write->buf)[0] |= 0x4;
      }
      if ((stat_buf.st_mode & S_IXUSR) != 0) {
        ((unsigned char *)temp_to_write->buf)[0] |= 0x8;
      }
      if ((stat_buf.st_mode & S_IRGRP) != 0) {
        ((unsigned char *)temp_to_write->buf)[0] |= 0x10;
      }
      if ((stat_buf.st_mode & S_IWGRP) != 0) {
        ((unsigned char *)temp_to_write->buf)[0] |= 0x20;
      }
      if ((stat_buf.st_mode & S_IXGRP) != 0) {
        ((unsigned char *)temp_to_write->buf)[0] |= 0x40;
      }
      if ((stat_buf.st_mode & S_IROTH) != 0) {
        ((unsigned char *)temp_to_write->buf)[0] |= 0x80;
      }
      if ((stat_buf.st_mode & S_IWOTH) != 0) {
        ((unsigned char *)temp_to_write->buf)[1] |= 0x1;
      }
      if ((stat_buf.st_mode & S_IXOTH) != 0) {
        ((unsigned char *)temp_to_write->buf)[1] |= 0x2;
      }
#endif

      simple_archiver_list_add(to_write, temp_to_write, free_internal_to_write);

      // Write file length.
      __attribute__((cleanup(free_FILE_helper))) FILE *fd =
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
      u64 = end;
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
    // TODO
  }

  fprintf(stderr, "[%10u/%10u]\n", ++(state->count), state->max);
  return 0;
}

void cleanup_nop_fn(__attribute__((unused)) void *unused) {}
void cleanup_free_fn(void *data) { free(data); }

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
    unsigned char c = 1;
    if (fwrite(&c, 1, 1, out_f) != 1) {
      return SDAS_FAILED_TO_WRITE;
    }
    c = 0;
    for (unsigned int i = 0; i < 3; ++i) {
      if (fwrite(&c, 1, 1, out_f) != 1) {
        return SDAS_FAILED_TO_WRITE;
      }
    }

    // De/compressor bytes.
    u16 = strlen(state->parsed->compressor);
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

    u16 = strlen(state->parsed->decompressor);
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
    unsigned char c = 0;
    for (unsigned int i = 0; i < 4; ++i) {
      if (fwrite(&c, 1, 1, out_f) != 1) {
        return SDAS_FAILED_TO_WRITE;
      }
    }
  }

  // Write file count.
  {
    uint32_t u32 = filenames->count;
    simple_archiver_helper_32_bit_be(&u32);
    if (fwrite(&u32, 1, 4, out_f) != 4) {
      return SDAS_FAILED_TO_WRITE;
    }
  }

  // Iterate over files in list to write.
  state->count = 0;
  state->max = filenames->count;
  state->out_f = out_f;
  fprintf(stderr, "Begin archiving...\n");
  fprintf(stderr, "[%10u/%10u]\n", state->count, state->max);
  if (simple_archiver_list_get(filenames, write_files_fn, state)) {
    // Error occurred.
    fprintf(stderr, "Error ocurred writing file(s) to archive.\n");
    return SDAS_FAILED_TO_WRITE;
  }
  state->out_f = NULL;

  fprintf(stderr, "End archiving.\n");
  return SDAS_SUCCESS;
}

int simple_archiver_parse_archive_info(FILE *in_f, int do_extract,
                                       const SDArchiverState *state) {
  unsigned char buf[1024];
  memset(buf, 0, 1024);
  uint16_t u16;
  uint32_t u32;
  uint64_t u64;
  int is_compressed = 0;

  if (fread(buf, 1, 18, in_f) != 18) {
    return SDAS_INVALID_FILE;
  } else if (memcmp(buf, "SIMPLE_ARCHIVE_VER", 18) != 0) {
    return SDAS_INVALID_FILE;
  } else if (fread(buf, 1, 2, in_f) != 2) {
    return SDAS_INVALID_FILE;
  } else if (buf[0] != 0 || buf[1] != 0) {
    // Version is not zero.
    return SDAS_INVALID_FILE;
  } else if (fread(buf, 1, 4, in_f) != 4) {
    return SDAS_INVALID_FILE;
  }

  __attribute__((cleanup(free_malloced_memory))) void *decompressor_cmd = NULL;

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
      __attribute__((cleanup(free_malloced_memory))) void *heap_buf =
          malloc(u16 + 1);
      unsigned char *uc_heap_buf = heap_buf;
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
      __attribute__((cleanup(free_malloced_memory))) void *heap_buf =
          malloc(u16 + 1);
      unsigned char *uc_heap_buf = heap_buf;
      if (fread(uc_heap_buf, 1, u16 + 1, in_f) != (size_t)u16 + 1) {
        return SDAS_INVALID_FILE;
      }
      uc_heap_buf[u16 - 1] = 0;
      fprintf(stderr, "Decompressor cmd: %s\n", uc_heap_buf);
    }
  } else {
    fprintf(stderr, "De/compressor flag is NOT set.\n");
  }

  if (fread(&u32, 1, 4, in_f) != 4) {
    return SDAS_INVALID_FILE;
  }
  simple_archiver_helper_32_bit_be(&u32);
  fprintf(stderr, "File count is %u\n", u32);

  uint32_t size = u32;
  int skip = 0;
  __attribute__((cleanup(simple_archiver_hash_map_free)))
  SDArchiverHashMap *hash_map = NULL;
  if (state && state->parsed->working_files &&
      state->parsed->working_files[0] != NULL) {
    hash_map = simple_archiver_hash_map_init();
    for (char **iter = state->parsed->working_files; *iter != NULL; ++iter) {
      int len = strlen(*iter) + 1;
      char *key = malloc(len);
      memcpy(key, *iter, len);
      key[len - 1] = 0;
      simple_archiver_hash_map_insert(&hash_map, key, key, len, cleanup_nop_fn,
                                      cleanup_free_fn);
      // fprintf(stderr, "\"%s\" put in map\n", key);
    }
  }
  for (uint32_t idx = 0; idx < size; ++idx) {
    fprintf(stderr, "\nFile %10u of %10u.\n", idx + 1, size);
    if (feof(in_f) || ferror(in_f)) {
      return SDAS_INVALID_FILE;
    } else if (fread(&u16, 2, 1, in_f) != 1) {
      return SDAS_INVALID_FILE;
    }
    simple_archiver_helper_16_bit_be(&u16);
    __attribute__((cleanup(free_malloced_memory))) void *out_f_name = NULL;
    __attribute__((cleanup(free_FILE_helper))) FILE *out_f = NULL;
    if (u16 < 1024) {
      if (fread(buf, 1, u16 + 1, in_f) != (size_t)u16 + 1) {
        return SDAS_INVALID_FILE;
      }
      buf[1023] = 0;
      fprintf(stderr, "  Filename: %s\n", buf);
      if (do_extract) {
        if ((state->parsed->flags & 0x8) == 0) {
          __attribute__((cleanup(free_FILE_helper))) FILE *test_fd =
              fopen((const char *)buf, "rb");
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
        }
        if (!skip) {
          out_f_name = malloc(strlen((const char *)buf) + 1);
          memcpy(out_f_name, buf, strlen((const char *)buf) + 1);
        }
      }
    } else {
      __attribute__((cleanup(free_malloced_memory))) void *heap_buf =
          malloc(u16 + 1);
      unsigned char *uc_heap_buf = heap_buf;
      if (fread(uc_heap_buf, 1, u16 + 1, in_f) != (size_t)u16 + 1) {
        return SDAS_INVALID_FILE;
      }
      uc_heap_buf[u16 - 1] = 0;
      fprintf(stderr, "  Filename: %s\n", uc_heap_buf);
      if (do_extract) {
        if ((state->parsed->flags & 0x8) == 0) {
          __attribute__((cleanup(free_FILE_helper))) FILE *test_fd =
              fopen((const char *)buf, "rb");
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
        }
        if (!skip) {
          out_f_name = malloc(strlen((const char *)buf) + 1);
          memcpy(out_f_name, buf, strlen((const char *)buf) + 1);
        }
      }
    }

    if (fread(buf, 1, 4, in_f) != 4) {
      return SDAS_INVALID_FILE;
    }

    unsigned int permissions = 0;
#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX

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

      int skip_due_to_map = 0;
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
        __attribute__((
            cleanup(cleanup_temp_filename_delete))) void **ptrs_array =
            malloc(sizeof(void *) * 2);
        ptrs_array[0] = out_f_name;
        ptrs_array[1] = &out_f;
#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
        if (is_compressed) {
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
          int write_again = 0;
          int write_pipe_done = 0;
          int read_pipe_done = 0;
          ssize_t fread_ret;
          char recv_buf[1024];
          size_t amount_to_read;
          while (!write_pipe_done || !read_pipe_done) {
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
                if (write_ret == fread_ret) {
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
                    return SDAS_INTERNAL_ERROR;
                  }
                } else {
                  // Should be unreachable, error.
                  return SDAS_INTERNAL_ERROR;
                }
              }
            }

            // Read output from decompressor and write to file.
            if (!read_pipe_done) {
              ssize_t read_ret = read(pipe_outof_cmd[0], recv_buf, 1024);
              if (read_ret > 0) {
                size_t fwrite_ret = fwrite(recv_buf, 1, read_ret, out_f);
                if (fwrite_ret == (size_t)read_ret) {
                  // Success.
                } else if (ferror(out_f)) {
                  // Error.
                  return SDAS_INTERNAL_ERROR;
                } else {
                  // Invalid state, error.
                  return SDAS_INTERNAL_ERROR;
                }
              } else if (read_ret == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                  // No bytes to read yet.
                } else {
                  // Error.
                  return SDAS_INTERNAL_ERROR;
                }
              } else if (read_ret == 0) {
                // EOF.
                read_pipe_done = 1;
                close(pipe_outof_cmd[0]);
                free_FILE_helper(&out_f);
              } else {
                // Invalid state (unreachable?), error.
                return SDAS_INTERNAL_ERROR;
              }
            }
          }

          waitpid(decompressor_pid, NULL, 0);
        } else {
          uint64_t compressed_file_size = u64;
          ssize_t fread_ret;
          while (compressed_file_size != 0) {
            if (compressed_file_size > 1024) {
              fread_ret = fread(buf, 1, 1024, in_f);
              if (ferror(in_f)) {
                // Error.
                return SDAS_INTERNAL_ERROR;
              }
              fwrite(buf, 1, fread_ret, out_f);
              if (ferror(out_f)) {
                // Error.
                return SDAS_INTERNAL_ERROR;
              }
              compressed_file_size -= fread_ret;
            } else {
              fread_ret = fread(buf, 1, compressed_file_size, in_f);
              if (ferror(in_f)) {
                // Error.
                return SDAS_INTERNAL_ERROR;
              }
              fwrite(buf, 1, fread_ret, out_f);
              if (ferror(out_f)) {
                // Error.
                return SDAS_INTERNAL_ERROR;
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
        fprintf(stderr, "  Extracted.\n");
#endif
      } else {
        while (u64 != 0) {
          if (u64 > 1024) {
            ssize_t read_ret = fread(buf, 1, 1024, in_f);
            if (read_ret > 0) {
              u64 -= read_ret;
            } else if (ferror(in_f)) {
              return SDAS_INTERNAL_ERROR;
            }
          } else {
            ssize_t read_ret = fread(buf, 1, u64, in_f);
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
      if (fread(&u16, 2, 1, in_f) != 1) {
        return SDAS_INVALID_FILE;
      }
      simple_archiver_helper_16_bit_be(&u16);
      if (u16 < 1024) {
        if (fread(buf, 1, u16 + 1, in_f) != (size_t)u16 + 1) {
          return SDAS_INVALID_FILE;
        }
        buf[1023] = 0;
        fprintf(stderr, "  Link absolute path: %s\n", buf);
      } else {
        __attribute__((cleanup(free_malloced_memory))) void *heap_buf =
            malloc(u16 + 1);
        unsigned char *uc_heap_buf = heap_buf;
        if (fread(uc_heap_buf, 1, u16 + 1, in_f) != (size_t)u16 + 1) {
          return SDAS_INVALID_FILE;
        }
        uc_heap_buf[u16 - 1] = 0;
        fprintf(stderr, "  Link absolute path: %s\n", uc_heap_buf);
      }

      if (fread(&u16, 2, 1, in_f) != 1) {
        return SDAS_INVALID_FILE;
      }
      simple_archiver_helper_16_bit_be(&u16);
      if (u16 < 1024) {
        if (fread(buf, 1, u16 + 1, in_f) != (size_t)u16 + 1) {
          return SDAS_INVALID_FILE;
        }
        buf[1023] = 0;
        fprintf(stderr, "  Link relative path: %s\n", buf);
      } else {
        __attribute__((cleanup(free_malloced_memory))) void *heap_buf =
            malloc(u16 + 1);
        unsigned char *uc_heap_buf = heap_buf;
        if (fread(uc_heap_buf, 1, u16 + 1, in_f) != (size_t)u16 + 1) {
          return SDAS_INVALID_FILE;
        }
        uc_heap_buf[u16 - 1] = 0;
        fprintf(stderr, "  Link relative path: %s\n", uc_heap_buf);
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
