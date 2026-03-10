/* zip.c -- IO on .zip files using zlib 
   Version 0.21, March 10th, 2003

   Read zip.h for more info
*/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "zlib.h"
#include "zip.h"

#ifdef STDC
#  include <stddef.h>
#  include <string.h>
#  include <stdlib.h>
#endif
#ifdef NO_ERRNO_H
    extern int errno;
#else
#   include <errno.h>
#endif

#include "time64.h"
extern unsigned char *ebc2asc;

#ifndef local
#  define local static
#endif
/* compile with -Dlocal if your debugger can't find static symbols */

#ifndef VERSIONMADEBY
# define VERSIONMADEBY   (0x0) /* platform depedent */
#endif

#ifndef Z_BUFSIZE
#define Z_BUFSIZE (16384)
#endif

#ifndef Z_MAXFILENAMEINZIP
#define Z_MAXFILENAMEINZIP (256)
#endif

#ifndef ALLOC
# define ALLOC(size) (malloc(size))
#endif
#ifndef TRYFREE
# define TRYFREE(p) {if (p) free(p);}
#endif

#ifndef DEF_MEM_LEVEL
#if MAX_MEM_LEVEL >= 8
#  define DEF_MEM_LEVEL 8
#else
#  define DEF_MEM_LEVEL  MAX_MEM_LEVEL
#endif
#endif

local zipFile tempOpen (const char *pathname, int append);
local zipFile tempOpen2(const char *pathname, int append, 
    zipcharpc *globalcomment, zlib_filefunc_def *pzlib_filefunc_def);

local int tempClose(zipFile file);

local int tempOpenNewFileInZip(zipFile file, int level);

local int tempWriteInFileInZip(zipFile file, const void *buf, unsigned len);

local int tempCloseFileInZip(zipFile file);
local int tempCloseFileInZipRaw(zipFile file, uLong *crc32, uLong *uncompressed_size, uLong *compressed_size);

local int tempZip(const char *dataset, uLong *crc32, uLong *uncompressed_size, uLong *compressed_size);

local int tempZipBin(const char *indataset, const char *tempdataset, uLong *crc32, uLong *uncompressed_size, uLong *compressed_size);

#define SIZEDATA_INDATABLOCK (4096-(4*4))

#define LOCALHEADERMAGIC    (0x04034b50)
#define CENTRALHEADERMAGIC  (0x02014b50)
#define ENDHEADERMAGIC      (0x06054b50)

#define FLAG_LOCALHEADER_OFFSET (0x06)
#define CRC_LOCALHEADER_OFFSET  (0x0e)

#define SIZECENTRALHEADER (0x2e) /* 46 */

typedef struct linkedlist_datablock_internal_s
{
  struct linkedlist_datablock_internal_s* next_datablock;
  uLong  avail_in_this_block;
  uLong  filled_in_this_block;
  uLong  unused; /* for future use and alignement */
  unsigned char data[SIZEDATA_INDATABLOCK];
} linkedlist_datablock_internal;

typedef struct linkedlist_data_s
{
    linkedlist_datablock_internal* first_block;
    linkedlist_datablock_internal* last_block;
} linkedlist_data;


typedef struct
{
	z_stream stream;            /* zLib stream structure for inflate */
    int  stream_initialised;    /* 1 is stream is initialised */
    uInt pos_in_buffered_data;  /* last written byte in buffered_data */

    uLong pos_local_header;     /* offset of the local header of the file 
                                     currenty writing */
    char* central_header;       /* central header data for the current file */
    uLong size_centralheader;   /* size of the central header for cur file */
    uLong flag;                 /* flag of the file currently writing */

    int  method;                /* compression method of file currenty wr.*/
    int  raw;                   /* 1 for directly writing raw data */
    Byte buffered_data[Z_BUFSIZE];/* buffer contain compressed data to be writ*/
    uLong dosDate;
    uLong crc32;
} curfile_info;

typedef struct
{
    zlib_filefunc_def z_filefunc;
	voidpf filestream;        /* io structore of the zipfile */
    linkedlist_data central_dir;/* datablock with central dir in construction*/
    int  in_opened_file_inzip;  /* 1 if a file in the zip is currently writ.*/
    curfile_info ci;            /* info on the file curretly writing */

    uLong begin_pos;            /* position of the beginning of the zipfile */
    uLong number_entry;
} zip_internal;

local linkedlist_datablock_internal* allocate_new_datablock()
{
    linkedlist_datablock_internal* ldi;
    ldi = (linkedlist_datablock_internal*)
                 ALLOC(sizeof(linkedlist_datablock_internal));
    if (ldi!=NULL)
    {
        ldi->next_datablock = NULL ;
        ldi->filled_in_this_block = 0 ;
        ldi->avail_in_this_block = SIZEDATA_INDATABLOCK ;
    }
    return ldi;
}

