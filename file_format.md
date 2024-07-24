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
