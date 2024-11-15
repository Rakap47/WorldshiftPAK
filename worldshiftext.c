

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <sys/stat.h>
#include <zlib.h>

#ifdef WIN32
    #include <direct.h>
    #define PATHSLASH   '\\'
    #define make_dir(x) mkdir(x)
#else
    #define stricmp     strcasecmp
    #define PATHSLASH   '/'
    #define make_dir(x) mkdir(x, 0755)
#endif

typedef uint8_t     u8;
typedef uint16_t    u16;
typedef uint32_t    u32;
typedef uint64_t    u64;



#define VER         "0.1.2d"
#define BLOCKSZ     0xce3c
#define MAXSZ       0xffff
#define WS_SIGN     0x78706b66

#ifndef MAX_PATH
    #define MAX_PATH    4096
#endif



u8 *get_ext(u8 *fname);
int check_wildcard(u8 *fname, u8 *wildcard);
u8 *ws_extract(FILE *fd, u8 *fdata, u8 *fdata_limit, int entries, u8 *name, int nameoff);
int ws_fseek(FILE *stream, int offset, int origin);
int ws_fread(void *ptr, int size, FILE *stream);
void check_overwrite(u8 *fname);
void ws_dumpa(FILE *fd, u8 *fname, int offset, int fsize);
void myfr(FILE *fd, void *data, unsigned size);
int unzip(u8 *in, int insz, u8 *out, int outsz);
void ws_decrypt(u8 *data, int datasz, int offset);
void std_err(void);



z_stream    *z          = NULL;
int     ws_rem          = 0,
        ws_offset       = 0,
        ws_listonly     = 0;
u16     ws_tot_idx      = 0,
        *ws_blocks      = NULL;
u8      *ws_wildcard    = NULL;



#pragma pack(2)
typedef struct {
    u32     sign;
    u32     offset;
    u32     files;
} ws_head_t;
typedef struct {
    u32     type;
    u32     fsize;
    u64     ftime;
    u32     offset;
    u16     dirnum;
} ws_file_t;
#pragma pack()



int main(int argc, char *argv[]) {
    ws_head_t   ws_head;
    FILE    *fd;
    u32     tmpsign,
            fsize;
    int     i,
            fdatasz;
    u8      name[MAX_PATH + 1],
            *fname,
            *fdir,
            *fdata;

    setbuf(stdout, NULL);

    fputs("\n"
        "WorldShift XE/XP files extractor "VER"\n"
        "by Luigi Auriemma\n"
        "e-mail: aluigi@autistici.org\n"
        "web:    aluigi.org\n"
        "\n", stdout);

    if(argc < 3) {
        printf("\n"
            "Usage: %s [options] <input.XE/XP> <output_folder>\n"
            "\n"
            "Options:\n"
            "-l      list files without extracting them\n"
            "-f \"W\"  extract only the files which match the wildcard W like \"*.dds\"\n"
            "\n", argv[0]);
        exit(1);
    }

    argc -= 2;
    for(i = 1; i < argc; i++) {
        if(((argv[i][0] != '-') && (argv[i][0] != '/')) || (strlen(argv[i]) != 2)) {
            printf("\nError: wrong argument (%s)\n", argv[i]);
            exit(1);
        }
        switch(argv[i][1]) {
            case 'l': ws_listonly   = 1;            break;
            case 'f': ws_wildcard   = argv[++i];    break;
            default: {
                printf("\nError: wrong argument (%s)\n", argv[i]);
                exit(1);
            }
        }
    }
    fname = argv[argc];
    fdir  = argv[argc + 1];

    printf("- open %s\n", fname);
    fd = fopen(fname, "rb");
    if(!fd) std_err();

    if(!ws_listonly) {
        printf("- set output folder %s\n", fdir);
        if(chdir(fdir) < 0) std_err();
    }

    fread(&tmpsign, 1, 4, fd);
    if(tmpsign == WS_SIGN) {
        printf("- type XP\n");
        fseek(fd, 0, SEEK_END);
        fsize = ftell(fd);
    } else {
        printf("- type XE\n");
        z = malloc(sizeof(z_stream));
        if(!z) std_err();
        z->zalloc = (alloc_func)0;
        z->zfree  = (free_func)0;
        z->opaque = (voidpf)0;
        if(inflateInit2(z, 15) != Z_OK) {
            printf("\nError: zlib initialization error\n");
            exit(1);
        }

        if(fseek(fd, -2, SEEK_END)) std_err();
        myfr(fd, &ws_tot_idx, 2);
        ws_blocks = malloc(ws_tot_idx * 2);
        if(!ws_blocks) std_err();

        if(fseek(fd, -((ws_tot_idx * 2) + 2), SEEK_END)) std_err();
        myfr(fd, ws_blocks, ws_tot_idx * 2);
        fsize = BLOCKSZ * ws_tot_idx;
    }

    ws_fseek(fd, 0, SEEK_SET);
    ws_fread(&ws_head, sizeof(ws_head_t), fd);
    if(ws_head.sign != WS_SIGN) {
        printf("\nError: wrong signature (%08x)\n", tmpsign);
        exit(1);
    }

    fdatasz = fsize - ws_head.offset;
    fdata = malloc(fdatasz);
    if(!fdata) std_err();

    ws_fseek(fd, ws_head.offset, SEEK_SET);
    fdatasz = ws_fread(fdata, fdatasz, fd);

    printf("\n"
        "  filesize   filename\n"
        "---------------------\n");

    name[0] = 0;
    ws_extract(fd, fdata, fdata + fdatasz, ws_head.files, name, 0);

    fclose(fd);
    if(z) inflateEnd(z);
    printf("\n- %u files in the archive\n", ws_head.files);
    return(0);
}



