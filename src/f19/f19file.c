/* F-19 resource-file layer — native port of src_start/stfile.c.
 * DOS int21 handles become indexes into an SDL_IOStream table so the
 * int16 handle globals in the ported sources still work. (off,seg)
 * destination pairs resolve through the segment-handle table. */
#include <SDL3/SDL.h>
#include "f19.h"

#define F19_MAX_FILES 32
static SDL_IOStream *f19_files[F19_MAX_FILES];

static int16 f19_fileSlot(SDL_IOStream *io) {
    int i;
    for (i = 1; i < F19_MAX_FILES; i++)
        if (f19_files[i] == NULL) { f19_files[i] = io; return i; }
    return -1;
}
SDL_IOStream *f19_fileIo(int16 h) {   /* resFile handle -> stream (f19stubs) */
    return (h >= 1 && h < F19_MAX_FILES) ? f19_files[h] : NULL;
}

int16 resFileOpen(const char *path, int16 mode) {
    SDL_IOStream *io = openFile(path, mode);
    static int dbg = -1;
    if (dbg < 0) dbg = getenv("F19_DBG") ? 1 : 0;
    if (dbg) fprintf(stderr, "resOpen %s -> %s\n", path, io ? "ok" : "FAIL");
    return io ? f19_fileSlot(io) : -1;
}
int16 resFileCreate(const char *path, int16 attr) {
    SDL_IOStream *io = createFile(path, attr);
    return io ? f19_fileSlot(io) : -1;
}
int16 resFileClose(int16 h) {
    SDL_IOStream *io = f19_fileIo(h);
    if (!io) return -1;
    f19_files[h] = NULL;
    fileClose(io);
    return 0;
}

int16 resFileReadFar(int16 h, int16 count, int16 off, int16 seg);

/* resFileRead — near read: (h, count, dstoff) into f19_dseg; count <0 => EOF.
 * (sub_1E1CC -> sub_1E2E2, int21/3Fh DS=dseg). */
int16 resFileRead(int16 h, int16 count, int16 off) {
    return resFileReadFar(h, count, off, 0);
}
/* seg000:0x47fc/sub_1E1E0 — far read: (h, count, off, seg); count <0 => EOF. */
int16 resFileReadFar(int16 h, int16 count, int16 off, int16 seg) {
    SDL_IOStream *io = f19_fileIo(h);
    char *dst = F19_FP(seg, off);
    size_t got;
    if (!io || !dst) return -1;
    if (count < 0) {
        Sint64 avail = SDL_GetIOSize(io) - SDL_TellIO(io);
        if (avail < 0) return -1;
        count = (int16)avail;
    }
    got = fileRead(dst, 1, (uint16)count, io);
    return (int16)got;
}
/* sub_1E1F8/sub_1E39D — int21/40h write: (h, count, bufOff, bufSeg, extraOff)
 * writes count bytes from seg:(bufOff+extraOff). */
int16 resFileWrite(int16 h, int16 count, int16 bufOff, int16 bufSeg, int16 extra) {
    SDL_IOStream *io = f19_fileIo(h);
    char *src = F19_FP(bufSeg, (uint16)(bufOff + extra));
    if (!io || !src) return -1;
    return (int16)fileWrite(src, 1, (uint16)count, io);
}

/* seg000:0x4746 — open(path,0) → resFileReadFar(h,b,c,-1) → close
 * (sub_14929 = int21/3Fh: CX=count 0xFFFF, DX=off, DS=seg) */
int16 resFileReadBlock(const char *path, int16 b, int16 c) {
    int16 h, r;
    h = resFileOpen(path, 0);
    r = resFileReadFar(h, -1, b, c);
    resFileClose(h);
    return r;
}
/* seg000:0x477e — create(path,0) → sub_149B9(fd,e,b,c,d): int21/40h writes
 * CX=e bytes from DS:DX = seg c : offset b+d. */
int16 resFileWriteBlock(const char *path, int16 b, int16 c, int16 d, int16 e) {
    int16 h, r;
    h = resFileCreate(path, 0);
    r = resFileWrite(h, e, b, c, d);
    resFileClose(h);
    return r;
}

