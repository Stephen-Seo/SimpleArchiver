# Simple Archiver

## NOTICE

[The 2026-10-20 issue write-up in this repo.](https://github.com/Stephen-Seo/SimpleArchiver/blob/main/SECURITY_NOTICES.md#2016-10-20-the-decompressor-command)  
[Writeup about the 2026-10-20 issue here.](https://seodisparate.com/blog/2026-10_SA_security_issue)  
[Backup gist about the 2026-10-20 issue here.](https://gist.github.com/Stephen-Seo/606a2876112e5d37979da2f856910d07)

Please refer to the [Changelog.md](https://github.com/Stephen-Seo/SimpleArchiver/blob/main/Changelog.md)
for recent important changes.

[The 2026-03-18 issue write-up in this repo.](https://github.com/Stephen-Seo/SimpleArchiver/blob/main/SECURITY_NOTICES.md#2016-03-18-path-validation)  
[Writeup about the 2026-03-18 issue here.](https://seodisparate.com/blog/simplearchiver_issue_disclosure/)  
[Backup gist about the 2026-03-18 issue here.](https://gist.github.com/Stephen-Seo/f804dd6d65f409b93243dd00d4f67452)

Please use the latest version of `simplearchiver`
(at least tag `4.0.0` or at least the [latest Github
release](https://github.com/Stephen-Seo/SimpleArchiver/releases)) as
previous versions may be vulnerable with fixes added as of `4.0.0`.

## About SimpleArchiver

This program ~~is not yet~~ ~~almost~~ basically finished! ~~Basic~~ ~~Necessary
functionality is implemented and only~~ ~~some advanced features are missing~~
~~some extra features are not yet implemented.~~  
This program has slowly but surely built up a significant amount of features
over time, which may have, perhaps, outgrown the "Simple" part of its name.

This program exists because I could not get `tar` or `ar` to compile with
[Cosmopolitan](https://justine.lol/cosmopolitan/index.html). Thus, this
archiver will be written with support for Cosmopolitan in mind. This means
sticking to the C programming language and possibly using Cosmopolitan-specfic
API calls.

[AUR package](https://aur.archlinux.org/packages/simplearchiver)

[Man Page Converted To HTML](https://stephen-seo.github.io/SimpleArchiver/)

## Usage

    Usage flags:
    -c : create archive file
    -x : extract archive file
    -t : examine archive file
    -f <filename> : filename to work on
      Use "-f -" to work on stdout when creating archive or stdin when reading archive
      NOTICE: "-f" is not affected by "-C"!
    -C <dir> : Change current working directory before archiving/extracting
    --prefix <prefix> | --prefix=<prefix> : set prefix for archived/extracted paths ("/" will be appended to the end if missing)
    --compressor <full_compress_cmd> | --compressor=<cmd> : requires --decompressor and cmd must use stdin/stdout
    --decompressor <full_decompress_cmd> | --decompressor=<cmd> : requires --compressor and cmd must use stdin/stdout
      Specifying "--decompressor" when extracting overrides archive file's stored decompressor cmd
    --overwrite-create : allows overwriting an archive file
    --overwrite-extract : allows overwriting when extracting
    --no-abs-symlink : do not store absolute paths for symlinks
    --preserve-symlinks : preserve the symlink's path on archive creation instead of deriving abs/relative paths, ignores "--no-abs-symlink" (It is not recommended to use this option, as absolute-path-symlinks may be clobbered on extraction)
    --no-safe-links : keep symlinks that link to outside archive contents
    --temp-files-dir <dir> | --temp-files-dir=<dir> : where to store temporary files created when compressing (defaults to same directory as output file) (this is mutually exclusive with "--force-tmpfile")
    --force-tmpfile : Force the use of "tmpfile()" during compression (this is mutually exclusive with "--temp-files-dir")
    --write-version <version> | --write-version=<version> : Force write version file format (default 7)
    --chunk-min-size <bytes> | --chunk-min-size=<bytes> : minimum chunk size (default 268435456 or 256MiB) when using chunks (file formats v. 1 and up)
      Note suffixes "KB, KiB, MB, MiB, GB, and GiB" are supported
      Use like "32MiB" without spaces.
    --no-pre-sort-files : do NOT pre-sort files by size (by default enabled so that the first file is the largest; mutually exclusive with "--sort-files-by-name")
    --sort-files-by-name : pre-sort files by name (mutually exclusive with "--no-pre-sort-files").
    --no-preserve-empty-dirs : do NOT preserve empty dirs (only for file format 2 and onwards)
    --force-uid <uid> | --force-uid=<uid> : Force set UID on archive creation/extraction
      On archive creation, sets UID for all files/dirs in the archive.
      On archive extraction, sets UID for all files/dirs only if EUID is 0.
    --force-user <username> | --force-user=<username> : Force set UID (same as --force-uid but fetched from username)
    --force-gid <gid> | --force-gid=<gid> : Force set GID on archive creation/extraction
    --force-group <groupname> | --force-group=<group> : Force set GID (same as --force-gid but fetched from groupname)
      On archive creation, sets GID for all files/dirs in the archive.
      On archive extraction, sets GID for all files/dirs only if EUID is 0.
    --extract-prefer-uid : Prefer UID over Username when extracting
      Note that by default Username is preferred over UID
    --extract-prefer-gid : Prefer GID over Group when extracting
      Note that by default Group is preferred over GID
    --map-user <UID/Uname>:<UID/Uname> | --map-user=<UID/Uname>:<UID/Uname> : Maps a UID/Username to UID/Username
    --map-group <GID/Gname>:<GID/Gname> | --map-group=<GID/Gname>:<GID/Gname> : Maps a GID/Group to GID/Group
    --force-file-permissions <3/4-octal-values> | --force-file-permissions=<3/4-octal-values> : Force set permissions for files on archive creation/extraction
      Must be three/four octal characters like "755" or "440" or "4755"
    --force-dir-permissions <3/4-octal-values> | --force-dir-permissions=<3/4-octal-values> : Force set permissions for directories on archive creation/extraction
      Must be three/four octal characters like "755" or "440" or "4755"
    --force-empty-dir-permissions <3/4-octal-values> | --force-empty-dir-permissions=<3/4-octal-vlaues> : Force set EMPTY dir permissions. Like "--force-dir-permissions", but for empty directories.
    --force-prefix-dir-permissions <3/4-octal-values> | --force-prefix-dir-permissions=<3/4-octal-values> : Force set permissions for "--prefix=<dir>" dir(s) after extraction
    --set-prefix-dir-uid <uid> | --set-prefix-dir-uid=<uid> : Set uid onprefix dir(s) if current user is root
    --set-prefix-dir-user <user> | --set-prefix-dir-user=<user> : Set user uid on prefix dir(s) if current user is root
    --set-prefix-dir-gid <gid> | --set-prefix-dir-gid=<gid> : Set gid onprefix dir(s) if current user is root
    --set-prefix-dir-group <group> | --set-prefix-dir-group=<group> : Set group gid on prefix dir(s) if current user is root
    --whitelist-exact <text> | --whitelist-exact=<text> : Whitelist entries that match "<text>", specify multiple times to allow entries that match any of the specified "<text>"s.
    --whitelist-contains-any <text> | --whitelist-contains-any=<text> : Whitelist entries to contain "<text>", specify multiple times to allow entries that contain any of the specified "<text>"s.
    --whitelist-contains-all <text> | --whitelist-contains-all=<text> : Whitelist entries to contain "<text>", specify multiple times to allow entries that contain all of the specified "<text>"s.
    --whitelist-begins-with <text> | --whitelist-begins-with=<text> : Whitelist entries to start with "<text>", specify multiple times to allow different entries to start with different "<text>" entries.
    --whitelist-ends-with <text> | --whitelist-ends-with=<text> : Whitelist entries to end with "<text>", specify multiple times to allow different entries to end with different "<text>" entries.
    --blacklist-exact <text> | --blacklist-exact=<text> : Blacklist entries that match "<text>", specify multiple times to deny entries that match any of the specified "<text>"s.
    --blacklist-contains-any <text> | --blacklist-contains-any=<text> : blacklist entries that contains "<text>", specify multiple times to deny entries that contain any of the specified "<text>"s.
    --blacklist-contains-all <text> | --blacklist-contains-all=<text> : blacklist entries that contains "<text>", specify multiple times to deny entries that contain all of the specified "<text>"s.
    --blacklist-begins-with <text> | --blacklist-begins-with=<text> : blacklist entries that starts with "<text>", specify multiple times to deny multiple entries starting with different "<text>" entries.
    --blacklist-ends-with <text> | --blacklist-ends-with=<text> : blacklist entries that ends with "<text>", specify multiple times to deny multiple entries ending with different "<text>" entries.
    --wb-case-insensitive : Makes white/black-list checking case insensitive.
    --archive-exclude-dir <dir> | --archive-exclude-dir=<dir> : exclude listed dir when archiving, can be used multiple times.
    --print-file-exts-preset : Prints the preset extensions to stderr and stops simplearchiver.
    --use-file-exts-preset : Adds preset extensions to collection of file extensions to choose to not compress
    --add-file-ext <ext> | --add-file-ext=<ext> : Add a extension to choose to not compress (must be like ".thing")
    --v6-remove-empty-dirs : Remove dirs that are empty after extraction (but were not empty when archived)
    --v6-remove-leaf-dirs : Also remove leaf dirs even if they normally would be kept
    --test-max-depth <depth> | --test-max-depth=<depth> : When testing (-t) an archive, do not print files/symlinks that have more forward-slashes than this specified amount
    --non-interactive : Run in non-interactive mode (note possibly dangerous!)
    --version : prints version and exits
    -- : specifies remaining arguments are files to archive/extract
    If creating archive file, remaining args specify files to archive.
    If extracting archive file, remaining args specify files to extract.
    Note that permissions/ownership/remapping is saved when archiving, but when extracting they are only preserved when extracting as root!

Note that `--compressor` and `--decompressor` cmds must accept data from stdin
and return processed data to stdout.

## Using the Cosmopolitan-Compiled Version

Note that on Linux, the `actually_portable_simplearchiver` binaries may attempt
to open via Wine (if Wine is installed). [A workaround is mentioned here.](https://github.com/jart/cosmopolitan/blob/master/README.md#linux)

## Changes

See the [Changelog](https://github.com/Stephen-Seo/SimpleArchiver/blob/main/Changelog.md).

## `dev` branch

The `dev` branch sees active development, and is only merged into `main` when
considered a new release/tag and/or stable.

## Other Things to Know

When compressing, it may be useful to set `--temp-files-dir <dir>` as
`simplearchiver` will create a temporary file (a chunk) usually in the current
working directory or in the directory specified by `-C <dir>` by default. Note
that in [Version 1.16, the default directory for temp files has been changed to
the directory holding the output archive
file](https://github.com/Stephen-Seo/SimpleArchiver/blob/dev/Changelog.md#version-116).
In case the temporary file cannot be created in the default directory,
[`tmpfile()`](https://man7.org/linux/man-pages/man3/tmpfile.3.html) is used
instead as a fallback. Thus, `--temp-files-dir <dir>` changes the default dir
to store the temporary compressed chunk. Note that `--force-tmpfile` exists to
force simplearchiver to use `tmpfile()`.

Note that file format version 7 does NOT use a temporary file. File formats 6
and lower USE a temporary file.

Note that temporary files are only created when using compression. This is due
to the need to support "streaming". Or in other words, `simplearchiver` can
create an archive and output it to standard-output (with `-f -`), or
test/extract an archive given as standard-input (again, with `-f -`). The file
formats require storing the chunk size before the chunk data itself, but the
chunk size cannot be known for compressed chunks until the chunk has been
actually compressed. This dependency on creating the compressed chunk first is
the reason why temporary files are created.

When storing symlinks, `simplearchiver` will typically store relative and
absolute-paths for all symlinks. If a symlink points to something that will be
stored in the archive during archive creation, then relative paths will be
preferred for that symlink on extraction. If `--no-safe-links` is set when
creating the archive, then `simplearchiver` will prefer absolute paths on
extraction for symlinks that point to anything that wasn't stored in the
archive. `--no-abs-symlink` will force `simplearchiver` to store only relative
symlinks and not absolute-path symlinks on archive creation.

### UID/GID/Username/Group Handling

UID and GID will only be set on extracted files if the EUID is 0. Thus, files
extracted by non-EUID-0 users will typically have the extracted files UID/GID as
the extracting user's UID/GID.

For the latest file format version (verison 3), `--map-user` and `--map-group`
will have differing behavior if the first value specified is a name or a numeric
id.  If a name is given to map to another value, then it will affect only the
username/groupname for files/links/dirs in the archive. If a numeric id is given
as the first value, then it will affect only the UID/GID fro files/links/dirs in
the archive. For previous file format versions (version 2 and 1), either name or
numeric id will have an effect since these older file format versions only store
UID/GID.

### Checking for `chown` Availability

Binaries built with `libcap` support can also check if `CAP_CHOWN` is enabled
for the current user to check if `chown` can be used to set ownership of
extracted files/dirs.  
Note that the check-if-can-chown works like this:

  - check if user is root
    - if is root, then can use `chown`
  - check if has `CAP_CHOWN` (only if libcap support is enabled)
    - If has capability, then can use `chown`
  - cannot use `chown`, dont use it

### Example Archiving a Directory

If you want to archive `foo/` in `/home/bar/some_dir`, then you can use the
command:

```
simplearchiver -c -f /tmp/my_archive.sa -C /home/bar/some_dir foo
```

If you want to archive `/home/bar/some_dir/foo/` without the `foo/`:

```
simplearchiver -c -f /tmp/my_archive.sa -C /home/bar/some_dir/foo .
```

If you want to archive the current directory:

```
simplearchiver -c -f /tmp/my_archive.sa .
```

With compression:

```
simplearchiver -c -f /tmp/my_archive.sa --compressor gzip --decompressor 'gzip -d' .
```

### Expected use case (with decompression)

The expected use-case behavior should be to run `simplearchiver` as usual
without the `--non-interactive` flag, and should the error message (noting
`--non-interactive`) be emitted, the user should check the decompressor command
to ensure it is valid/safe, and then the user should run the command again with
the `--non-interactive` flag set. If the command is deemed to be unsafe, the
user has the option to force the decompressor command used with
`--decompressor`.

## Additional Info

Check the man page (or
[the html generated page](https://stephen-seo.github.io/SimpleArchiver/)).

## LICENSE Information

Uses the [ISC License](https://choosealicense.com/licenses/isc/).
