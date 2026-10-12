# zlib370

**zlib 1.3.1 and minizip for MVS 3.8j, as a static library.**

zlib370 brings the [zlib](https://zlib.net) compression library (deflate,
inflate, zlib and gzip wrapping, crc32, adler32) and the minizip ZIP
reader/writer to MVS 3.8j. It is built on the workstation with CC/370
(cc370) against LIBC/370 (libc370) and linked into a program statically.
Nothing of it is installed on MVS itself.

> **Status:** under evaluation. The library builds and its tests pass on
> the workstation and on MVS. Whether it becomes a maintained part of the
> mvslovers projects is still open.

## What is in it

| Header | What |
|---|---|
| `zlib.h`, `zconf.h` | zlib 1.3.1: `deflate*`, `inflate*`, `compress`/`uncompress`, `crc32`, `adler32` |
| `zip.h`, `unzip.h`, `ioapi.h` | minizip 0.21 (Gilles Vollant): write and read ZIP archives, with pluggable file functions |

gzip *file* I/O (`gzopen`, `gzread`, ...) is not included: the library is
built with `Z_SOLO`. gzip *streams* are, through `deflateInit2()` and
`inflateInit2()` with `windowBits` 31 (or 47 to detect zlib or gzip).

## Differences from upstream

These sources are altered from the zlib and minizip distributions:

- **External names.** MVS object modules allow eight characters. Every
  exported function carries an `asm("...")` label in `zlib.h`, `zutil.h`,
  `zip.h` and `unzip.h` (`deflateInit2_` is `DEFLAT02`, and so on), so no
  two names collide when they are cut to eight characters.
- **Default allocator under `Z_SOLO`.** Upstream `Z_SOLO` requires the
  caller to supply `zalloc`/`zfree`. Here a stream with both left at zero
  uses `malloc`/`free`, for deflate and inflate alike.
- **`zip_add_dataset_bin(zipfile, dataset, name)`** adds an MVS data set
  to an archive, read in binary. The entry name and a comment that records
  the record format (`E,FB80`, for example) are translated to ASCII
  (code page 037 to ISO 8859-1). It allocates a temporary data set
  (`&&TEMPZIP`) while it works.
- **Diagnostics.** `zip_add_dataset_bin()`, `zipOpenNewFileInZip2()` and
  error paths in `deflateInit2_()` write trace messages to the console
  with WTO.
- Everything compiles cleanly under `-Wall -Wextra -Werror`, with cc370 and
  with a native compiler.

Entries written with `zipOpenNewFileInZip()` keep their names and data
exactly as given: on MVS that is EBCDIC. Translate names to ASCII yourself
if the archive is meant for another system.

## Storage

Each stream allocates its working storage through `zalloc`. Measured on
MVS 3.8j at compression level 6 (peak bytes held per stream):

| Stream | `windowBits` / `memLevel` | Bytes |
|---|---|---|
| deflate | 15 / 8 (default) | 267 972 |
| deflate | 13 / 6 | 71 364 |
| deflate | 12 / 4 | 30 404 |
| inflate | 15 | 39 888 |

On text such as JSON listings and job output, 13/6 compressed as well as
the default (7.9 % against 8.0 % of the original size); 12/4 reached
9.2 %. In a 24-bit address space, choose the smallest deflate setting
that compresses well enough.

## Build

The library builds on the workstation with the cc370 toolchain and
[mbt 3](https://github.com/mvslovers/mbt), the `mbt` program on your
`PATH`. MVS is touched only by the MVS test run.

```sh
mbt build          # build/zlib370.a
mbt test           # build and run the tests on the workstation
mbt test --mvs     # run the tests on MVS (batch and TSO steps)
mbt package        # release tarball (library + headers) in dist/
mbt doctor         # check the toolchain and the MVS connection
```

`[toolchain]` in `mbt.toml` pins the mbt, cc370 and libc370 versions a
release is built with.

Once a release is published, a project uses the library through its
`mbt.toml`:

```toml
[dependencies]
"mvslovers/zlib370" = ">=1.0.0"
```

## Tests

| Test | Runs on | Covers |
|---|---|---|
| `TSTZLIB` | workstation, MVS | deflate/inflate round trips (zlib, gzip, small window; EBCDIC text, binary, empty, over 64 KB), crc32/adler32 check values, `compress`/`uncompress` |
| `TSTZIP` | workstation, MVS | minizip: write and read an archive held in storage |
| `TSTZIPDS` | MVS | `zip_add_dataset_bin()` on a fixture member |

## License

zlib is Copyright (C) 1995-2024 Jean-loup Gailly and Mark Adler, minizip
Copyright (C) 1998-2003 Gilles Vollant, both under the
[zlib license](https://zlib.net/zlib_license.html), reproduced at the top
of `zlib.h`. The sources here are altered versions as described above,
not the original software.