local void free_datablock(ldi)
    linkedlist_datablock_internal* ldi;
{
    while (ldi!=NULL)
    {
        linkedlist_datablock_internal* ldinext = ldi->next_datablock;
        TRYFREE(ldi);
        ldi = ldinext;
    }
}

local void init_linkedlist(ll)
    linkedlist_data* ll;
{
    ll->first_block = ll->last_block = NULL;
}

local void free_linkedlist(ll)
    linkedlist_data* ll;
{
    free_datablock(ll->first_block);
    ll->first_block = ll->last_block = NULL;
}


local int add_data_in_datablock(ll,buf,len)
    linkedlist_data* ll;    
    const void* buf;
    uLong len;
{
    linkedlist_datablock_internal* ldi;
    const unsigned char* from_copy;

    if (ll==NULL)
        return ZIP_INTERNALERROR;

    if (ll->last_block == NULL)
    {
        ll->first_block = ll->last_block = allocate_new_datablock();
        if (ll->first_block == NULL)
            return ZIP_INTERNALERROR;
    }

    ldi = ll->last_block;
    from_copy = (unsigned char*)buf;

    while (len>0)
    {
        uInt copy_this;
        uInt i;
        unsigned char* to_copy;

        if (ldi->avail_in_this_block==0)
        {
            ldi->next_datablock = allocate_new_datablock();
            if (ldi->next_datablock == NULL)
                return ZIP_INTERNALERROR;
            ldi = ldi->next_datablock ;
            ll->last_block = ldi;
        }

        if (ldi->avail_in_this_block < len)
            copy_this = (uInt)ldi->avail_in_this_block;
        else
            copy_this = (uInt)len;

        to_copy = &(ldi->data[ldi->filled_in_this_block]);

        for (i=0;i<copy_this;i++)
            *(to_copy+i)=*(from_copy+i);

        ldi->filled_in_this_block += copy_this;
        ldi->avail_in_this_block -= copy_this;
        from_copy += copy_this ;
        len -= copy_this;
    }
    return ZIP_OK;
}



/****************************************************************************/

/* ===========================================================================
   Outputs a long in LSB order to the given file
   nbByte == 1, 2 or 4 (byte, short or long)
*/

local int ziplocal_putValue OF((const zlib_filefunc_def* pzlib_filefunc_def,
                                voidpf filestream, uLong x, int nbByte));
local int ziplocal_putValue (pzlib_filefunc_def, filestream, x, nbByte)
    const zlib_filefunc_def* pzlib_filefunc_def;
    voidpf filestream;
    uLong x;
    int nbByte;
{
    unsigned char buf[4];
    int n;
    for (n = 0; n < nbByte; n++) {
        buf[n] = (unsigned char)(x & 0xff);
        x >>= 8;
    }
    if (ZWRITE(*pzlib_filefunc_def,filestream,buf,nbByte)!=(uLong)nbByte)
        return ZIP_ERRNO;
    else
        return ZIP_OK;
}

local void ziplocal_putValue_inmemory OF((void* dest, uLong x, int nbByte));
local void ziplocal_putValue_inmemory (dest, x, nbByte)
    void* dest;
    uLong x;
    int nbByte;
{
    unsigned char* buf=(unsigned char*)dest;
    int n;
    for (n = 0; n < nbByte; n++) {
        buf[n] = (unsigned char)(x & 0xff);
        x >>= 8;
    }
}
/****************************************************************************/


local uLong ziplocal_TmzDateToDosDate(ptm,dosDate)
    const tm_zip* ptm;
    uLong dosDate;
{
    uLong year = (uLong)ptm->tm_year;
    if (year>1980)
        year-=1980;
    else if (year>80)
        year-=80;
    return
      (uLong) (((ptm->tm_mday) + (32 * (ptm->tm_mon+1)) + (512 * year)) << 16) |
        ((ptm->tm_sec/2) + (32* ptm->tm_min) + (2048 * (uLong)ptm->tm_hour));
}

local uLong tmToDosDate(struct tm *tm)
{
    uLong year      = (uLong)tm->tm_year;
    uLong dosDate   = -1;

    if (year>1980) {
        year-=1980;
    }
    else if (year>80) {
        year-=80;
    }
    
    dosDate = (uLong) (
        ((tm->tm_mday)          + 
         (32 * (tm->tm_mon+1))  + 
         (512 * year))          << 16
        ) |
        ((tm->tm_sec/2) + 
         (32 * tm->tm_min) + 
         (2048 * (uLong)tm->tm_hour));
    
    return dosDate;
} 