u8 *get_ext(u8 *fname) {
    u8      *p;

    p = strrchr(fname, '.');
    if(!p) return("");
    return(p + 1);
}



void create_dir(u8 *name) {
    struct  stat    xstat;
    u8      *p,
            *l;

        // removed any security check since the files are ever officials
    if(!stat(name, &xstat)) return;

    for(p = name;; p = l + 1) {
        l = strchr(p, '\\');
        if(!l) l = strchr(p, '/');
        if(!l) break;
        *l = 0;
        make_dir(name);
        *l = PATHSLASH;
    }
}



int check_wildcard(u8 *fname, u8 *wildcard) {
    u8      *f,
            *w,
            *a;

    if(!wildcard) return(0);
    f = fname;
    w = wildcard;
    a = NULL;
    while(*f || *w) {
        if(!*w && !a) return(-1);
        if(*w == '?') {
            if(!*f) break;
            w++;
            f++;
        } else if(*w == '*') {
            w++;
            a = w;
        } else {
            if(!*f) break;
            if(tolower(*f) != tolower(*w)) {
                if(!a) return(-1);
                f++;
                w = a;
            } else {
                f++;
                w++;
            }
        }
    }
    if(*f || *w) return(-1);
    return(0);
}



u8 *ws_extract(FILE *fd, u8 *fdata, u8 *fdata_limit, int entries, u8 *name, int nameoff) {
    ws_file_t   *ws_file;
    int     i,
            len;
    u8      *p;

    p = fdata;
    for(i = 0; i < entries; i++) {
        if(p >= fdata_limit) break;
        len = *(u16 *)p;                p += 2;
        if((nameoff + len) > MAX_PATH) {
            printf("\n"
                "Error: the name of the current file is too long:\n"
                "       %.*s\n", len, p);
            exit(1);
        }
        if(len) {
            if(nameoff && (name[nameoff - 1] != PATHSLASH)) name[nameoff++] = PATHSLASH;
            memcpy(name + nameoff, p, len);
            name[nameoff + len] = 0;    p += len;
        }
        ws_file = (void *)p;            p += sizeof(ws_file_t);

        if(ws_file->type & 0x10) {
            p = ws_extract(fd, p, fdata_limit, ws_file->dirnum, name, nameoff + len);
        } else if(ws_file->type & 0x20) {
            ws_dumpa(fd, name, ws_file->offset, ws_file->fsize);
        } else {
            printf("\nError: unknown file type (0x%x)\n", ws_file->type);
            exit(1);
        }
        name[nameoff] = 0;
    }
    return(p);
}



int ws_fseek(FILE *stream, int offset, int origin) {
    int     i,
            idx,
            off = 0;

    if(!z) return(fseek(stream, offset, origin));

    idx = offset / BLOCKSZ;
    if(idx > ws_tot_idx) return(-1);
    for(i = 0; i < idx; i++) {
        off += ws_blocks[i];
    }
    ws_rem    = offset % BLOCKSZ;
    ws_offset = offset;
    return(fseek(stream, off, origin));
}



