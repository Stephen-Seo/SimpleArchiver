# File Format

Note that any unused bytes/bits should be zeroed-out before being written.

## Format Version 0

File extension is "*.simplearchive"

First 18 bytes of file will be:

    SIMPLE_ARCHIVE_VER

Next 2 bytes is 16-bit unsigned integer "version" in big-endian. In this case,
it will be zero.

Next 4 bytes are bit-flags.

1. The first byte
    1. The first bit is set if de/compressor is set for this archive.

The remaining unused flags are reserved for future revisions and are currently
ignored.

If the previous "de/compressor is set" flag is enabled, then the next section is
added:

1. 2 bytes is 16-bit unsigned integer "compressor cmd+args" in big-endian. This
   does not include the NULL at the end of the string.
2. X bytes of "compressor cmd+args" (length defined by previous value). Is a
   NULL-terminated string.
3. 2 bytes is 16-bit unsigned integer "decompressor cmd+args" in big-endian.
   This does not include the NULL at the end of the string.
4. X bytes of "decompressor cmd+args" (length defined by previous value). Is a
   NULL-terminated string.

The next 4 bytes is 32-bit unsigned integer "file count" in big-endian which
will indicate the number of files in this archive.

Following the file-count bytes, the following bytes are added for each file:

1. 2 bytes is 16-bit unsigned integer "filename length" in big-endian. This does
   not include the NULL at the end of the string.
2. X bytes of filename (length defined by previous value). Is a NULL-terminated
   string.
3. 4 bytes bit-flags
    1. The first byte
        1. The first bit is set if the file is a symbolic link.
        2. The second bit is "user read permission".
        3. The third bit is "user write permission".
        4. The fourth bit is "user execute permission".
        5. The fifth bit is "group read permission".
        6. The sixth bit is "group write permission".
        7. The seventh bit is "group execute permission".
        8. The eighth bit is "other read permission".
    2. The second byte.
        1. The first bit is "other write permission".
        2. The second bit is "other execute permission".
        3. The third bit is UNSET if relative links are preferred, and is SET
           if absolute links are preferred.
        4. The fourth bit is set if this file/symlink-entry is invalid and must
           be skipped. Ignore following bytes after these 4 bytes bit-flags in
           this specification and skip to the next entry; if marked invalid,
           the following specification bytes for this file/symlink entry must
           not exist.
    3. The third byte.
        1. Currently unused.
    4. The fourth byte.
        1. Currently unused.
4. If this file is a symbolic link:
    1. 2 bytes is 16-bit unsigned integer "link target absolute path" in
       big-endian. This does not include the NULL at the end of the string.
    2. X bytes of link-target-absolute-path (length defined by previous value).
       Is a NULL-terminated string. If the previous "size" value is 0, then
       this entry does not exist and should be skipped.
    3. 2 bytes is 16-bit unsigned integer "link target relative path" in
       big-endian. This does not include the NULL at the end of the string.
    4. X bytes of link-target-relative-path (length defined by previous value).
       Is a NULL-terminated string. If the previous "size" value is 0, then
       this entry does not exist and should be skipped.
5. If this file is NOT a symbolic link:
    1. 8 bytes 64-bit unsigned integer "size of filename in this archive file"
       in big-endian.
    2. X bytes file data (length defined by previous value).

## Format Version 1

File extension is "*.simplearchive" but this isn't really checked.

First 18 bytes of file will be (in ascii):

    SIMPLE_ARCHIVE_VER

Next 2 bytes is a 16-bit unsigned integer "version" in big-endian. It will be:

    0x00 0x01

Next 4 bytes are bit-flags.

1. The first byte
    1. The first bit is set if de/compressor is set for this archive.

The remaining unused flags in the previous bit-flags bytes are reserved for
future revisions and are currently ignored.

If the previous "de/compressor is set" flag is enabled, then the next section is
added:

1. 2 bytes is 16-bit unsigned integer "compressor cmd+args" in big-endian. This
   does not include the NULL at the end of the string.
2. X bytes of "compressor cmd+args" (length defined by previous value). Is a
   NULL-terminated string.
3. 2 bytes is 16-bit unsigned integer "decompressor cmd+args" in big-endian.
   This does not include the NULL at the end of the string.
4. X bytes of "decompressor cmd+args" (length defined by previous value). Is a
   NULL-terminated string.