local uLong nowDosDate(void)
{
    time64_t    now;
    struct tm   *tm;
    uLong       dosDate  = -1;
    
    time64(&now);
    tm = localtime64(&now);
    
    if (tm) dosDate = tmToDosDate(tm);

    return dosDate;
}

/****************************************************************************/

extern int ZEXPORT 
zip_add_dataset_bin(zipFile             file, 
                    const char          *dataset,
                    const char          *filename)
{
    int             err                     = ZIP_OK;
    zip_fileinfo    zipfi                   = {0};
    const void      *extrafield_local       = NULL; 
    uInt            size_extrafield_local   = 0;
    const void      *extrafield_global      = NULL;
    uInt            size_extrafield_global  = 0;
    char            comment[20]             = {0};
    int             method                  = Z_DEFLATED;
    int             level                   = Z_DEFAULT_COMPRESSION;
    int             raw                     = 0;
    FILE            *fp                     = NULL;
    char            *buf                    = NULL;
    int             buf_size                = 0;
    int             size_read               = 0;
    uLong           crc32                   = 0;
    uLong           uncompressed_size       = 0;
    uLong           compressed_size         = 0;
    char            tempdd[12]              = "DD:";
    char            ascii_filename[256]     = {0};
    char            ascii_comment[20]       = {0};

    zip_internal    *zi                     = NULL;
    uInt            size_filename           = 0;
    uInt            size_comment            = 0;
    uInt            i;

#if 1   /* debugging */
    wtof("%s: Enter", __func__);
    wtof("%s:   file                    %p", __func__, file);
    wtof("%s:   dataset                 %s", __func__, dataset);
    wtof("%s:   filename                %s", __func__, filename);
#endif

    zi = (zip_internal*)file;
    if (!zi) {
        err = ZIP_PARAMERROR;
        goto quit;
    }

    if (!dataset) {
        err = ZIP_PARAMERROR;
        goto quit;
    }

    /* allocate a temp zip dataset */
    err = __dsalc(&tempdd[3], "dsn=&&tempzip,dsorg=ps,recfm=fb,"
                              "lrecl=80,blksize=27920,space=cyl(10,10)");
    if (err) {
        wtof("%s: Unable to allocate &&TEMPZIP dataset, err=%d", __func__, err);
        goto quit;
    }

    /* open the dataset for reading */
    fp = fopen(dataset, "rb");
    if (!fp) {
        err = ZIP_ERRNO;
        goto quit;
    }
    
    /* save dataset info as comment string */
    strcpy(comment, "E,");
    if ((fp->recfm & _FILE_RECFM_TYPE) == _FILE_RECFM_U) {
        strcat(comment, "U");
    }
    else if ((fp->recfm & _FILE_RECFM_TYPE) == _FILE_RECFM_V) {
        strcat(comment, "V");
    }
    else if ((fp->recfm & _FILE_RECFM_TYPE) == _FILE_RECFM_F) {
        strcat(comment, "F");
    }
    
    if (fp->recfm & _FILE_RECFM_B) {
        strcat(comment, "B");
    }
    
    i = strlen(comment);
    sprintf(&comment[i], "%u", fp->lrecl);
    
    fclose(fp);
    fp = NULL;

    size_comment = strlen(comment);
    wtof("%s: size_comment  = %u", __func__, size_comment);
    wtof("%s: comment       = \"%s\"", __func__, comment);

    /* make ASCII copy of comment */
    for(i=0; i < size_comment; i++) {
        ascii_comment[i] = ebc2asc[comment[i]];
    }
    wtodumpf(ascii_comment, size_comment, "%s: ASCII comment", __func__);
    
    if (zi->in_opened_file_inzip == 1) {
        wtof("%s: calling zipCloseFileInZip()", __func__);
        err = zipCloseFileInZip (file);
        if (err != ZIP_OK) {
            goto quit;
        }
    }

    /* the filename is the "name" we put in the zip directory */
    if (!filename)  filename = dataset;

    size_filename = strlen(filename);
    wtof("%s: size_filename = %u", __func__, size_filename);
    wtof("%s: filename      = \"%s\"", __func__, filename);

    /* make ASCII copy of filename */
    for(i=0; i < size_filename; i++) {
        ascii_filename[i] = ebc2asc[filename[i]];
    }
    wtodumpf(ascii_filename, size_filename, "%s ASCII filename", __func__);

    /* Use the current date and time as the dos date */
    zi->ci.dosDate = nowDosDate();

    zi->ci.flag = 0;
    if ((level==8) || (level==9))   zi->ci.flag |= 2;
    if ((level==2))                 zi->ci.flag |= 4;
    if ((level==1))                 zi->ci.flag |= 6;

    /* We want deflate the input dataset to a temp dataset before
     * we write anything to the zip file dataset so that we can
     * calculate and obtain the crc, compressed size and uncompresses
     * size values.
     */
    err = tempZipBin(dataset, tempdd, &crc32, &uncompressed_size, &compressed_size);
    if (err != ZIP_OK) {
        wtof("%s: tempZipBin() err=%d", __func__, err);
        goto quit;
    }


    zi->ci.crc32 = crc32;
    zi->ci.method = Z_DEFLATED;
    zi->ci.stream_initialised = 0;
    zi->ci.pos_in_buffered_data = 0;
    zi->ci.raw = raw;
    zi->ci.pos_local_header = ZTELL(zi->z_filefunc,zi->filestream);
    wtof("%s: zi->ci.pos_local_header = %u", __func__, zi->ci.pos_local_header);

    zi->ci.size_centralheader = SIZECENTRALHEADER + size_filename + 
                                      size_extrafield_global + size_comment;
    zi->ci.central_header = (char*)ALLOC((uInt)zi->ci.size_centralheader);
    wtof("%s: zi->ci.central_header = %p", __func__, zi->ci.central_header);

    ziplocal_putValue_inmemory(zi->ci.central_header,(uLong)CENTRALHEADERMAGIC,4);
    /* version info */
    ziplocal_putValue_inmemory(zi->ci.central_header+4,(uLong)VERSIONMADEBY,2);
    ziplocal_putValue_inmemory(zi->ci.central_header+6,(uLong)20,2);
    ziplocal_putValue_inmemory(zi->ci.central_header+8,(uLong)zi->ci.flag,2);
    ziplocal_putValue_inmemory(zi->ci.central_header+10,(uLong)zi->ci.method,2);
    ziplocal_putValue_inmemory(zi->ci.central_header+12,(uLong)zi->ci.dosDate,4);
    ziplocal_putValue_inmemory(zi->ci.central_header+16,(uLong)crc32,4); /*crc*/
    ziplocal_putValue_inmemory(zi->ci.central_header+20,(uLong)compressed_size,4); /*compr size*/
    ziplocal_putValue_inmemory(zi->ci.central_header+24,(uLong)uncompressed_size,4); /*uncompr size*/
    ziplocal_putValue_inmemory(zi->ci.central_header+28,(uLong)size_filename,2);
    ziplocal_putValue_inmemory(zi->ci.central_header+30,(uLong)size_extrafield_global,2);
    ziplocal_putValue_inmemory(zi->ci.central_header+32,(uLong)size_comment,2);
    ziplocal_putValue_inmemory(zi->ci.central_header+34,(uLong)0,2); /*disk nm start*/

    ziplocal_putValue_inmemory(zi->ci.central_header+36,(uLong)0,2); /* internal file attributes */
    ziplocal_putValue_inmemory(zi->ci.central_header+38,(uLong)0,4); /* external file attributes */

    ziplocal_putValue_inmemory(zi->ci.central_header+42,(uLong)zi->ci.pos_local_header,4);

    wtodumpf(ascii_filename, size_filename, "%s: ASCII filename 2", __func__);
    for (i=0;i<size_filename;i++)
        *(zi->ci.central_header+SIZECENTRALHEADER+i) = ascii_filename[i];

    for (i=0;i<size_extrafield_global;i++)
        *(zi->ci.central_header+SIZECENTRALHEADER+size_filename+i) =
              *(((const char*)extrafield_global)+i);

    wtodumpf(ascii_comment, size_comment, "%s: ASCII comment 2", __func__);
    for (i=0;i<size_comment;i++)
        *(zi->ci.central_header+SIZECENTRALHEADER+size_filename+
              size_extrafield_global+i) = ascii_comment[i];

    if (zi->ci.central_header == NULL) {
        err = ZIP_INTERNALERROR;
        goto quit;
    }

    wtof("%s: Writing local header", __func__);

    /* write the local header */
    err = ziplocal_putValue(&zi->z_filefunc,zi->filestream,(uLong)LOCALHEADERMAGIC,4);

    if (err==ZIP_OK)
        err = ziplocal_putValue(&zi->z_filefunc,zi->filestream,(uLong)20,2);/* version needed to extract */
    if (err==ZIP_OK)
        err = ziplocal_putValue(&zi->z_filefunc,zi->filestream,(uLong)zi->ci.flag,2);

    if (err==ZIP_OK)
        err = ziplocal_putValue(&zi->z_filefunc,zi->filestream,(uLong)zi->ci.method,2);

    if (err==ZIP_OK)
        err = ziplocal_putValue(&zi->z_filefunc,zi->filestream,(uLong)zi->ci.dosDate,4);

    if (err==ZIP_OK)
        err = ziplocal_putValue(&zi->z_filefunc,zi->filestream,crc32,4); /* crc 32 */
    if (err==ZIP_OK)
        err = ziplocal_putValue(&zi->z_filefunc,zi->filestream,compressed_size,4); /* compressed size */
    if (err==ZIP_OK)
        err = ziplocal_putValue(&zi->z_filefunc,zi->filestream,uncompressed_size,4); /* uncompressed size */

    if (err==ZIP_OK)
        err = ziplocal_putValue(&zi->z_filefunc,zi->filestream,(uLong)size_filename,2);

    if (err==ZIP_OK)
        err = ziplocal_putValue(&zi->z_filefunc,zi->filestream,(uLong)size_extrafield_local,2);

    wtof("%s: ziplocal_putValue() err=%d", __func__, err);

    if ((err==ZIP_OK) && (size_filename>0)) {
        if (ZWRITE(zi->z_filefunc, zi->filestream, ascii_filename, size_filename) != size_filename) {
            err = ZIP_ERRNO;
            wtof("%s: ZWRITE 1 err=%d", __func__, err);
        }
    }
    
    if ((err==ZIP_OK) && (size_extrafield_local>0)) {
        if (ZWRITE(zi->z_filefunc,zi->filestream,extrafield_local,size_extrafield_local)!=size_extrafield_local) {
            err = ZIP_ERRNO;
            wtof("%s: ZWRITE 2 err=%d", __func__, err);
        }
    }
    
    zi->ci.stream.avail_in = (uInt)0;
    zi->ci.stream.avail_out = (uInt)Z_BUFSIZE;
    zi->ci.stream.next_out = zi->ci.buffered_data;
    zi->ci.stream.total_in = 0;
    zi->ci.stream.total_out = 0;

    /* copy the temp dataset into the zip file */
    fp = fopen(tempdd, "rb");
    if (!fp) {
        wtof("%s: Unable to open temp dataset %s for input", __func__, tempdd);
        err = ZIP_INTERNALERROR;
        goto quit;
    }

    /* allocate a buffer for reading the dataset */
    buf_size = fp->blksize;
    buf = calloc(1, buf_size + 8);
    if (!buf) {
        wtof("%s: Unable to allocate buffer %u bytes.", __func__, buf_size + 8);
        err = ZIP_INTERNALERROR;
        goto quit;
    }

    /* copy tempdd dataset (defalted) into our zip file */
    do {
        err = ZIP_OK;
        size_read = (int)fread(buf, 1, buf_size, fp);
        // wtof("%s: fread() size_read=%d", __func__, size_read);
        if (size_read < buf_size) {
            if (feof(fp)==0) {
                wtof("%s: error in reading %s", __func__, tempdd);
                err = ZIP_ERRNO;
            }
        }

        if (size_read>0) {
            if (ZWRITE(zi->z_filefunc, zi->filestream, buf, size_read) != size_read) {
                err = ZIP_ERRNO;
                wtof("%s: ZWRITE 2 err=%d", __func__, err);
            }
        }
    } while ((err == ZIP_OK) && (size_read>0));

    zi->in_opened_file_inzip = 0;

quit:
    if (buf)        free(buf);
    if (fp)         fclose(fp);
    if (tempdd[3])  __dsfree(&tempdd[3]);

    wtof("%s: Exit err=%d", __func__, err);
    return err;
}

