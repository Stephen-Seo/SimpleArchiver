# Changelog

## Upcoming Changes

More robust handling of de/compression process (handling SIGPIPE).

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
