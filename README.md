# Simple Archiver

This program ~~is not yet~~ ~~almost~~ basically finished! ~~Basic~~ Necessary
functionality is implemented and only ~~some advanced features are missing~~
some extra features are not yet implemented.

This program exists because I could not get `tar` or `ar` to compile with
[Cosmopolitan](https://justine.lol/cosmopolitan/index.html). Thus, this
archiver will be written with support for Cosmopolitan in mind. This means
sticking to the C programming language and possibly using Cosmopolitan-specfic
API calls.

## Usage

    Usage flags:
    -c : create archive file
    -x : extract archive file
    -t : examine archive file
    -f <filename> : filename to work on
      Use "-f -" to work on stdout when creating archive or stdin when reading archive
      NOTICE: "-f" is not affected by "-C"!
    -C <dir> : Change current working directory before archiving/extracting
    --compressor <full_compress_cmd> : requires --decompressor and cmd must use stdin/stdout
    --decompressor <full_decompress_cmd> : requires --compressor and cmd must use stdin/stdout
      Specifying "--decompressor" when extracting overrides archive file's stored decompressor cmd
    --overwrite-create : allows overwriting an archive file
    --overwrite-extract : allows overwriting when extracting
    --no-abs-symlink : do not store absolute paths for symlinks
    --preserve-symlinks : preserve the symlink's path on archive creation instead of deriving abs/relative paths, ignores "--no-abs-symlink" (It is not recommended to use this option, as absolute-path-symlinks may be clobbered on extraction)
    --no-safe-links : keep symlinks that link to outside archive contents
    --temp-files-dir <dir> : where to store temporary files created when compressing (defaults to current working directory)
    --write-version <version> : Force write version file format (default 1)
    --chunk-min-size <bytes> : v1 file format minimum chunk size (default 4194304 or 4MiB)
    --no-pre-sort-files : do NOT pre-sort files by size (by default enabled so that the first file is the largest)
    --no-preserve-empty-dirs : do NOT preserve empty dirs (only for file format 2 and onwards)
    --force-uid <uid> : Force set UID on archive creation/extraction
      On archive creation, sets UID for all files/dirs in the archive.
      On archive extraction, sets UID for all files/dirs only if EUID is 0.
    --force-gid <gid> : Force set GID on archive creation/extraction
      On archive creation, sets GID for all files/dirs in the archive.
      On archive extraction, sets GID for all files/dirs only if EUID is 0.
    --force-file-permissions <3-octal-values> : Force set permissions for files on archive creation/extraction
      Must be three octal characters like "755" or "440"
    --force-dir-permissions <3-octal-values> : Force set permissions for directories on archive creation/extraction
      Must be three octal characters like "755" or "440"
    -- : specifies remaining arguments are files to archive/extract
    If creating archive file, remaining args specify files to archive.
    If extracting archive file, remaining args specify files to extract.

Note that `--compressor` and `--decompressor` cmds must accept data from stdin
and return processed data to stdout.

## Using the Cosmopolitan-Compiled Version

Note that on Linux, the `actually_portable_simplearchiver` binaries may attempt
to open via Wine (if Wine is installed). [A workaround is mentioned here.](https://github.com/jart/cosmopolitan/blob/master/README.md#linux)

## Changes

See the [Changelog](https://github.com/Stephen-Seo/SimpleArchiver/blob/main/Changelog.md).

## Other Things to Know

When compressing, it may be useful to set `--temp-files-dir <dir>` as
`simplearchiver` will create a temporary file (a chunk) usually in the current
working directory or in the directory specified by `-C <dir>` by default. In
case the temporary file cannot be created in the default directory,
[`tmpfile()`](https://man7.org/linux/man-pages/man3/tmpfile.3.html) is used
instead as a fallback. Thus, `--temp-files-dir <dir>` changes the default dir to
store the temporary compressed chunk.

When storing symlinks, `simplearchiver` will typically store relative and
absolute-paths for all symlinks. If a symlink points to something that will be
stored in the archive during archive creation, then relative paths will be
preferred for that symlink on extraction. If `--no-safe-links` is set when
creating the archive, then `simplearchiver` will prefer absolute paths on
extraction for symlinks that point to anything that wasn't stored in the
archive. `--no-abs-symlink` will force `simplearchiver` to store only relative
symlinks and not absolute-path symlinks on archive creation.

UID and GID will only be set on extracted files if the EUID is 0. Thus, files
extracted by non-EUID-0 users will typically have the extracted files UID/GID as
the extracting user's UID/GID.

## LICENSE Information

Uses the [ISC License](https://choosealicense.com/licenses/isc/).