local int 
tempWriteInFileInZip(zipFile file, const void *buf, unsigned len)
{
    zip_internal* zi;
    int err=ZIP_OK;

    if (file == NULL)
        return ZIP_PARAMERROR;

    zi = (zip_internal*)file;

    if (zi->in_opened_file_inzip == 0)
        return ZIP_PARAMERROR;

    zi->ci.stream.next_in = (void*)buf;
    zi->ci.stream.avail_in = len;
    zi->ci.crc32 = crc32(zi->ci.crc32, buf, len);

    while ((err==ZIP_OK) && (zi->ci.stream.avail_in>0)) {
        if (zi->ci.stream.avail_out == 0) {
            if (ZWRITE(zi->z_filefunc,zi->filestream,zi->ci.buffered_data,zi->ci.pos_in_buffered_data) !=zi->ci.pos_in_buffered_data) {
                err = ZIP_ERRNO;
            }
            zi->ci.pos_in_buffered_data = 0;
            zi->ci.stream.avail_out = (uInt)Z_BUFSIZE;
            zi->ci.stream.next_out = zi->ci.buffered_data;
        }

        if(err != ZIP_OK) {
            break;
        }

        if (zi->ci.method == Z_DEFLATED) {
            uLong uTotalOutBefore = zi->ci.stream.total_out;
            
            err=deflate(&zi->ci.stream,  Z_NO_FLUSH);
            
            zi->ci.pos_in_buffered_data += (uInt)(zi->ci.stream.total_out - uTotalOutBefore) ;

        }
        else {
            uInt copy_this,i;

            if (zi->ci.stream.avail_in < zi->ci.stream.avail_out) {
                copy_this = zi->ci.stream.avail_in;
            }
            else {
                copy_this = zi->ci.stream.avail_out;
            }
            
            for (i=0;i<copy_this;i++) {
                *(((char*)zi->ci.stream.next_out)+i) = *(((const char*)zi->ci.stream.next_in)+i);
            }

            zi->ci.stream.avail_in -= copy_this;
            zi->ci.stream.avail_out-= copy_this;
            zi->ci.stream.next_in+= copy_this;
            zi->ci.stream.next_out+= copy_this;
            zi->ci.stream.total_in+= copy_this;
            zi->ci.stream.total_out+= copy_this;
            zi->ci.pos_in_buffered_data += copy_this;
        }
    }

    return err;
}

