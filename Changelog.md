# Changelog

## Upcoming Changes

## Alternate Changes

(Alternate Changes are changes that are not in `main` or `dev`.)

Attempt fix https://github.com/Stephen-Seo/SimpleArchiver/issues/6 .

## Version 1.37

Refactored signal handling (e.g. SIGINT/SIGHUP/SIGTERM/SIGPIPE).

## Version 1.36

Handle SIGHUP and SIGTERM as well as SIGINT during compress/extract execution.

## Version 1.35

Make simplearchiver more responsive to early Ctrl-C.

## Version 1.34

When showing un/compressed data sizes, show up to "hundredths" decimal numbers.

Add Github page for man-page converted to HTML. (Link in the README.md)

## Version 1.33

Add "--sort-files-by-name" option. This will sort files by name before
archiving. Note that this option is mutally exclusive with
"--no-pre-sort-files".

Updated README.md and man page for the new option.

## Version 1.32

Fix manpage still noting 4MiB as the default chunk-min-size when it has recently
been changed to 256MiB.

Update README.md to show the default chunk-min-size is 256MiB.

Rework how temporary files are created to avoid a race condition in creating a
new temporary file.

## Version 1.31

Update man page.

Change behavior to only print size stats on success.

Change chunk-min-size from 4MiB to 256MiB.

## Version 1.30

Show archive size stats (compressed size and actual size; only actual size if
compression is not used) when archiving (`-c`).

Fix archive stats when using `-t` on file format version 0 archives.

## Version 1.29

Minor refactorings.

Support "=" for long-arguments that support an option.  
For example, "--prefix out" can now be used as "--prefix=out".

## Version 1.28

Minor tweak to CMake config for easier version string setting when packaging.

Less verbose output when using `-t` and `-x`.

Fix file format 4/5 not extracting symlinks.

Fix checking/extracting links when extracting with `--prefix <dir>`.

Allow suffixes for `--chunk-min-size` like "32MiB".

## Version 1.27

Add actual/compressed file stats (when relevant) when using `-t` and `-x`.  
These stats should show as the last few outputs to stderr.

Also show chunk sizes when using `-t` and `-x` (applicable to file formats that
uses chunks).

Minor fixes to file size outputs (listed as "compressed" size when they are
actually "uncompressed" size.)

Minor changes to CMake file for easier version-setting when packaging.

## Version 1.26

Minor refactorings.

Minor change to allow forcing build with "-g" (for debug packaging).

## Version 1.25

Fix recent refactorings such that using `-t` and positional arguments will
now filter based on the positional arguments.

Minor formatting fixes.

## Version 1.24

A bunch of refactorings.

Added a "clone" and "shallow clone" for data structure "priority heap".

Implemented a "list array" which is like a "chunked array", but stores the
pointers to each sub-array in a linked-list instead of a contiguous array.

Implemented "top" and "bottom" (with "const" variants) for "chunked array" and
"list array".

Created a man page for simplearchiver.

Fixed simplearchiver ignoring positional arguments when extracting.

## Version 1.23

Implement skipping chunks if they are completely ignored due to
black/white-lists.

Fix bug where file format v5 parsing hangs indefintely.

Fix bug where creating archive to stdout while compressing fails to create an
archive.

## Version 1.22

Fix parsing file format 5 where testing/extracting a zero-size chunk errors out.

## Version 1.21

Add new data structure `chunked array`.

Refactor `priority heap` to use `chunked array`.

Add file format version 5 to be able to handle writing chunks with zero size.  
(Fix for archiving files of zero size.)

## Version 1.20

Fix errors for file format 4 archive creation when file/chunk/directory counts
were actually larger than 32-bits.

Fix erronous usage of return value from `fcntl(...)`.

Cleanup of warnings that show when building for 32-bit systems, including
integer conversions between `size_t` and `uint64_t` (`size_t` is 4 bytes on
32-bit systems and 8 bytes on 64-bit systems).

## Version 1.19

Fix bug where writes fail sometimes (due to checking against wrong byte count
(read instead of write)). This can cause creation of archives to fail.

Refactoring of error handling.

## Version 1.18

Add file format version 4 to support more than 4 billion symlinks, and more
than 4 billion files.

Some minor fixes.

Changed behavior of permissions for temp-file (created during compression) to
be more strict. This may not be important to most end-users.

Added `--force-tmpfile`, which acts like `--temp-files-dir <dir>` but forces the
use of `tmpdir()` instead of forcing the temporary file's directory.  
Note that these two flags are mutually exclusive.

## Version 1.17

Fix `--whitelist-begins-with <text>` and `--whitelist-ends-with <text>`.

## Version 1.16

Add white/black-list flags:

  - `--whitelist-contains-any <text>`
  - `--whitelist-contains-all <text>`
  - `--whitelist-begins-with <text>`
  - `--whitelist-ends-with <text>`
  - `--blacklist-contains-any <text>`
  - `--blacklist-contains-all <text>`
  - `--blacklist-begins-with <text>`
  - `--blacklist-ends-with <text>`
  - `--wb-case-insensitive`

These flags should affect what entries are archived, what entries are printed
(with `-t`), and what entries are extracted.

Fixed/refactored how temp files are handled during compression. Note that a
directory can be specified with `--temp-files-dir <dir>`.  
This means that temp files are now, by default, created in the same directory
as the output file instead of the current working directory.

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