/* sub_1E172 — lseek(h, off-lo, off-hi, mode) over the handle table */
int16 sub_1E172(int16 h, int16 lo, int16 hi, int16 mode) {
    SDL_IOStream *io = f19_fileIo(h);
    int32 off = ((int32)hi << 16) | (uint16)lo;
    static const SDL_IOWhence wh[3] = {SDL_IO_SEEK_SET, SDL_IO_SEEK_CUR, SDL_IO_SEEK_END};
    if (!io || mode < 0 || mode > 2) return -1;
    return (int16)SDL_SeekIO(io, off, wh[mode]);
}

/* FILE*-style globals used by stgrid.c/stparse.c collapse onto the table */
int16 f19_fdOf(FILE *f) { return (int16)(intptr_t)f; }

/* 0f43:000a — EXEC-overlay-load an MZ file's load image into an f19 seg
 * block and return the block handle. START uses it for "scenery0.exe"
 * (the string-table resource): the original probes free memory, allocs a
 * block, EXEC/4B03-loads the image, shrinks it, then the reloc entries
 * patch the in-image seg words (record +0x18/+0x1A) to the block seg. */
int16 far ovlF43_a(int16 v) {
    const char *name = (const char *)(f19_dseg + (uint16)v);
    uint8 hdr[0x20];
    uint16 pages, lastpg, nrel, reloff, hdrpar;
    int32 imgsz;
    int16 h, seg;
    uint8 *img;
    int i;

    h = resFileOpen(name, 0);
    if (h < 0) return 0;
    if (fileRead(hdr, 1, 0x20, f19_fileIo(h)) != 0x20 ||
        hdr[0] != 'M' || hdr[1] != 'Z') {
        resFileClose(h);
        return 0;
    }
    lastpg = *(uint16 *)(hdr + 2);  pages = *(uint16 *)(hdr + 4);
    nrel   = *(uint16 *)(hdr + 6);  hdrpar = *(uint16 *)(hdr + 8);
    reloff = *(uint16 *)(hdr + 0x18);
    imgsz = (int32)(pages - 1) * 0x200 + (lastpg ? lastpg : 0x200)
          - (int32)hdrpar * 16;
    seg = f19_allocSeg((uint16)((imgsz + 0xF) >> 4));
    img = (uint8 *)f19_segPtr(seg);
    if (!img || imgsz <= 0) { resFileClose(h); return 0; }
    SDL_SeekIO(f19_fileIo(h), (Sint64)hdrpar * 16, SDL_IO_SEEK_SET);
    if (fileRead(img, 1, (size_t)imgsz, f19_fileIo(h)) != (size_t)imgsz) {
        resFileClose(h);
        return 0;
    }
    for (i = 0; i < nrel; i++) {
        uint8 re[4];
        uint32 tgt;
        SDL_SeekIO(f19_fileIo(h), reloff + (Sint64)i * 4, SDL_IO_SEEK_SET);
        if (fileRead(re, 1, 4, f19_fileIo(h)) != 4) break;
        tgt = (uint32)*(uint16 *)(re + 2) * 16 + *(uint16 *)re;
        *(uint16 *)(img + tgt) += (uint16)seg;
    }
    resFileClose(h);
    return seg;
}

/* 0f43:010d — splat a string-resource record's offset directory into the
 * dseg far-ptr cells at 0x7EA + rec[0x1C]*4, each entry {off,blockseg}.
 * scenery0.exe's record (count 0x8C at +0x22, offs at +0x24) lands the
 * count bytes and every menu string used by the row widgets. */
void far ovlF43_10d(int16 seg) {
    uint8 *rec = (uint8 *)f19_segPtr(seg);
    uint32 dst;
    int i, cnt;
    if (!rec) return;
    dst = 0x7EA + (uint32)*(uint16 *)(rec + 0x1C) * 4;
    cnt = *(uint16 *)(rec + 0x22);
    for (i = 0; i < cnt; i++) {
        *(uint16 *)(f19_dseg + dst + (uint32)i * 4) =
            *(uint16 *)(rec + 0x24 + (uint32)i * 2);
        *(uint16 *)(f19_dseg + dst + (uint32)i * 4 + 2) = (uint16)seg;
    }
}