The next 4 bytes is a 32-bit unsigned integer "link count" in big-endian which
will indicate the number of symbolic links in this archive.

Following the link-count bytes, the following bytes are added for each symlink:

1. 2 bytes bit-flags:
    1. The first byte.
        1. The first bit is UNSET if relative links are preferred, and is SET if
           absolute links are preferred.
        2. The second bit is "user read permission".
        3. The third bit is "user write permission".
        4. The fourth bit is "user execute permission".
        5. The fifth bit is "group read permission".
        6. The sixth bit is "group write permission".
        7. The seventh bit is "group execute permission".
        8. The eighth bit is "other read permission".
    2. The second byte.
        1. The first bit is "other write permission".
        2. The second bit is "other execute permission".
        3. If this bit is set, then this entry is marked invalid. The link name
           will be preserved in this entry, but the following link target paths
           will be set to zero-length and will not be stored.
2. 2 bytes 16-bit unsigned integer "link name" in big-endian. This does not
   include the NULL at the end of the string. Must not be zero.
3. X bytes of link-name (length defined by previous value). Is a NULL-terminated
   string.
4. 2 bytes is 16-bit unsigned integer "link target absolute path" in
   big-endian. This does not include the NULL at the end of the string.
5. X bytes of link-target-absolute-path (length defined by previous value).
   Is a NULL-terminated string. If the previous "size" value is 0, then
   this entry does not exist and should be skipped.
6. 2 bytes is 16-bit unsigned integer "link target relative path" in
   big-endian. This does not include the NULL at the end of the string.
7. X bytes of link-target-relative-path (length defined by previous value).
   Is a NULL-terminated string. If the previous "size" value is 0, then
   this entry does not exist and should be skipped.

After the symlink related data, the next 4 bytes is a 32-bit unsigned integer
"chunk count" in big-endian which will indicate the number of chunks in this
archive.

Following the chunk-count bytes, the following bytes are added for each chunk:

1. 4 bytes that are a 32-bit unsigned integer "file count" in big-endian.

The following bytes are added for each file within the current chunk:

1. 2 bytes that are a 16-bit unsigned integer "filename length" in big-endian.
   This does not include the NULL at the end of the string.
2. X bytes of filename (length defined by previous value). Is a NULL-terminated
   string.
3. 4 bytes bit-flags.
    1. The first byte.
        1. The first bit is "user read permission".
        2. The second bit is "user write permission".
        3. The third bit is "user execute permission".
        4. The fourth bit is "group read permission".
        5. The fifth bit is "group write permission".
        6. The sixth bit is "group execute permission".
        7. The seventh bit is "other read permission".
        8. The eighth bit is "other write permission".
    2. The second byte.
        1. The first bit is "other execute permission".
    3. The third byte.
        1. Currently unused.
    4. The fourth byte.
        1. Currently unused.
4. Two 4-byte unsigned integers in big-endian for UID and GID.
    1. A 32-bit unsigned integer in big endian that specifies the UID of the
       file. Note that during extraction, if the user is not root, then this
       value will be ignored.
    2. A 32-bit unsigned integer in big endian that specifies the GID of the
       file. Note that during extraction, if the user is not root, then this
       value will be ignored.
5. A 64-bit unsigned integer in big endian for the "size of file".

After the files' metadata are the current chunk's data:

1. A 64-bit unsigned integer in big endian for the "size of chunk".
2. X bytes of data for the current chunk of the previously specified size. If
   not using de/compressor, this section is the previously mentioned files
   concatenated with each other. If using de/compressor, this section is the
   previously mentioned files concatenated and compressed into a single blob of
   data.

## Format Version 2

This format is nearly identical to `Format Version 1` as it appends data
required to preserve empty directories. In other words, the previous format did
not have support for archiving empty directories and this version adds support
for that.

First 18 bytes of the file will be (in ascii):

    SIMPLE_ARCHIVE_VER

Next 2 bytes is a 16-bit unsigned integer "version" in big-endian. It will be:

    0x00 0x02

The following bytes are exactly the same as `Format Version 1` and will not be
repeated (please refer to the previous specification), but new data is appended
as mentioned earlier.

After the similar `Format Version 1` data:

4 bytes (a 32-bit unsigned integer) of "directory count" in big-endian.

After this, for each directory of count "directory count":

1. 2 bytes (16-bit unsigned integer) "directory name length" in big-endian. This
   does not include the NULL at the end of the string.