local int 
tempCloseFileInZipRaw(zipFile file, uLong *crc32, uLong *uncompressed_size, uLong *compressed_size)
{
    zip_internal* zi;
    int err=ZIP_OK;

    if (file == NULL)
        return ZIP_PARAMERROR;

    zi = (zip_internal*)file;

    if (zi->in_opened_file_inzip == 0)    
        return ZIP_PARAMERROR;

    zi->ci.stream.avail_in = 0;
    
    if ((zi->ci.method == Z_DEFLATED) && (!zi->ci.raw)) while (err==ZIP_OK) {
        uLong uTotalOutBefore;

        if (zi->ci.stream.avail_out == 0) {
            if (ZWRITE(zi->z_filefunc,zi->filestream,zi->ci.buffered_data,zi->ci.pos_in_buffered_data) !=zi->ci.pos_in_buffered_data) {
                err = ZIP_ERRNO;
            }
            zi->ci.pos_in_buffered_data = 0;
            zi->ci.stream.avail_out = (uInt)Z_BUFSIZE;
            zi->ci.stream.next_out = zi->ci.buffered_data;
        }

        uTotalOutBefore = zi->ci.stream.total_out;
        err=deflate(&zi->ci.stream,  Z_FINISH);
        zi->ci.pos_in_buffered_data += (uInt)(zi->ci.stream.total_out - uTotalOutBefore) ;
    }

    if (err==Z_STREAM_END) {
        err=ZIP_OK; /* this is normal */
    }

    if ((zi->ci.pos_in_buffered_data>0) && (err==ZIP_OK)) {
        if (ZWRITE(zi->z_filefunc,zi->filestream,zi->ci.buffered_data,zi->ci.pos_in_buffered_data) !=zi->ci.pos_in_buffered_data) {
            err = ZIP_ERRNO;
        }
    }
    
    if (zi->ci.method == Z_DEFLATED) {
        err=deflateEnd(&zi->ci.stream);
        zi->ci.stream_initialised = 0;
    }

    if (crc32) *crc32 = (uLong)zi->ci.crc32;
    if (uncompressed_size) *uncompressed_size = (uLong)zi->ci.stream.total_in;
    if (compressed_size) *compressed_size =(uLong)zi->ci.stream.total_out;



#if 0 /* not needed for our temp zip file */
    ziplocal_putValue_inmemory(zi->ci.central_header+16,crc32,4); /*crc*/
    ziplocal_putValue_inmemory(zi->ci.central_header+20,
                                compressed_size,4); /*compr size*/
    if (zi->ci.stream.data_type == Z_ASCII)
        ziplocal_putValue_inmemory(zi->ci.central_header+36,(uLong)Z_ASCII,2); 
    ziplocal_putValue_inmemory(zi->ci.central_header+24,
                                uncompressed_size,4); /*uncompr size*/

    if (err==ZIP_OK)
        err = add_data_in_datablock(&zi->central_dir,zi->ci.central_header,
                                       (uLong)zi->ci.size_centralheader);
    free(zi->ci.central_header);

    if (err==ZIP_OK)
    {
        long cur_pos_inzip = ZTELL(zi->z_filefunc,zi->filestream);
	    if (ZSEEK(zi->z_filefunc,zi->filestream,
                  zi->ci.pos_local_header + 14,ZLIB_FILEFUNC_SEEK_SET)!=0)
		    err = ZIP_ERRNO;

        if (err==ZIP_OK)
            err = ziplocal_putValue(&zi->z_filefunc,zi->filestream,crc32,4); /* crc 32, unknown */

        if (err==ZIP_OK) /* compressed size, unknown */
            err = ziplocal_putValue(&zi->z_filefunc,zi->filestream,compressed_size,4); 

        if (err==ZIP_OK) /* uncompressed size, unknown */
            err = ziplocal_putValue(&zi->z_filefunc,zi->filestream,uncompressed_size,4);

	    if (ZSEEK(zi->z_filefunc,zi->filestream,
                  cur_pos_inzip,ZLIB_FILEFUNC_SEEK_SET)!=0)
		    err = ZIP_ERRNO;
    }
#endif /* not needed for our temp zip file */

    zi->number_entry ++;
    zi->in_opened_file_inzip = 0;

    return err;
}

