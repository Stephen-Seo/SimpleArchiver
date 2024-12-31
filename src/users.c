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
// `users.c` is a source file for getting the current system's user/group info.

#include "users.h"
#include "data_structures/hash_map.h"

// C standard library includes
#include <sys/types.h>
#include <pwd.h>
#include <grp.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

void simple_archiver_users_free_users_infos(UsersInfos *users_infos) {
  if (users_infos) {
    if (users_infos->UidToUname) {
      simple_archiver_hash_map_free_single_ptr(users_infos->UidToUname);
      users_infos->UidToUname = 0;
    }
    if (users_infos->UnameToUid) {
      simple_archiver_hash_map_free_single_ptr(users_infos->UnameToUid);
      users_infos->UnameToUid = 0;
    }
    if (users_infos->GidToGname) {
      simple_archiver_hash_map_free_single_ptr(users_infos->GidToGname);
      users_infos->GidToGname = 0;
    }
    if (users_infos->GnameToGid) {
      simple_archiver_hash_map_free_single_ptr(users_infos->GnameToGid);
      users_infos->GnameToGid = 0;
    }
  }
}

UsersInfos simple_archiver_users_get_system_info(void) {
  UsersInfos users_infos;
  users_infos.UidToUname = simple_archiver_hash_map_init();
  users_infos.UnameToUid = simple_archiver_hash_map_init();
  users_infos.GidToGname = simple_archiver_hash_map_init();
  users_infos.GnameToGid = simple_archiver_hash_map_init();

  struct passwd *passwd_info = getpwent();
  uint32_t *u32_ptr;
  while (passwd_info) {
    u32_ptr = malloc(sizeof(uint32_t));
    *u32_ptr = passwd_info->pw_uid;
    simple_archiver_hash_map_insert(users_infos.UidToUname,
                                    strdup(passwd_info->pw_name),
                                    u32_ptr,
                                    sizeof(uint32_t),
                                    NULL,
                                    NULL);
    u32_ptr = malloc(sizeof(uint32_t));
    *u32_ptr = passwd_info->pw_uid;
    simple_archiver_hash_map_insert(users_infos.UnameToUid,
                                    u32_ptr,
                                    strdup(passwd_info->pw_name),
                                    strlen(passwd_info->pw_name) + 1,
                                    NULL,
                                    NULL);
    passwd_info = getpwent();
  }
  endpwent();

  struct group *group_info = getgrent();
  while(group_info) {
    u32_ptr = malloc(sizeof(uint32_t));
    *u32_ptr = group_info->gr_gid;
    simple_archiver_hash_map_insert(users_infos.GidToGname,
                                    strdup(group_info->gr_name),
                                    u32_ptr,
                                    sizeof(uint32_t),
                                    NULL,
                                    NULL);
    u32_ptr = malloc(sizeof(uint32_t));
    *u32_ptr = group_info->gr_gid;
    simple_archiver_hash_map_insert(users_infos.GnameToGid,
                                    u32_ptr,
                                    strdup(group_info->gr_name),
                                    strlen(group_info->gr_name) + 1,
                                    NULL,
                                    NULL);
    group_info = getgrent();
  }
  endgrent();

  return users_infos;
}