2. X bytes of "directory name" with length of "directory name length". Note that
   this string is stored as a NULL-terminated string.
3. 2 bytes permissions flags:
    1. The first byte's bits:
        1. User read permission
        2. User write permission
        3. User execute permission
        4. Group read permission
        5. Group write permission
        6. Group execute permission
        7. Other read permission
        8. Other write permission
    2. The second byte's bits (unused bits are assumed to be set to 0):
        1. Other execute permission
4. Two 4-byte unsigned integers in big-endian for UID and GID.
    1. A 32-bit unsigned integer in big-endian that is the UID of the directory.
    2. A 32-bit unsigned integer in big-endian that is the GID of the directory.

Note that it is possible for directory entries to consist of only the "tail end"
directory and not specify "intermediate" directories. For example, if there is a
directory structure such as "/a/b/c/", then it is possible for there to be only
an entry "/a/b/c/" and the directories "/a/b/" and "/a/" may need to be created
even though they are not specified.

## Format Version 3

This format builds upon `Format Version 2` by adding fields to store
username/groupname for symlink/file entries.

File extension is "*.simplearchive" but this isn't really checked.

First 18 bytes of file will be (in ascii):

    SIMPLE_ARCHIVE_VER

Next 2 bytes is a 16-bit unsigned integer "version" in big-endian. It will be:

    0x00 0x03

Next 4 bytes are bit-flags.

1. The first byte
    1. The first bit is set if de/compressor is set for this archive.

The remaining unused flags in the previous bit-flags bytes are reserved for
future revisions and are currently ignored.

If the previous "de/compressor is set" flag is enabled, then the next section is
added:

1. 2 bytes is 16-bit unsigned integer "compressor cmd+args" in big-endian. This
   does not include the NULL at the end of the string.
2. X bytes of "compressor cmd+args" (length defined by previous value). Is a
   NULL-terminated string.
3. 2 bytes is 16-bit unsigned integer "decompressor cmd+args" in big-endian.
   This does not include the NULL at the end of the string.
4. X bytes of "decompressor cmd+args" (length defined by previous value). Is a
   NULL-terminated string.

The next 4 bytes is a 32-bit unsigned integer "link count" in big-endian which
will indicate the number of symbolic links in this archive.

Following the link-count bytes, the following bytes are added for each symlink:

1. 2 bytes bit-flags:
    1. The first byte.
        1. The first bit is UNSET if relative links are preferred, and is SET if
           absolute links are preferred.
        2. The second bit is "user read permission".
        3. The third bit is "user write permission".
        4. The fourth bit is "user execute permission".
        5. The fifth bit is "group read permission".
        6. The sixth bit is "group write permission".
        7. The seventh bit is "group execute permission".
        8. The eighth bit is "other read permission".
    2. The second byte.
        1. The first bit is "other write permission".
        2. The second bit is "other execute permission".
        3. If this bit is set, then this entry is marked invalid. The link name
           will be preserved in this entry, but the following link target paths
           will be set to zero-length and will not be stored.
2. 2 bytes 16-bit unsigned integer "link name" in big-endian. This does not
   include the NULL at the end of the string. Must not be zero.
3. X bytes of link-name (length defined by previous value). Is a NULL-terminated
   string.
4. 2 bytes is 16-bit unsigned integer "link target absolute path" in
   big-endian. This does not include the NULL at the end of the string.
5. X bytes of link-target-absolute-path (length defined by previous value).
   Is a NULL-terminated string. If the previous "size" value is 0, then
   this entry does not exist and should be skipped.
6. 2 bytes is 16-bit unsigned integer "link target relative path" in
   big-endian. This does not include the NULL at the end of the string.
7. X bytes of link-target-relative-path (length defined by previous value).
   Is a NULL-terminated string. If the previous "size" value is 0, then
   this entry does not exist and should be skipped.
8. 4 bytes is a 32-bit unsigned integer in big-endian storing UID for this
   symlink.
9. 4 bytes is a 32-bit unsigned integer in big-endian storing GID for this
   symlink.
10. 2 bytes 16-bit unsigned integer "user name" length in big-endian. This does
    not include the NULL at the end of the string.
11. X bytes of user-name (length defined by previous value). Is a
    NULL-terminated string. If the previous "size" value is 0, then this entry
    does not exist and should be skipped.
10. 2 bytes 16-bit unsigned integer "group name" length in big-endian. This does
    not include the NULL at the end of the string.