local int 
tempCloseFileInZip(zipFile file)
{
    return tempCloseFileInZipRaw(file, NULL, NULL, NULL);
}

local zipFile 
tempOpen2(const char *pathname, int append, zipcharpc *globalcomment, zlib_filefunc_def *pzlib_filefunc_def)
{
    zip_internal ziinit;
    zip_internal* zi;

    if (pzlib_filefunc_def==NULL)
        fill_fopen_filefunc(&ziinit.z_filefunc);
    else
        ziinit.z_filefunc = *pzlib_filefunc_def;

    ziinit.filestream = (*(ziinit.z_filefunc.zopen_file))
                 (ziinit.z_filefunc.opaque, 
                  pathname, 
                  (append == 0) ? 
                  (ZLIB_FILEFUNC_MODE_READ | ZLIB_FILEFUNC_MODE_WRITE | ZLIB_FILEFUNC_MODE_CREATE) :
                    (ZLIB_FILEFUNC_MODE_READ | ZLIB_FILEFUNC_MODE_WRITE | ZLIB_FILEFUNC_MODE_EXISTING));

    if (ziinit.filestream == NULL)
        return NULL;
    ziinit.begin_pos = 0;   // ZTELL(ziinit.z_filefunc,ziinit.filestream);
    ziinit.in_opened_file_inzip = 0;
    ziinit.ci.stream_initialised = 0;
    ziinit.number_entry = 0;
    init_linkedlist(&(ziinit.central_dir));

    zi = (zip_internal*)ALLOC(sizeof(zip_internal));
    if (zi==NULL) {
        ZCLOSE(ziinit.z_filefunc,ziinit.filestream);
        return NULL;
    }

    *zi = ziinit;
    return (zipFile)zi;
}

