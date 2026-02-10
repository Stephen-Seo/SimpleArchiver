# Changelog

## Upcoming Changes

Add `--force-prefix-dir-permissions`, which sets the permissions of dirs created
with `--prefix`.

Add `--set-prefix-dir-uid`, `--set-prefix-dir-user`, `--set-prefix-dir-gid`,
and `--set-prefix-dir-group`.

Some refactoring of error logs/prints when arg parsing.

Unix platforms now link against `libcap` to check for privileges to check if
`chown` (changing ownership) can be used during extraction of files/dirs.  
Useful for options like `--force-uid=<UID>` or `--set-prefix-dir-gid=<GID>`.

## Alternate Changes

(Alternate Changes are changes that are not in `main` or `dev`.)

Attempt fix https://github.com/Stephen-Seo/SimpleArchiver/issues/6 .

## Version 2.5

Fix bug where default file sorting method (sort by size) was broken for the
current file format (default file format version 6). (Note that if a
"first-chunk-with-no-compression" exists, then that first-chunk is always
sorted by name. All other chunks are sorted based on settings (file-size by
default).)

## Version 2.4

Fix bug where extracting a directory in fv6 is read-only and simplearchiver
fails to extract a file to the inside of the read-only directory by setting
permissions after files have been extracted. (The directory is initially set to
700 (or u+rwx and not for group or other), then set to the necessary permissions
later.)

Add option `--v6-remove-leaf-dirs`.

## Version 2.3

Update man page with new option `--v6-remove-empty-dirs`.

Fix erronous file size stats printing when using file format 6 and creating an
archive with compression when there are no files matching extensions set by
`--use-file-exts-preset` or `--add-file-ext <ext>`.

## Version 2.2

Fix extracting dirs in file format v6 by extracting them all regardless of
white/blacklists or positional args, and using opt-in "--v6-remove-empty-dirs"
to remove them on extraction. A reserved bit in file format 6 is used to keep
track of dirs already empty on archive creation so that on extract, these dirs
are not removed. Prior to this change, all directories are treated as "empty" so
that they will not be removed when using "--v6-remove-empty-dirs" for backwards
compatability.

To clarify:  
"--v6-remove-empty-dirs" removes dirs that are empty after extraction but does
not remove dirs that were empty during archive creation. All archives created
before the existence of this option are treated as "empty during archive
creation".

Fix v5/v6 archive extraction skipping chunks causing parsing offset mismatch. In
other words, this should fix skipping chunks via white/blacklists for v5 and v6.

Note that there was suspicion (by me, Stephen) that file format v5 may have
broken things, but after checking the code, the addition of the 2 byte prefix
"SA" to all chunks was implemented as it should. The only breakage was found in
the code handling "skipped chunks". A chunk (during extraction or checking) is
skipped only if white/blacklists are used, and that it was determined that a
chunk contained only "skipped" items due to the white/blacklist. Existing code
since the introduction to file format version 5 appears to have had this bug
where "skipped chunks" did not account for these two bytes. This has been
notably fixed as of version 2.2 of simplearchiver. (The implementation of
skipped chunks was to simply read all of the chunk from the archive without
saving it elsewhere to progress the "position" of the read into the archive;
simplearchiver is designed to be streamable (no seeking allowed).
Interestingly, uncompressing compressed chunks did not have this problem as
they already accounted for the 2 bytes, but extracting non-compressed chunks
had this problem.)

## Version 2.1

Add some validation to positional arguments.

In other words, ensure paths to archive are in a valid format. This means that
absolute paths to files are not allowed. Use `-C <dir>`, `.` instead.

## Version 2.0

Added new File Format Version 6.

This new file format supports conditionally compressed chunks when using
compression. By default, all chunks are compressed.

One can use `--use-file-exts-preset` to enable a preset of known file extensions
of files to not compress. One can also use `--add-file-ext` to add a file
extension to not compress when using v6. These options cause all files that
match the extensions to be placed in the first chunk, which will be set to not
be compressed. All other chunks will be compressed with the expected files as
normal.

All directories in the archive path will be saved (instead of just "leaf"
directories as in the previous file format versions). Their permissions and
ownership will be stored, but as usual, only root (UID 0) can set ownership when
extracting.

## Version 1.38

Fix bug in writing uncompressed file format v5 chunks where "SA" was prepended
to the chunk size instead of the chunk data. This broke chunk size stats, but
extraction still worked because extraction was based on individual file sizes
instead of the chunk size.

Upon examination of this bug, there is NO RISK OF LOSS OF DATA, because
individual file-sizes were used when extracting each file from an uncompressed
archive, and only the CHUNK SIZE was corrupted, which wasn't used for the
per-file extraction process.

In other words, this bug only caused garbled per-chunk-size-output when using
`-x` or `-t`.

## Version 1.37.1

Fix missing include line in `archive.c`.

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
