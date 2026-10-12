# CLAUDE.md — zlib370 Project Instructions

Extends the root `CLAUDE.md` of the mvslovers ecosystem; never contradicts it.

## What is this project?

zlib 1.3.1 (deflate/inflate, zlib and gzip wrapping, crc32, adler32) and
minizip 0.21 (zip/unzip/ioapi) as a static library for MVS 3.8j.

**Status: under evaluation, not adopted** (maintainer, 2026-10-12). The
deciding question is the deflate storage per stream in a 24-bit address
space; the likely first consumer is HTTPD's `Content-Encoding: gzip`,
which needs deflate only. minizip builds and is tested, but nothing
depends on it until a consumer asks. New features wait for the decision.

## Build system

- **Compiler:** cc370 host toolchain; compile and archive run on the
  host, MVS is touched only by `mbt test --mvs`.
- **C library:** libc370 from the cc370 sysroot (`-lc`), not a declared
  dependency. zlib370 has no `[dependencies]`.
- **Build tool:** mbt 3, reading `mbt.toml` (schema 3). No submodule,
  no `Makefile`, no `VERSION`. The `mbt.toml` was written by hand:
  zlib370 came from mbt **v1**, which `mbt migrate` does not read.
- **Toolchain pins:** cc370 1.6.1, libc370 2.10.0 in `[toolchain]`.
- **Kind:** `library` → `build/zlib370.a`. `[lib] headers` lists what
  ships: `zlib.h`, `zconf.h`, `ioapi.h`, `unzip.h`, `zip.h`. Everything
  else in `include/` is internal.
- **Defines:** `Z_SOLO` (no gz file I/O), `NOCRYPT`, `USE_FILE32API`,
  `NO_ADDFILEINEXISTINGZIP`.

## Tests

mbt builds a test from its own file only, so each `[test.NAME]` lists
the sources it links. Names have at most 8 characters.

| Test | Where | Sources beyond the test |
|---|---|---|
| `TSTZLIB` | host + MVS | zlib core |
| `TSTZIP` | host + MVS | zlib core + `ioapi.c`, `unzip.c`, `zipopen.c` |
| `TSTZIPDS` | MVS (`host = false`) | as TSTZIP + `zipadd.c`; fixture `INPUT` |

- `test/memfile.h` gives zipOpen2()/unzOpen2() an archive in storage, so
  minizip tests need no data set. Include it from a test; it is not a test.
- Fixture lines beginning with `//` or `/*` arrive as data since mbt
  3.0.0-dev d58fd82 (mbt#261); an older mbt ends the in-stream data
  there and breaks the job.
- Binary or RECFM U/VB input cannot come from a fixture (FB 80 only).
- **dev1 is shared: ask the MBT session for a slot before `mbt test
  --mvs`**, and delete `IBMUSER.ZLIB370.**` (TESTLIB and FIX data sets)
  afterwards.

## Things that are not upstream zlib

1. **External names.** Every exported function has an `asm("NAME")`
   label in `zlib.h`, `zutil.h`, `zip.h`, `unzip.h`. cc370 cuts names to
   8 characters (`_` → `@`), so `deflateInit_`/`deflateInit2_` would both
   be `DEFLATEI`. A new exported function needs a label; check the
   archive's symbol table (`ar370 t build/zlib370.a`) for duplicates.
   `#pragma map` does not work with cc370 (it only warns).
2. **`Z_SOLO` with a default allocator.** deflate and inflate both fall
   back to `zcalloc`/`zcfree` (`zutil.c`) when `zalloc`/`zfree` are 0.
   Upstream `Z_SOLO` returns `Z_STREAM_ERROR`; do not restore that.
3. **`zip_add_dataset_bin()`** (`zipadd.c`) is MVS-only: it reads a data
   set, allocates `&&TEMPZIP` with `__dsalc()`, and translates the entry
   name and its format comment to ASCII with its own CP037 → ISO 8859-1
   table. `zipadd.c` duplicates the static helpers of `zipopen.c`.
4. **WTO diagnostics** in `zipadd.c`, `zipopen.c` and `deflate.c` come from
   `<mvs/wto.h>`, included under `#ifdef __MVS__`; on the host `wtof` and
   `wtodumpf` are no-op macros so the code builds natively.
5. **Copyright strings** are kept in the object with
   `__attribute__((used))`.

## Measured figures (dev1, 2026-10-12)

Peak bytes per stream, level 6: deflate 15/8 = 267 972, 13/6 = 71 364,
12/4 = 30 404; inflate 15 = 39 888. Deflate ~1.2 s CPU per MB on dev1,
inflate ~0.3 s. Details and job numbers are in zlib370#2 and the reports
to the MBT session; re-measure rather than quoting these after a change
to deflate.

## Rules

- `-Wall -Wextra -Werror` with cc370 **and** the host compiler; fix,
  never suppress.
- EBCDIC: no ASCII constants in logic. ZIP format bytes (signatures,
  ASCII names) are binary data and are written as byte values.
- A change that can alter generated code for MVS is checked by comparing
  cc370 object decks before and after, not by reading the diff.