local zipFile 
tempOpen (const char *pathname, int append)
{
    return tempOpen2(pathname,append,NULL,NULL);
}

local int 
tempClose (zipFile file)
{
    zip_internal* zi;
    int err = 0;

    if (file == NULL)
        return ZIP_PARAMERROR;

    zi = (zip_internal*)file;

    if (zi->in_opened_file_inzip == 1) {
        err = tempCloseFileInZip(file);
    }

    free_datablock(zi->central_dir.first_block);

    if (ZCLOSE(zi->z_filefunc,zi->filestream) != 0) {
        if (err == ZIP_OK) {
            err = ZIP_ERRNO;
        }
    }
    
    TRYFREE(zi);

    return err;
}

local int 
tempOpenNewFileInZip(zipFile file, int level)
{
    int             err     = ZIP_OK;
    zip_internal    *zi;

#if 1   /* debugging */
    wtof("%s: Enter", __func__);
    wtof("%s:   file                    %p", __func__, file);
    wtof("%s:   level                   %d", __func__, level);
#endif

    if (file == NULL) {
        err = ZIP_PARAMERROR;
        goto quit;
    }

    zi = (zip_internal*)file;
    // wtodumpf(zi, sizeof(zip_internal), "%s: ZIP_INTERNAL", __func__);

    if (zi->in_opened_file_inzip == 1) {
        wtof("%s: calling zipCloseFileInZip()", __func__);
        err = tempCloseFileInZip(file);
        if (err != ZIP_OK) {
            goto quit;
        }
    }

    zi->ci.dosDate = 0;
    zi->ci.flag = 0;

    if ((level==8) || (level==9))   zi->ci.flag |= 2;
    if ((level==2))                 zi->ci.flag |= 4;
    if ((level==1))                 zi->ci.flag |= 6;

    zi->ci.crc32 = 0;
    zi->ci.method = Z_DEFLATED;
    zi->ci.stream_initialised = 0;
    zi->ci.pos_in_buffered_data = 0;
    zi->ci.raw = 0;
    zi->ci.pos_local_header = 0;    // ZTELL(zi->z_filefunc,zi->filestream);

    zi->ci.stream.avail_in = (uInt)0;
    zi->ci.stream.avail_out = (uInt)Z_BUFSIZE;
    zi->ci.stream.next_out = zi->ci.buffered_data;
    zi->ci.stream.total_in = 0;
    zi->ci.stream.total_out = 0;

    if ((err==ZIP_OK) && (zi->ci.method == Z_DEFLATED)) {
        zi->ci.stream.zalloc = (alloc_func)0;
        zi->ci.stream.zfree = (free_func)0;
        zi->ci.stream.opaque = (voidpf)0;

        err = deflateInit2(&zi->ci.stream, level, Z_DEFLATED, -MAX_WBITS, DEF_MEM_LEVEL, 0);
        wtof("%s: deflateInit2() err=%d", __func__, err);
        if (err==Z_OK) {
            zi->ci.stream_initialised = 1;
            wtof("%s: zi->ci.stream_initialised = 1;", __func__);
        }
    }

    if (err==Z_OK) {
        zi->in_opened_file_inzip = 1;
        wtof("%s: zi->in_opened_file_inzip = 1;", __func__);
    }

quit:
    wtof("%s: Exit err=%d", __func__, err);
    return err;
}

