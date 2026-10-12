/* ------------------------------------------------------------------ */
/*  tstzlib.c - zlib370 round-trip tests                              */
/*                                                                    */
/*  Built and run by mbt: `mbt test` on the host, `mbt test --mvs`    */
/*  on MVS. [test.TSTZLIB] in mbt.toml links it with the zlib core.   */
/*                                                                    */
/*  Every round trip deflates, inflates and compares, through an      */
/*  allocator that counts, so a stream that leaks fails the test.     */
/* ------------------------------------------------------------------ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <mbtcheck.h>

#include "zlib.h"

/* "123456789" in ASCII: the check value input of every CRC catalogue.
   Binary data, so it is written as bytes, not as a string literal. */
static const unsigned char check9[9] = {
    0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39
};

/* Above 64 KB, so a 16-bit length or offset would wrap. */
#define BIGLEN 70000UL

/* ------------------------------------------------------------------ */
/*  Counting allocator                                                */
/* ------------------------------------------------------------------ */

struct counter {
    unsigned long allocs;
    unsigned long frees;
    unsigned long cur;          /* bytes held now */
    unsigned long peak;         /* most bytes held at once */
};

/* Each block carries its size in front, so a free can be counted. */
static voidpf count_alloc(voidpf opaque, uInt items, uInt size)
{
    struct counter *c = (struct counter *)opaque;
    unsigned long   n = (unsigned long)items * size;
    unsigned long  *p = malloc(n + sizeof(unsigned long) * 2);

    if (p == NULL)
        return Z_NULL;
    p[0] = n;
    c->allocs++;
    c->cur += n;
    if (c->cur > c->peak)
        c->peak = c->cur;
    return (voidpf)(p + 2);
}

static void count_free(voidpf opaque, voidpf address)
{
    struct counter *c = (struct counter *)opaque;
    unsigned long  *p = (unsigned long *)address - 2;

    c->frees++;
    c->cur -= p[0];
    free(p);
}

/* ------------------------------------------------------------------ */
/*  One round trip                                                    */
/* ------------------------------------------------------------------ */

/* Deflate src with the given window and memory level, inflate it again
   and compare. windowBits 31 writes a gzip wrapper and checks it on the
   way back. Returns the compressed length, 0 on failure. */
static uLong roundtrip(const char *what, const unsigned char *src, uLong len,
                       int windowBits, int memLevel)
{
    z_stream        zs;
    struct counter  dc;
    struct counter  ic;
    unsigned char  *packed;
    unsigned char  *unpacked;
    uLong           bound;
    uLong           plen = 0;
    int             rc;
    char            msg[80];

    memset(&dc, 0, sizeof(dc));
    memset(&ic, 0, sizeof(ic));

    /* deflateBound does not know about the gzip wrapper: 18 bytes */
    bound    = compressBound(len) + 18;
    packed   = malloc(bound);
    unpacked = malloc(len + 1);
    if (packed == NULL || unpacked == NULL) {
        sprintf(msg, "%s: test buffers", what);
        CHECK(0, msg);
        free(packed);
        free(unpacked);
        return 0;
    }

    memset(&zs, 0, sizeof(zs));
    zs.zalloc = count_alloc;
    zs.zfree  = count_free;
    zs.opaque = &dc;
    rc = deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, windowBits,
                      memLevel, Z_DEFAULT_STRATEGY);
    sprintf(msg, "%s: deflateInit2", what);
    CHECK_EQ(rc, Z_OK, msg);
    if (rc == Z_OK) {
        zs.next_in   = (Bytef *)src;
        zs.avail_in  = (uInt)len;
        zs.next_out  = packed;
        zs.avail_out = (uInt)bound;
        rc = deflate(&zs, Z_FINISH);
        sprintf(msg, "%s: deflate to Z_STREAM_END", what);
        CHECK_EQ(rc, Z_STREAM_END, msg);
        plen = zs.total_out;
        sprintf(msg, "%s: deflateEnd", what);
        CHECK_EQ(deflateEnd(&zs), Z_OK, msg);
    }
    sprintf(msg, "%s: deflate frees what it allocates", what);
    CHECK(dc.allocs == dc.frees && dc.cur == 0, msg);

    if (windowBits > 15) {
        sprintf(msg, "%s: gzip magic 1F8B", what);
        CHECK(plen > 2 && packed[0] == 0x1F && packed[1] == 0x8B, msg);
    }

    memset(&zs, 0, sizeof(zs));
    zs.zalloc = count_alloc;
    zs.zfree  = count_free;
    zs.opaque = &ic;
    rc = inflateInit2(&zs, windowBits);
    sprintf(msg, "%s: inflateInit2", what);
    CHECK_EQ(rc, Z_OK, msg);
    if (rc == Z_OK) {
        zs.next_in   = packed;
        zs.avail_in  = (uInt)plen;
        zs.next_out  = unpacked;
        zs.avail_out = (uInt)(len + 1);
        rc = inflate(&zs, Z_FINISH);
        sprintf(msg, "%s: inflate to Z_STREAM_END", what);
        CHECK_EQ(rc, Z_STREAM_END, msg);
        sprintf(msg, "%s: inflate restores the length", what);
        CHECK_EQ(zs.total_out, len, msg);
        sprintf(msg, "%s: inflate restores the data", what);
        CHECK(zs.total_out == len && memcmp(src, unpacked, len) == 0, msg);
        sprintf(msg, "%s: inflateEnd", what);
        CHECK_EQ(inflateEnd(&zs), Z_OK, msg);
    }
    sprintf(msg, "%s: inflate frees what it allocates", what);
    CHECK(ic.allocs == ic.frees && ic.cur == 0, msg);

    printf("  %s: %lu -> %lu bytes, peak deflate %lu, inflate %lu\n",
           what, len, plen, dc.peak, ic.peak);

    free(packed);
    free(unpacked);
    return plen;
}

