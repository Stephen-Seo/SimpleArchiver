# Simple Archiver

This program ~~is not yet~~ almost finished! Basic functionality is implemented
and only some advanced features are missing. You can track progress
[here](https://git.seodisparate.com/stephenseo/SimpleArchiver/projects/3).

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
    --compressor <full_compress_cmd> : requires --decompressor
    --decompressor <full_decompress_cmd> : requires --compressor
      Specifying "--decompressor" when extracting overrides archive file's stored decompressor cmd
    --overwrite-create : allows overwriting an archive file
    --overwrite-extract : allows overwriting when extracting
    --no-abs-symlink : do not store absolute paths for symlinks
    --temp-files-dir <dir> : where to store temporary files created when compressing (defaults to current working directory)
    -- : specifies remaining arguments are files to archive/extract
    If creating archive file, remaining args specify files to archive.
    If extracting archive file, remaining args specify files to extract.

Note that `--compressor` and `--decompressor` cmds must accept data from stdin
and return processed data to stdout.

## LICENSE Information

Uses the [ISC License](https://choosealicense.com/licenses/isc/).
