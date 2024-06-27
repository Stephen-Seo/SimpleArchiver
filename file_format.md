# File Format

## Format Version 0

File extension is "*.simplearchive"

First 18 bytes of file will be:

    SIMPLE_ARCHIVE_VER

Next 2 bytes is 16-bit unsigned integer "version" in big-endian. In this case,
will be zero.

Next 4 bytes is 32-bit unsigned integer "file count" in big-endian which will
indicate the number of files in this archive.

For each file:

1. 2 bytes is 16-bit unsigned integer "filename length" in big-endian.
2. X bytes of filename (defined by previous value).
3. 8 bytes 64-bit unsigned integer "location of filename in this archive file".
4. 8 bytes 64-bit unsigned integer "size of filename in this archive file".

The remaining bytes in the file are the files to be included in the archive file
concatenated together. Their locations and sizes should match what was listed
before.