int ws_fread(void *ptr, int size, FILE *stream) {
    int     i,
            idx,
            len,
            currsz;
    static  u8  *in     = NULL,
                *out    = NULL;

    if(!z) {
        myfr(stream, ptr, size);
        return(size);
    }

    if(!in || !out) {
        in  = malloc(MAXSZ);
        out = malloc(MAXSZ);   // BLOCKSZ
        if(!in || !out) std_err();
    }

    ws_fseek(stream, ws_offset, SEEK_SET);

    currsz = 0;
    for(idx = ws_offset / BLOCKSZ; idx < ws_tot_idx; idx++) {
        if(currsz >= size) break;
        myfr(stream, in, ws_blocks[idx]);
        len = unzip(in, ws_blocks[idx], out, MAXSZ);
        if(ws_rem) {
            if(ws_rem > len) {
                ws_rem -= len;
                continue;
            }
            len -= ws_rem;
            //memmove(out, out + ws_rem, len);
            for(i = 0; i < len; i++) {
                out[i] = out[ws_rem + i];
            }
            ws_rem = 0;
        }
        currsz += len;
        if(currsz > size) {
            len -= (currsz - size);
            currsz = size;
        }
        memcpy(ptr, out, len);
        ptr += len;
        if(currsz >= size) break;
    }
    ws_offset += currsz;
    ws_rem    = ws_offset % BLOCKSZ;
    return(currsz);
}



void check_overwrite(u8 *fname) {
    FILE    *fd;
    u8      ans[16];

    fd = fopen(fname, "rb");
    if(!fd) return;
    fclose(fd);
    printf("  the file already exists, do you want to overwrite it (y/N)? ");
    fgets(ans, sizeof(ans), stdin);
    if(tolower(ans[0]) != 'y') exit(1);
}



void ws_dumpa(FILE *fd, u8 *fname, int offset, int fsize) {
    FILE    *fdo;
    int     len;
    static u8   *buff = NULL;

    if(check_wildcard(fname, ws_wildcard) < 0) return;
    printf("  %-10u %s\n", fsize, fname);
    if(ws_listonly) return;

    check_overwrite(fname);
    create_dir(fname);
    fdo = fopen(fname, "wb");
    if(!fdo) std_err();

    if(!buff) {
        buff = malloc(MAXSZ);
        if(!buff) std_err();
    }

    ws_fseek(fd, offset, SEEK_SET);
    for(len = MAXSZ; fsize; fsize -= len) {
        if(len > fsize) len = fsize;
        if(ws_fread(buff, len, fd) != len) break;
        fwrite(buff, 1, len, fdo);
    }
    fclose(fdo);
}



void myfr(FILE *fd, void *data, unsigned size) {
    u32     offset;

    offset = ftell(fd);
    if(fread(data, 1, size, fd) != size) {
        printf("\nError: incomplete input file, can't read %u bytes\n", size);
        exit(1);
    }
    ws_decrypt(data, size, offset);
}



int unzip(u8 *in, int insz, u8 *out, int outsz) {
    inflateReset(z);

    z->next_in   = in;
    z->avail_in  = insz;
    z->next_out  = out;
    z->avail_out = outsz;
    if(inflate(z, Z_SYNC_FLUSH) != Z_STREAM_END) {
        printf("\nError: the compressed input is wrong or incomplete\n");
        exit(1);
    }
    return(z->total_out);
}



void ws_decrypt(u8 *data, int datasz, int offset) {
    int     i;
    static const u8 key[] =
        "\x46\x69\x6C\x65\xFE\x4E\x61\x6D\x65\x09\x0D\x0A\x46\x69\x6C\x65"
        "\x50\x6F\x73\x09\x0D\x0A\x31\x0D\x09\x0A\x02\x21\x2A\x31\x31\x09"
        "\x46\x69\x6C\x65\x53\x69\x7A\x65\x0D\x0A\x48\x68\x31\x01\x8E\x9E"
        "\xAC\xBC\xDC\x98\xF1\xE1";

    if(!z) return;
    for(i = 0; i < datasz; i++) {
        data[i] -= key[(offset + i) % 0x36];
    }
}



void std_err(void) {
    perror("\nError");
    exit(1);
}


