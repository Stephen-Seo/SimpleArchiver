# Notes about security issues with simplearchiver

## 2016-10-20: The Decompressor Command

The latest updates for `simplearchiver` adds option `--non-interactive`. This
option is required when extracting/testing archives that require a decompressor
command to continue AND MUST ONLY BE USED WHEN THE USER IS ABSOLUTELY CERTAIN
THAT THE DECOMPRESSOR COMMAND IS SAFE TO RUN. Otherwise, `simplearchiver` will
emit an error message and will note the decompressor command. The user is
expected to verify that the decompressor command is as expected before
continuing with the `--non-interactive` flag.

### Vulnerable versions

Note that all versions prior to 4.0.0 of `simplearchiver` is potentially
vulnerable to a malicious decompressor command as these prior versions will
automatically use the decompressor command without pause (should one exist in
the archive). In other words, all versions of `simplearchiver` prior to 4.0.0
will run the "decompressor command" without checking during extraction/testing
(with `-x` option or `-t` option).

### Compressor/Decompressor usage process

Whenever an archive is created with `simplearchiver`, if compression is
desired, the user would specify `--compressor <command>` and `--decompressor
<command>`. `simplearchiver` would then store both commands in the archive
itself, and run the compressor command specified to store in the archive. When
extracting/testing, and if the decompressor was specified during archive
creation, then `simplearchiver` would just automatically use the decompressor
command, which was a mistake to do so unchecked. (Note there is a way to
override the decompressor command by using `--decompressor <command>` during
extraction/testing.)

### How the problem is addressed

The fix is as mentioned a `--non-interactive` flag to set, and MUST ONLY BE SET
AFTER CHECKING THE DECOMPRESSOR COMMAND. The error message emitted when
`--non-interactive` is required should note the decompressor command to check.
Again, if necessary, the decompressor command can be overridden with
`--decompressor <command>` when extracting/testing.

### Expected use case

The expected use-case behavior should be to run `simplearchiver` as usual
without the `--non-interactive` flag, and should the error message (noting
`--non-interactive`) be emitted, the user should check the decompressor command
to ensure it is valid/safe, and then the user should run the command again with
the `--non-interactive` flag set. If the command is deemed to be unsafe, the
user has the option to force the decompressor command used with
`--decompressor`.

### Closing notes

By the time this writeup is published, vulnerable versions of `simplearchiver`
should no longer be available. Versions 4.0.0 and up will remain. If you are
having trouble recovering data from an archive that has been tampered with, you
may try (with caution) to use an eariler 4 series version as they are less
strict than the very latest version.

## 2016-03-18: Path Validation

On the 18th of March, 2026, it was discovered that `simplearchiver` was
vulnerable to an issue in parsing existing archive files. In particular, every
version of `simplearchiver` prior to version `2.7` is vulnerable.

### Some Background

[`simplearchiver`](https://github.com/Stephen-Seo/SimpleArchiver) can be
described as an alternative to tar. It can create archives, and test/extract
them. Note that when testing or extracting a simplearchiver archive, a binary
file is parsed according to the file format specification [(the format of which is specified here)](https://github.com/Stephen-Seo/SimpleArchiver/blob/main/file_format.md).

### The Problem

The problem discovered in `simplearchiver` was this: all paths parsed in the
archive file were not validated. Normally, this wouldn't be an issue, since an
archive file created with `simplearchiver` is always properly created according
to the file format specification. However, such an archive file can be edited
by someone before being distributed to someone else. Again, this also can be a
non-issue since if anything important was being archived, the archive would've
been distributed with a cryptographic hash (like sha256) to verify its
validity.

The fix created for version `2.7` of `simplearchiver` is to validate every
parsed single filename, directory name, and symbolic link name. Specifically,
it checks if any of these names/strings start with "../", end with "/..", or
has "/../" in between. If such a string exists, then `simplearchiver` prints
that there was an error, and halts parsing immediately.

### Implications of the Problem

For those unfamiliar with directory traversal (common on Unix systems), any
path that contains a ".." usually refers to the "previous directory". For
example, say there was the following directory structure:

    outer_dir/
     - inner_dir/
       - another_dir/

If the current working directory is "another_dir" (by executing a command like
"cd outer_dir/inner_dir/another_dir"), then a command like "ls .." will print
the contents of "inner_dir", and "ls ../.." will print the contents of
"outer_dir".

As ".." refers to the previous dir, a command executed (from within
"another_dir") like "echo test > ../../test_file" will create a file called
"test_file" in the "outer_dir" directory.

The point is this: If `simplearchiver` accepted paths that had ".." in them,
then when a maliciously crafted archive is parsed for extraction, it may create
and/or modify files not in the current directory (or the directory specified by
the "-C <dir>" passed to `simplearchiver`).

### Closing Remarks

Unfortunately, this kind of problem is [not uncommon](https://github.com/advisories/GHSA-f842-phm9-p4v4). Software
development remains a type of work that is difficult to get things right,
especially on the first time. Luckily, there are
[tools](https://valgrind.org/docs/manual/mc-manual.html) to help identify and
fix bugs, and there are ways to [harden software](https://best.openssf.org/Compiler-Hardening-Guides/Compiler-Options-Hardening-Guide-for-C-and-C++.html)
to prevent other classes of bugs. It may be somewhat easy to introduce a bug in
one's software, but with the tooling available these days, it may be just as
easy to fix them. We can only hope that when such a bug appears, that it does
not cause a security issue for end-users, and that it is properly identified
and fixed before giving everyone a headache. 
