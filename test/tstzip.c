/* ------------------------------------------------------------------ */
/*  tstzip.c - minizip round-trip tests                               */
/*                                                                    */
/*  Built and run by mbt: `mbt test` on the host, `mbt test --mvs`    */
/*  on MVS. [test.TSTZIP] in mbt.toml links it with the zlib core     */
/*  and zipopen.c, unzip.c and ioapi.c.                               */
/*                                                                    */
/*  The archive lives in storage: zipOpen2() and unzOpen2() are given */
/*  file functions over a buffer, so the test needs no data set and   */
/*  runs the same on both systems.                                    */
/* ------------------------------------------------------------------ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <mbtcheck.h>

#include "zlib.h"
#include "zip.h"
#include "unzip.h"
#include "memfile.h"

/* ------------------------------------------------------------------ */
/*  Test data                                                         */
/* ------------------------------------------------------------------ */

#define NFILES  4
#define BIGLEN  70000UL         /* above 64 KB */

struct entry {
    const char     *name;
    int             method;     /* 0 stored, Z_DEFLATED */
    unsigned char  *data;
    uLong           len;
};

static const char text[] =
    "//IBMUSERA JOB (ACCT),'ZIP TEST',CLASS=A,MSGCLASS=H\n"
    "//STEP1    EXEC PGM=IEFBR14\n"
    "//DD1      DD   DSN=IBMUSER.TEST.DATA,DISP=(NEW,CATLG),\n"
    "//              SPACE=(TRK,(1,1)),UNIT=SYSDA\n";

static const char gcomment[] = "zlib370 minizip test";

/* writes the entries; returns 1 when every call succeeded */
static int write_zip(zlib_filefunc_def *ff, struct entry *e)
{
    zipFile         zf;
    zip_fileinfo    zi;
    int             i, ok = 1;
    char            msg[80];

    zf = zipOpen2("MEMORY", 0, NULL, ff);
    CHECK(zf != NULL, "zipOpen2 on the storage file");
    if (zf == NULL)
        return 0;

    memset(&zi, 0, sizeof(zi));
    zi.tmz_date.tm_year = 2026;
    zi.tmz_date.tm_mon  = 9;        /* October */
    zi.tmz_date.tm_mday = 12;
    zi.tmz_date.tm_hour = 18;
    zi.tmz_date.tm_min  = 30;

    for (i = 0; i < NFILES; i++) {
        uLong off;
        int   rc;

        rc = zipOpenNewFileInZip(zf, e[i].name, &zi, NULL, 0, NULL, 0,
                                 NULL, e[i].method, Z_DEFAULT_COMPRESSION);
        sprintf(msg, "zipOpenNewFileInZip %s", e[i].name);
        CHECK_EQ(rc, ZIP_OK, msg);
        ok &= rc == ZIP_OK;

        /* in pieces, as a caller reading a data set would */
        for (off = 0; rc == ZIP_OK && off < e[i].len; off += 8192) {
            uLong n = e[i].len - off > 8192 ? 8192 : e[i].len - off;
            rc = zipWriteInFileInZip(zf, e[i].data + off, (unsigned)n);
        }
        sprintf(msg, "zipWriteInFileInZip %s", e[i].name);
        CHECK_EQ(rc, ZIP_OK, msg);
        ok &= rc == ZIP_OK;

        rc = zipCloseFileInZip(zf);
        sprintf(msg, "zipCloseFileInZip %s", e[i].name);
        CHECK_EQ(rc, ZIP_OK, msg);
        ok &= rc == ZIP_OK;
    }

    CHECK_EQ(zipClose(zf, gcomment), ZIP_OK, "zipClose");
    return ok;
}