local int 
tempZipBin(const char *indataset, const char *tempdataset, uLong *crc32, uLong *uncompressed_size, uLong *compressed_size)
{
    int         err     = 0;
    FILE        *fp     = NULL;
    zipFile     zf      = NULL;
    char        *buf    = NULL;
    int         buf_size;
    int         size_read;

    wtof("%s: Enter indataset=\"%s\" tempdataset=\"%s\"", __func__, indataset, tempdataset);

    /* open the input dataset for reading in binary mode */
    fp = fopen(indataset, "rb");
    wtof("%s: fopen(\"%s\", \"rb\") fp=%p", __func__, indataset, fp);
    if (!fp) {
        wtof("%s: Unable to open input dataset %s", __func__, indataset);
        err = ZIP_INTERNALERROR ;
        goto quit;
    }

    /* open the temp dataset as a ZIP file */
    zf = tempOpen(tempdataset, 0);
    wtof("%s: tempOpen(\"%s\", 0) zf=%p", __func__, tempdataset, zf);
    if (!zf) {
        wtof("%s: Unable to open temp dataset %s", __func__, tempdataset);
        err = ZIP_INTERNALERROR ;
        goto quit;
    }

    buf_size = fp->blksize;
    buf = calloc(1, buf_size + 8);
    wtof("%s: calloc(1, %d) buf=%p", __func__, buf_size+8, buf);
    if (!buf) {
        wtof("%s: Unable to allocate temp buffer %d", __func__, buf_size+8);
        err = ZIP_INTERNALERROR ;
        goto quit;
    }

    err = tempOpenNewFileInZip(zf, Z_DEFAULT_COMPRESSION);
    wtof("%s: tempOpenNewFileInZip(%p,%d) err=%d", __func__, zf, Z_DEFAULT_COMPRESSION, err);
    if (err != ZIP_OK) {
        wtof("%s: error in opening new file in temp ZIPFILE", __func__);
        goto quit;
    }

    do {
        err = ZIP_OK;
        size_read = (int)fread(buf, 1, buf_size, fp);
        wtof("%s: fread() size_read=%d", __func__, size_read);
        if (size_read < buf_size) {
            if (feof(fp)==0) {
                wtof("%s: error in reading %s", __func__, tempdataset);
                err = ZIP_ERRNO;
            }
        }

        if (size_read>0) {
            err = tempWriteInFileInZip(zf, buf, size_read);
            wtof("%s: tempWriteInFileInZip(%d) rc=%d", __func__, size_read, err);
            if (err<0) {
                wtof("%s: error in writing new file in the temp ZIPFILE", __func__);
            }
        }
    } while ((err == ZIP_OK) && (size_read>0));

    if (err<0) {
        wtof("%s: err = %d", __func__, err);
        err=ZIP_ERRNO;
        wtof("%s: ZIP_ERRNO rc = %d", __func__, err);
    }
    else {                    
        err = tempCloseFileInZipRaw(zf, crc32, uncompressed_size, compressed_size);
        wtof("%s: tempCloseFileInZipRaw() err = %d", __func__, err);
        if (err!=ZIP_OK) {
            wtof("%s: error in closing new file in the temp ZIPFILE", __func__);
        }
    }

quit:
    if (zf) tempClose(zf);
    if (fp) fclose(fp);

    wtof("%s: Exit err=%d", __func__, err);
    return err;
}

