# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/).

## [1.0.0] - 2026-10-13

First release: zlib 1.3.1 as a static library for MVS 3.8j, built with
cc370 and libc370 2.x. Released against cc370 1.6.1 and libc370 2.10.0.

### Added
- zlib 1.3.1 core: deflate and inflate, zlib and gzip wrapping (through
  `windowBits`), `compress()`/`uncompress()`, `crc32()` and `adler32()`.
  The package ships `zlib.h`, `zconf.h` and `zlib370.a`.
- `TSTZLIB`: round trips with zlib and gzip wrapping and a small window
  (EBCDIC text, random binary data, an empty buffer, 70 000 bytes), the
  crc32 and adler32 check values, a damaged stream and an output buffer
  that is too small, all through a counting allocator so a leak fails.
  Passes on the workstation and on MVS, in batch and under TSO.

### Changed (against upstream zlib 1.3.1)
- Built with `Z_SOLO`: no gzip file I/O (`gzopen`, `gzread`, ...).
- Every external function and table carries an `asm("...")` label, so no
  two names collide when cut to the eight characters of an MVS object
  module (`deflateInit_` is `DEFLAT01`, `deflateInit2_` is `DEFLAT02`;
  cut by the compiler, both would be `DEFLATEI`).
- A stream whose `zalloc` and `zfree` are both zero uses `malloc` and
  `free`, for deflate and inflate alike; upstream `Z_SOLO` refuses it with
  `Z_STREAM_ERROR`.
- `compress()`, `compress2()`, `compressBound()`, `uncompress()` and
  `uncompress2()` are declared under `Z_SOLO`; upstream hides them there.
- The library calls nothing from the C library but `malloc` and `free`,
  and writes no messages.