static void read_zip(zlib_filefunc_def *ff, struct entry *e)
{
    unzFile             uf;
    unz_global_info     gi;
    unz_file_info       fi;
    char                name[64];
    char                comment[64];
    unsigned char      *buf;
    int                 i, n, rc;
    char                msg[80];

    uf = unzOpen2("MEMORY", ff);
    CHECK(uf != NULL, "unzOpen2 on the storage file");
    if (uf == NULL)
        return;

    CHECK_EQ(unzGetGlobalInfo(uf, &gi), UNZ_OK, "unzGetGlobalInfo");
    CHECK_EQ(gi.number_entry, NFILES, "number of entries");
    CHECK_EQ(gi.size_comment, sizeof(gcomment) - 1, "global comment length");
    rc = unzGetGlobalComment(uf, comment, sizeof(comment));
    CHECK(rc == (int)sizeof(gcomment) - 1 && strcmp(comment, gcomment) == 0,
          "global comment");

    buf = malloc(BIGLEN + 1);
    CHECK(buf != NULL, "read buffer");
    if (buf == NULL) {
        unzClose(uf);
        return;
    }

    /* walk the central directory in order */
    rc = unzGoToFirstFile(uf);
    for (i = 0; i < NFILES; i++) {
        sprintf(msg, "entry %d present", i);
        CHECK_EQ(rc, UNZ_OK, msg);
        if (rc != UNZ_OK)
            break;

        rc = unzGetCurrentFileInfo(uf, &fi, name, sizeof(name),
                                   NULL, 0, NULL, 0);
        sprintf(msg, "unzGetCurrentFileInfo %d", i);
        CHECK_EQ(rc, UNZ_OK, msg);
        sprintf(msg, "name of entry %d is %s", i, e[i].name);
        CHECK(strcmp(name, e[i].name) == 0, msg);
        sprintf(msg, "%s: method", e[i].name);
        CHECK_EQ(fi.compression_method, e[i].method, msg);
        sprintf(msg, "%s: uncompressed size", e[i].name);
        CHECK_EQ(fi.uncompressed_size, e[i].len, msg);
        sprintf(msg, "%s: crc32", e[i].name);
        CHECK(fi.crc == crc32(0L, e[i].data, (uInt)e[i].len), msg);
        sprintf(msg, "%s: date 2026-10-12 18:30", e[i].name);
        CHECK(fi.tmu_date.tm_year == 2026 && fi.tmu_date.tm_mon == 9 &&
              fi.tmu_date.tm_mday == 12 && fi.tmu_date.tm_hour == 18 &&
              fi.tmu_date.tm_min == 30, msg);

        rc = unzOpenCurrentFile(uf);
        sprintf(msg, "%s: unzOpenCurrentFile", e[i].name);
        CHECK_EQ(rc, UNZ_OK, msg);
        n = unzReadCurrentFile(uf, buf, (unsigned)(BIGLEN + 1));
        sprintf(msg, "%s: unzReadCurrentFile restores the data", e[i].name);
        CHECK(n == (int)e[i].len && memcmp(buf, e[i].data, e[i].len) == 0,
              msg);
        sprintf(msg, "%s: unzCloseCurrentFile checks the crc", e[i].name);
        CHECK_EQ(unzCloseCurrentFile(uf), UNZ_OK, msg);

        rc = unzGoToNextFile(uf);
    }
    CHECK_EQ(rc, UNZ_END_OF_LIST_OF_FILE, "no entry after the last");

    /* find one by name */
    rc = unzLocateFile(uf, e[2].name, 1);
    CHECK_EQ(rc, UNZ_OK, "unzLocateFile finds an entry");
    rc = unzLocateFile(uf, "NOT.THERE", 1);
    CHECK_EQ(rc, UNZ_END_OF_LIST_OF_FILE, "unzLocateFile misses a stranger");

    free(buf);
    CHECK_EQ(unzClose(uf), UNZ_OK, "unzClose");
}

int main(void)
{
    struct memfile      m;
    zlib_filefunc_def   ff;
    struct entry        e[NFILES];
    unsigned char      *big;
    unsigned long       x = 4711;
    uLong               i;

    m.data = malloc(MEMMAX);
    big = malloc(BIGLEN);
    CHECK(m.data != NULL && big != NULL, "test buffers");
    if (m.data == NULL || big == NULL)
        return mbt_test_summary("TSTZIP");

    /* random bytes: the deflated entry ends up larger than stored */
    for (i = 0; i < BIGLEN; i++) {
        x = (x * 1103515245UL + 12345UL) & 0x7FFFFFFFUL;
        big[i] = (unsigned char)(x >> 16);
    }

    e[0].name = "JCL/STORED.TXT";   e[0].method = 0;
    e[0].data = (unsigned char *)text;  e[0].len = sizeof(text) - 1;
    e[1].name = "JCL/DEFLATED.TXT"; e[1].method = Z_DEFLATED;
    e[1].data = (unsigned char *)text;  e[1].len = sizeof(text) - 1;
    e[2].name = "BIN/RANDOM.BIN";   e[2].method = Z_DEFLATED;
    e[2].data = big;                    e[2].len = BIGLEN;
    e[3].name = "EMPTY";            e[3].method = Z_DEFLATED;
    e[3].data = big;                    e[3].len = 0;

    mem_filefunc(&ff, &m);
    if (write_zip(&ff, e)) {
        /* local header signature "PK\3\4" and end of central directory
           "PK\5\6", in bytes: the format is the same on every system */
        CHECK(m.size > 22 && m.data[0] == 0x50 && m.data[1] == 0x4B &&
              m.data[2] == 0x03 && m.data[3] == 0x04, "local header magic");
        CHECK(m.size > 22 + sizeof(gcomment) - 1 &&
              m.data[m.size - 22 - (sizeof(gcomment) - 1)] == 0x50 &&
              m.data[m.size - 21 - (sizeof(gcomment) - 1)] == 0x4B &&
              m.data[m.size - 20 - (sizeof(gcomment) - 1)] == 0x05 &&
              m.data[m.size - 19 - (sizeof(gcomment) - 1)] == 0x06,
              "end of central directory magic");
        printf("  archive: %lu bytes\n", m.size);
        read_zip(&ff, e);
    }

    /* a truncated archive is refused */
    m.size /= 2;
    CHECK(unzOpen2("MEMORY", &ff) == NULL, "unzOpen2 refuses a cut archive");

    free(big);
    free(m.data);
    return mbt_test_summary("TSTZIP");
}
