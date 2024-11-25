# Changelog

## Upcoming Changes

Minor refactorings related to `printf` and `uintX_t`/`size_t` types.

## Version 1.7

Refactor the internal hash-map data structure.

Minor update to CMakeLists.txt.

## Version 1.6

Enforce "safe-links" on extraction by ensuring every extracted symlink actually
points to a file in the archive. Additionally any extracted symlinks that don't
point to a valid destination is removed. This "enforce safe-links on extract"
can be disabled with the "--no-safe-links" option.

Add "--preserve-symlinks" option that will verbatim store the symlinks' target.
Not recommended if symlinks are pointing to absolute paths, which will be
clobbered on extraction to a different directory unless if "--no-safe-links" is
specified on extraction.

## Version 1.5

Previous file-format-v1 implementation of "safe links" still created a symlink
if a relative or absolute link existed in the file. This version fixes this, and
prevents invalid symlinks from being created. (This check is only done if the
bit-flag is set in the file as mentioned in the file-format spec for v1 files.)

## Version 1.4

Do "safe links" behavior by default: symlinks pointing to outside of archived
files (or invalid symlinks) should not be included in the archive, unless if the
option "--no-safe-links" is specified. This is supported in both v0 and v1 file
formats.

## Version 1.3

Prevent `simplearchiver` from busy-waiting during non-blocking IO by sleeping
in "EWOULDBLOCK" conditions. This results in less consumed cpu time by the
process, especially during compression.

## Version 1.2

Proper handling of Ctrl+C (SIGINT). This prevents temporary files from
persisting by doing a proper cleanup before stopping the program.

## Version 1.1

More robust handling of de/compression process (handling SIGPIPE).

By default files are now pre-sorted by size before placed into chunks.  
Add option to NOT pre-sort files by size.

## Version 1.0

First release.

Features:

  - Can specify any command as de/compressor when archiving.
      - The commands must accept file data in stdin and output processed data to
        stdout.
  - Can specify any command as decompressor when extracting to override the
    simple-archive's stored decompressor.
  - Archives/compresses into chunks to reduce overhead by compressing per chunk
    instead of per file.
  - Chunk size can be tweaked by a parameter setting.
  - Can archive without de/compressor to be compressed separately.
  - Supports pre-version-1 simple archiver file format (version 0).
  - Archives regular files and symlinks.
  - Keeps track of files and symlink permissions.
  - Keeps track of file UID and GID (only set if extracting as root).
  - Can be set to ignore absolute paths for symlinks by parameter setting.
