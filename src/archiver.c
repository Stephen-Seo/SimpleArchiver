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

#include "helpers.h"

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

int write_files_fn(void *data, void *ud) {
  const SDArchiverFileInfo *file_info = data;
  const SDArchiverState *state = ud;

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
      // TODO
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

  return 0;
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
  state->out_f = out_f;
  if (simple_archiver_list_get(filenames, write_files_fn, state)) {
    // Error occurred.
  }
  state->out_f = NULL;

  return SDAS_SUCCESS;
}