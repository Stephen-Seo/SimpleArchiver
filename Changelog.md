# Changelog

## Upcoming Changes

Add white/black-list flags:
    - `--whitelist-contains <text>`
    - `--whitelist-begins-with <text>`
    - `--whitelist-ends-with <text>`
    - `--blacklist-contains <text>`
    - `--blacklist-begins-with <text>`
    - `--blacklist-ends-with <text>`
    - `--wb-case-insensitive`

These flags should affect what entries are archived, what entries are printed
(with `-t`), and what entries are extracted.

## Version 1.15

Add flag `--force-empty-dir-permissions <3-octal-values>` that force sets the
permissions for all extracted empty directories.

Changes behavior to force set empty dir permissions on archive creation only if
`--force-empty-dir-permissions <...>` is used instead of
`--force-dir-permissions <...>`.

Internal refactorings/fixes.

## Version 1.14

Fix not setting empty directory permissions when extracted.  
The previous behavior just used the default (or forced) permissions.

## Version 1.13

Stricter default directory permissions when directories are created.

  - Previous impl used: `rwxrwxr-x`
  - Current impl uses: `rwxr-xr-x`

## Version 1.12

Fix bug where file formats 2 and 3 would create directories in an archive when
using `-t` mode.

Add new flag `--prefix <prefix>`.

  - When archiving, `--prefix` prepends the given string as a directory to every
    archived item. This effectively stores entries with the given `prefix`.
  - When extracting, `--prefix` prepends the given string as a directory to
    every extracted item. This effectively extracts items with `prefix` as the
    outermost directory in the current working directory.

Add new flag `--version`.

## Version 1.11

Added file format version 3 that supports username and groupname metadata for
all symlinks, files, and (leaf) directory entries in an archive.

Add `--force-user <username>` and `--force-group <groupname>` which acts just
like `--force-uid <UID>` and `--force-gid <GID>` but performs a lookup on the
local system to get the UID/GID.

Fix setting UID/GID for stored directories.

Make extraction prefer username over UID and groupname over GID by default.  
Added `--extract-prefer-uid` and `--extract-prefer-gid` to change this behavior.

Add `--remap-user <UID/Username>:<UID/Username>` and `--remap-group
<GID/Groupname>:<GID/Groupname>` that remaps the first item with the last item
during archival or extraction. Note that if a remap is specified, it always
takes effect when archiving, but when extracting it only takes effect when the
effective-user-id is 0/root.

For the latest file format version (verison 3), `--map-user` and `--map-group`
will have differing behavior if the first value specified is a name or a numeric
id.  If a name is given to map to another value, then it will affect only the
username/groupname for files/links/dirs in the archive. If a numeric id is given
as the first value, then it will affect only the UID/GID fro files/links/dirs in
the archive. For previous file format versions (version 2 and 1), either name or
numeric id will have an effect since these older file format versions only store
UID/GID.

Fix data_structures/priority_heap iter function.

## Version 1.10

Implemented force-setting permissions for files/dirs and force-setting UID/GID.

Better info is printed for directories in file format `version 2` when using
`test mode` `-t`.

Fix case when archiving from read-only directory:

- A temporary file is usually created to store compressed archive
chunks/files which is located where the files are. This version
falls-back to using `tmpfile()` if the first attempt to create a
temporary file fails.

## Version 1.9

Add `file format 2` to handle archiving empty directories.

Default file format is now `version 2`.

Fix edge case where archiving only empty files breaks.  
Currently archiving empty files with a compressor is broken. May not fix since
a compressor is not needed if only empty files are archived.

Add iterator for data structure priority heap.

## Version 1.8

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