11. X bytes of group-name (length defined by previous value). Is a
    NULL-terminated string. If the previous "size" value is 0, then this entry
    does not exist and should be skipped.

After the symlink related data, the next 4 bytes is a 32-bit unsigned integer
"chunk count" in big-endian which will indicate the number of chunks in this
archive.

Following the chunk-count bytes, the following bytes are added for each chunk:

1. 4 bytes that are a 32-bit unsigned integer "file count" in big-endian.

The following bytes are added for each file within the current chunk:

1. 2 bytes that are a 16-bit unsigned integer "filename length" in big-endian.
   This does not include the NULL at the end of the string.
2. X bytes of filename (length defined by previous value). Is a NULL-terminated
   string.
3. 4 bytes bit-flags.
    1. The first byte.
        1. The first bit is "user read permission".
        2. The second bit is "user write permission".
        3. The third bit is "user execute permission".
        4. The fourth bit is "group read permission".
        5. The fifth bit is "group write permission".
        6. The sixth bit is "group execute permission".
        7. The seventh bit is "other read permission".
        8. The eighth bit is "other write permission".
    2. The second byte.
        1. The first bit is "other execute permission".
    3. The third byte.
        1. Currently unused.
    4. The fourth byte.
        1. Currently unused.
4. Two 4-byte unsigned integers in big-endian for UID and GID.
    1. A 32-bit unsigned integer in big endian that specifies the UID of the
       file. Note that during extraction, if the user is not root, then this
       value will be ignored.
    2. A 32-bit unsigned integer in big endian that specifies the GID of the
       file. Note that during extraction, if the user is not root, then this
       value will be ignored.
5. 2 bytes 16-bit unsigned integer "user name" length in big-endian. This does
   not include the NULL at the end of the string.
6. X bytes of user-name (length defined by previous value). Is a
   NULL-terminated string. If the previous "size" value is 0, then this entry
   does not exist and should be skipped.
7. 2 bytes 16-bit unsigned integer "group name" length in big-endian. This does
   not include the NULL at the end of the string.
8. X bytes of group-name (length defined by previous value). Is a
   NULL-terminated string. If the previous "size" value is 0, then this entry
   does not exist and should be skipped.
9. A 64-bit unsigned integer in big endian for the "size of file".

After the files' metadata are the current chunk's data:

1. A 64-bit unsigned integer in big endian for the "size of chunk".
2. X bytes of data for the current chunk of the previously specified size. If
   not using de/compressor, this section is the previously mentioned files
   concatenated with each other. If using de/compressor, this section is the
   previously mentioned files concatenated and compressed into a single blob of
   data.

After all the chunks:
4 bytes (a 32-bit unsigned integer) of "directory count" in big-endian.

After this, for each directory of count "directory count":

1. 2 bytes (16-bit unsigned integer) "directory name length" in big-endian. This
   does not include the NULL at the end of the string.
2. X bytes of "directory name" with length of "directory name length". Note that
   this string is stored as a NULL-terminated string.
3. 2 bytes permissions flags:
    1. The first byte's bits:
        1. User read permission
        2. User write permission
        3. User execute permission
        4. Group read permission
        5. Group write permission
        6. Group execute permission
        7. Other read permission
        8. Other write permission
    2. The second byte's bits (unused bits are assumed to be set to 0):
        1. Other execute permission
4. Two 4-byte unsigned integers in big-endian for UID and GID.
    1. A 32-bit unsigned integer in big-endian that is the UID of the directory.
    2. A 32-bit unsigned integer in big-endian that is the GID of the directory.
5. 2 bytes 16-bit unsigned integer "user name" length in big-endian. This does
   not include the NULL at the end of the string.
6. X bytes of user-name (length defined by previous value). Is a
   NULL-terminated string. If the previous "size" value is 0, then this entry
   does not exist and should be skipped.
7. 2 bytes 16-bit unsigned integer "group name" length in big-endian. This does
   not include the NULL at the end of the string.
8. X bytes of group-name (length defined by previous value). Is a
   NULL-terminated string. If the previous "size" value is 0, then this entry
   does not exist and should be skipped.

Note that it is possible for directory entries to consist of only the "tail end"
directory and not specify "intermediate" directories. For example, if there is a
directory structure such as "/a/b/c/", then it is possible for there to be only
an entry "/a/b/c/" and the directories "/a/b/" and "/a/" may need to be created
even though they are not specified.
