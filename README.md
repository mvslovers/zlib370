# zlib370

**zlib 1.3.1 for MVS 3.8j, as a static library.**

zlib370 brings the [zlib](https://zlib.net) compression library to
MVS 3.8j: deflate and inflate, zlib and gzip wrapping, crc32 and adler32.
It is built on the workstation with CC/370 (cc370) against LIBC/370
(libc370) and linked into a program statically; nothing of it is
installed on MVS itself. It is an offer to developers: nothing requires
it, any program can use it.

> **Status:** under evaluation. The library builds and its tests pass on
> the workstation and on MVS.

## What is in it

`zlib.h` and `zconf.h`, with the zlib 1.3.1 API: `deflateInit2()`,
`deflate()`, `inflateInit2()`, `inflate()`, `compress()`/`uncompress()`,
`crc32()`, `adler32()` and the rest of the core.

gzip *file* I/O (`gzopen`, `gzread`, ...) is not included: the library is
built with `Z_SOLO`. gzip *streams* are, through `deflateInit2()` and
`inflateInit2()` with `windowBits` 31 (or 47 to accept zlib and gzip).

The linker takes only the modules a program calls, so a program that
only compresses gets no inflate code.

## Differences from upstream

- **External names.** MVS object modules allow eight characters. Every
  exported function carries an `asm("...")` label in `zlib.h`, `zutil.h`
  and the internal headers (`deflateInit2_` is `DEFLAT02`, and so on), so
  no two names collide when they are cut to eight characters.
- **Default allocator under `Z_SOLO`.** Upstream `Z_SOLO` requires the
  caller to supply `zalloc`/`zfree`. Here a stream with both left at zero
  uses `malloc`/`free`, for deflate and inflate alike.
- **`compress()`/`uncompress()` are available under `Z_SOLO`.** Upstream
  hides them in that configuration.

The library itself calls nothing from the C library but `malloc` and
`free`, and writes no messages.

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
`PATH`.

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

`TSTZLIB` runs on the workstation and on MVS: deflate/inflate round trips
with zlib and gzip wrapping and a small window, on EBCDIC text, random
binary data, an empty buffer and 70 000 bytes; the crc32 and adler32
check values; `compress`/`uncompress`, a damaged stream and an output
buffer that is too small. Every stream goes through a counting
allocator, so a leak fails the test.

## License

zlib is Copyright (C) 1995-2024 Jean-loup Gailly and Mark Adler, under the
[zlib license](https://zlib.net/zlib_license.html), reproduced at the top
of `zlib.h`. The sources here are an altered version as described above,
not the original software.
