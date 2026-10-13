# CLAUDE.md — zlib370 Project Instructions

Extends the root `CLAUDE.md` of the mvslovers ecosystem; never contradicts it.

## What is this project?

zlib 1.3.1 (deflate/inflate, zlib and gzip wrapping, crc32, adler32) as a
static library for MVS 3.8j. An offer to developers; nothing has to use it.

**Status: 1.0.0 released** (2026-10-13); whether other products adopt it
is under evaluation (maintainer, 2026-10-12). The intended first
consumer is HTTPD's `Content-Encoding: gzip`, which needs deflate only.

**Releasing:** `mbt release X.Y.Z` on a clean `main` at `X.Y.Z-dev`; it
pushes the release commit, the tag and the next `-dev` bump straight to
`main`, and `release.yml` publishes `zlib370-X.Y.Z-lib.tar.gz`. Add the
`CHANGELOG.md` section before, rework the release page after (root
`CLAUDE.md`, Git Workflow). No FMID: the library is linked statically.

**zlib only, no minizip** (maintainer, 2026-10-12). The ZIP code that was
here (minizip 0.21 + an MVS `zip_add_dataset_bin()`) was removed: MVS
MINIZIP/MINIUNZ are maintained elsewhere (MVS-sysgen/minizip, shipped
with TK5). Do not bring ZIP support back into this library.

## Build system

- **Compiler:** cc370 host toolchain; MVS is touched only by
  `mbt test --mvs`.
- **C library:** libc370 from the cc370 sysroot (`-lc`), not a declared
  dependency. zlib370 has no `[dependencies]`. The library calls only
  `malloc` and `free`.
- **Build tool:** mbt 3, reading `mbt.toml` (schema 3). No submodule, no
  `Makefile`, no `VERSION`. The `mbt.toml` was written by hand: zlib370
  came from mbt **v1**, which `mbt migrate` does not read.
- **Toolchain pins:** cc370 1.6.1, libc370 2.10.0 in `[toolchain]`.
- **Kind:** `library` → `build/zlib370.a`. Only `zlib.h` and `zconf.h`
  ship; the other headers in `include/` are internal.
- **Define:** `Z_SOLO` (no gz file I/O).

## Tests

`TSTZLIB` (`test/tstzlib.c`, host and MVS) links `src/*.c`. A new test
needs a name of at most 8 characters and its own `[test.NAME]` sources.
**dev1 is shared: ask the MBT session for a slot before `mbt test
--mvs`**, and delete `IBMUSER.ZLIB370.**` afterwards.

## Differences from upstream zlib 1.3.1 — keep them, and only them

Compare with the upstream tarball (`zlib.net/fossils/zlib-1.3.1.tar.gz`)
before and after a change; everything else is upstream byte for byte
(ignoring whitespace).

1. **External names.** Every external function and table has an
   `asm("NAME")` label in `zlib.h`, `zutil.h`, `deflate.h`, `inffast.h`,
   `inftrees.h`. cc370 cuts names to 8 characters (`_` → `@`), so
   `deflateInit_`/`deflateInit2_` would both be `DEFLATEI`. A new external
   needs a label; check `ar370 t build/zlib370.a` for duplicates.
   `#pragma map` does not work with cc370 (it only warns); upstream's
   block for it is removed from `zconf.h`.
2. **`Z_SOLO` with a default allocator.** `deflateInit2_`,
   `inflateInit2_` and `inflateBackInit_` fall back to `zcalloc`/`zcfree`
   when `zalloc`/`zfree` are 0, and `zutil.c`/`zutil.h` provide them under
   `Z_SOLO`. Upstream returns `Z_STREAM_ERROR`; do not restore that.
3. **`compress()`/`uncompress()` under `Z_SOLO`.** `zlib.h` declares the
   utility functions `compress`, `compress2`, `compressBound`, `uncompress`
   and `uncompress2` outside its `#ifndef Z_SOLO` block (upstream hides
   them under `Z_SOLO`); the gz file functions stay hidden.
4. **`zconf.h`** wraps the `<sys/types.h>` include in `#if
   !defined(__MVS__)`. Under `Z_SOLO` it is not reached anyway.
5. **Copyright strings** are `static` and kept with
   `__attribute__((used))`.
6. **No diagnostics.** The library writes nothing (no WTO, no printf).

## Measured figures (dev1, 2026-10-12)

Peak bytes per stream, level 6: deflate 15/8 = 267 972, 13/6 = 71 364,
12/4 = 30 404; inflate 15 = 39 888. Deflate ~1.2 s CPU per MB on dev1,
inflate ~0.3 s. Measured with the earlier deflate.c that carried WTO
calls on its error paths only; re-measure after a change to deflate.

## Rules

- `-Wall -Wextra -Werror` with cc370 **and** the host compiler; fix,
  never suppress.
- A change that can alter generated code for MVS is checked by comparing
  cc370 object decks before and after, not by reading the diff.
