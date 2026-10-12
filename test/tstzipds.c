/* ------------------------------------------------------------------ */
/*  tstzipds.c - zip_add_dataset_bin() on MVS                         */
/*                                                                    */
/*  MVS only: the function reads a data set and allocates a temporary */
/*  one. The input is the fixture member SAMPLE (test/fixtures/       */
/*  sample.txt) on DD INPUT, FB 80. The archive itself is in storage, */
/*  between two ordinary entries, so the central directory has to     */
/*  come out with all three.                                          */
/* ------------------------------------------------------------------ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <mbtcheck.h>

#include "zlib.h"
#include "zip.h"
#include "unzip.h"
#include "memfile.h"

#define INPUT   "DD:INPUT(SAMPLE)"
#define MAXDATA 8192

static const char before[] = "first entry\n";
static const char after[]  = "last entry\n";

/* the member as fopen("rb") delivers it: the records, unchanged */
static long read_member(unsigned char *buf, unsigned long max)
{
    FILE   *fp = fopen(INPUT, "rb");
    long    n;

    if (fp == NULL)
        return -1;
    n = (long)fread(buf, 1, max, fp);
    fclose(fp);
    return n;
}

static int add_text(zipFile zf, const char *name, const char *text)
{
    int rc = zipOpenNewFileInZip(zf, name, NULL, NULL, 0, NULL, 0, NULL,
                                 Z_DEFLATED, Z_DEFAULT_COMPRESSION);
    if (rc == ZIP_OK)
        rc = zipWriteInFileInZip(zf, text, (unsigned)strlen(text));
    if (rc == ZIP_OK)
        rc = zipCloseFileInZip(zf);
    return rc;
}

int main(void)
{
    /* "SAMPLE.TXT" and "E,FB80" in ASCII: the function translates the
       entry name and the comment it writes */
    static const unsigned char name_ascii[] = {
        0x53, 0x41, 0x4D, 0x50, 0x4C, 0x45, 0x2E, 0x54, 0x58, 0x54, 0x00
    };
    static const unsigned char comment_ascii[] = {
        0x45, 0x2C, 0x46, 0x42, 0x38, 0x30, 0x00
    };
    struct memfile      m;
    zlib_filefunc_def   ff;
    zipFile             zf;
    unzFile             uf;
    unz_global_info     gi;
    unz_file_info       fi;
    unsigned char      *member;
    unsigned char      *data;
    char                name[64];
    char                comment[64];
    long                mlen;
    int                 rc, n;

    m.data = malloc(MEMMAX);
    member = malloc(MAXDATA);
    data   = malloc(MAXDATA);
    CHECK(m.data != NULL && member != NULL && data != NULL, "test buffers");
    if (m.data == NULL || member == NULL || data == NULL)
        return mbt_test_summary("TSTZIPDS");

    mlen = read_member(member, MAXDATA);
    CHECK(mlen > 0 && mlen % 80 == 0, "fixture member read, FB 80");

    mem_filefunc(&ff, &m);
    zf = zipOpen2("MEMORY", 0, NULL, &ff);
    CHECK(zf != NULL, "zipOpen2 on the storage file");
    if (zf == NULL)
        return mbt_test_summary("TSTZIPDS");

    CHECK_EQ(add_text(zf, "BEFORE", before), ZIP_OK, "ordinary entry before");
    rc = zip_add_dataset_bin(zf, INPUT, "SAMPLE.TXT");
    CHECK_EQ(rc, ZIP_OK, "zip_add_dataset_bin");
    CHECK_EQ(add_text(zf, "AFTER", after), ZIP_OK, "ordinary entry after");
    CHECK_EQ(zipClose(zf, NULL), ZIP_OK, "zipClose");
    printf("  archive: %lu bytes\n", m.size);

    uf = unzOpen2("MEMORY", &ff);
    CHECK(uf != NULL, "unzOpen2 on the storage file");
    if (uf == NULL)
        return mbt_test_summary("TSTZIPDS");

    CHECK_EQ(unzGetGlobalInfo(uf, &gi), UNZ_OK, "unzGetGlobalInfo");
    CHECK_EQ(gi.number_entry, 3, "three entries in the central directory");

    rc = unzLocateFile(uf, (const char *)name_ascii, 1);
    CHECK_EQ(rc, UNZ_OK, "the data set entry is found by its ASCII name");
    if (rc == UNZ_OK) {
        rc = unzGetCurrentFileInfo(uf, &fi, name, sizeof(name), NULL, 0,
                                   comment, sizeof(comment));
        CHECK_EQ(rc, UNZ_OK, "unzGetCurrentFileInfo");
        CHECK(strcmp(comment, (const char *)comment_ascii) == 0,
              "comment records the format, E,FB80");
        CHECK_EQ(fi.compression_method, Z_DEFLATED, "deflated");
        CHECK_EQ(fi.uncompressed_size, mlen, "uncompressed size");
        CHECK(fi.crc == crc32(0L, member, (uInt)mlen), "crc32");

        CHECK_EQ(unzOpenCurrentFile(uf), UNZ_OK, "unzOpenCurrentFile");
        n = unzReadCurrentFile(uf, data, MAXDATA);
        CHECK(n == mlen && memcmp(data, member, (size_t)mlen) == 0,
              "unzReadCurrentFile restores the member");
        CHECK_EQ(unzCloseCurrentFile(uf), UNZ_OK, "crc checked on close");
    }

    rc = unzLocateFile(uf, "AFTER", 1);
    CHECK_EQ(rc, UNZ_OK, "the entry after it is intact");
    if (rc == UNZ_OK) {
        CHECK_EQ(unzOpenCurrentFile(uf), UNZ_OK, "AFTER: open");
        n = unzReadCurrentFile(uf, data, MAXDATA);
        CHECK(n == (int)strlen(after) && memcmp(data, after, (size_t)n) == 0,
              "AFTER: data");
        CHECK_EQ(unzCloseCurrentFile(uf), UNZ_OK, "AFTER: crc");
    }

    CHECK_EQ(unzClose(uf), UNZ_OK, "unzClose");
    free(data);
    free(member);
    free(m.data);
    return mbt_test_summary("TSTZIPDS");
}