/* ------------------------------------------------------------------ */
/*  Test data                                                         */
/* ------------------------------------------------------------------ */

/* Text as the compiler encodes it: EBCDIC on MVS, ASCII on the host. */
static const char *lines[] = {
    "IEF236I ALLOC. FOR HTTPD HTTPD\n",
    "{\"items\":[{\"dsname\":\"SYS1.MACLIB\",\"vol\":\"MVSRES\"}]}\n",
    "        LA    R1,PARMLIST         POINT TO PARAMETER LIST\n",
    "The quick brown fox jumps over the lazy dog. 0123456789\n",
};

static void fill_text(unsigned char *buf, uLong len)
{
    uLong   i = 0;
    int     n = 0;

    while (i < len) {
        const char *s = lines[n++ % 4];
        while (*s && i < len)
            buf[i++] = (unsigned char)*s++;
    }
}

/* A linear congruential generator: the same bytes on every system,
   and nothing for deflate to find. */
static void fill_binary(unsigned char *buf, uLong len)
{
    unsigned long   x = 12345;
    uLong           i;

    for (i = 0; i < len; i++) {
        x = (x * 1103515245UL + 12345UL) & 0x7FFFFFFFUL;
        buf[i] = (unsigned char)(x >> 16);
    }
}

int main(void)
{
    unsigned char  *data;
    unsigned char   packed[512];
    unsigned char   out[16];
    uLongf          plen;
    uLongf          ulen;
    uLong           n;
    int             rc;

    CHECK_EQ(crc32(0L, check9, sizeof(check9)), 0xCBF43926UL,
             "crc32 check value");
    CHECK_EQ(adler32(1L, check9, sizeof(check9)), 0x091E01DEUL,
             "adler32 check value");

    data = malloc(BIGLEN);
    if (data == NULL) {
        CHECK(0, "test buffer");
        return mbt_test_summary("TSTZLIB");
    }

    fill_text(data, 4096);
    n = roundtrip("text 4K zlib", data, 4096, 15, 8);
    CHECK(n > 0 && n < 4096 / 4, "text 4K compresses to under a quarter");

    roundtrip("text 4K gzip", data, 4096, 31, 8);
    roundtrip("text 4K small", data, 4096, 12, 4);

    fill_text(data, BIGLEN);
    roundtrip("text 70K zlib", data, BIGLEN, 15, 8);
    roundtrip("text 70K gzip", data, BIGLEN, 31, 8);

    fill_binary(data, BIGLEN);
    n = roundtrip("binary 70K zlib", data, BIGLEN, 15, 8);
    CHECK(n >= BIGLEN, "random data does not shrink");

    roundtrip("empty zlib", data, 0, 15, 8);
    roundtrip("empty gzip", data, 0, 31, 8);

    /* the one-call interface, with zalloc left at zero */
    fill_text(data, 4096);
    plen = compressBound(4096);
    {
        unsigned char *p = malloc(plen);
        unsigned char *u = malloc(4096);

        CHECK(p != NULL && u != NULL, "compress buffers");
        if (p != NULL && u != NULL) {
            rc = compress2(p, &plen, data, 4096, Z_BEST_COMPRESSION);
            CHECK_EQ(rc, Z_OK, "compress2");
            ulen = 4096;
            rc = uncompress(u, &ulen, p, plen);
            CHECK_EQ(rc, Z_OK, "uncompress with the default allocator");
            CHECK(ulen == 4096 && memcmp(data, u, 4096) == 0,
                  "uncompress restores the data");

            /* a damaged stream must be refused, not decoded */
            p[plen / 2] ^= 0xFF;
            ulen = 4096;
            rc = uncompress(u, &ulen, p, plen);
            CHECK(rc != Z_OK, "uncompress refuses a damaged stream");
        }
        free(p);
        free(u);
    }

    /* an output buffer too small is reported, not overrun */
    fill_text(data, 4096);
    plen = sizeof(packed);
    rc = compress(packed, &plen, data, 4096);
    CHECK_EQ(rc, Z_OK, "compress 4K of text into 512 bytes");
    ulen = sizeof(out);
    rc = uncompress(out, &ulen, packed, plen);
    CHECK_EQ(rc, Z_BUF_ERROR, "uncompress into 16 bytes is Z_BUF_ERROR");

    free(data);
    return mbt_test_summary("TSTZLIB");
}
