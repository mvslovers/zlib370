/* ------------------------------------------------------------------ */
/*  memfile.h - a ZIP archive in storage, for the minizip tests       */
/*                                                                    */
/*  zipOpen2() and unzOpen2() take these file functions, so a test    */
/*  needs no data set for the archive itself.                         */
/* ------------------------------------------------------------------ */

#ifndef MEMFILE_H
#define MEMFILE_H

#include <string.h>

#include "ioapi.h"

#define MEMMAX  200000UL

struct memfile {
    unsigned char  *data;
    uLong           size;       /* bytes written so far */
    uLong           pos;
    int             error;
};

static voidpf ZCALLBACK mem_open(voidpf opaque, const char *name, int mode)
{
    struct memfile *m = (struct memfile *)opaque;

    (void)name;
    if (mode & ZLIB_FILEFUNC_MODE_CREATE)
        m->size = 0;
    m->pos = 0;
    m->error = 0;
    return m;
}

static uLong ZCALLBACK mem_read(voidpf opaque, voidpf stream, void *buf,
                                uLong size)
{
    struct memfile *m = (struct memfile *)stream;

    (void)opaque;
    if (m->pos >= m->size)
        return 0;
    if (size > m->size - m->pos)
        size = m->size - m->pos;
    memcpy(buf, m->data + m->pos, size);
    m->pos += size;
    return size;
}

static uLong ZCALLBACK mem_write(voidpf opaque, voidpf stream,
                                 const void *buf, uLong size)
{
    struct memfile *m = (struct memfile *)stream;

    (void)opaque;
    if (m->pos + size > MEMMAX) {
        m->error = 1;
        return 0;
    }
    memcpy(m->data + m->pos, buf, size);
    m->pos += size;
    if (m->pos > m->size)
        m->size = m->pos;
    return size;
}

static long ZCALLBACK mem_tell(voidpf opaque, voidpf stream)
{
    (void)opaque;
    return (long)((struct memfile *)stream)->pos;
}

static long ZCALLBACK mem_seek(voidpf opaque, voidpf stream, uLong offset,
                               int origin)
{
    struct memfile *m = (struct memfile *)stream;
    uLong           base;

    (void)opaque;
    switch (origin) {
    case ZLIB_FILEFUNC_SEEK_SET: base = 0;       break;
    case ZLIB_FILEFUNC_SEEK_CUR: base = m->pos;  break;
    case ZLIB_FILEFUNC_SEEK_END: base = m->size; break;
    default: return -1;
    }
    if (base + offset > m->size)
        return -1;
    m->pos = base + offset;
    return 0;
}

static long ZCALLBACK mem_close(voidpf opaque, voidpf stream)
{
    (void)opaque;
    (void)stream;
    return 0;
}

static int ZCALLBACK mem_error(voidpf opaque, voidpf stream)
{
    (void)opaque;
    return ((struct memfile *)stream)->error;
}

static void mem_filefunc(zlib_filefunc_def *ff, struct memfile *m)
{
    ff->zopen_file  = mem_open;
    ff->zread_file  = mem_read;
    ff->zwrite_file = mem_write;
    ff->ztell_file  = mem_tell;
    ff->zseek_file  = mem_seek;
    ff->zclose_file = mem_close;
    ff->zerror_file = mem_error;
    ff->opaque      = m;
}

#endif /* MEMFILE_H */
