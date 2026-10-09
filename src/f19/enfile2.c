/* ported from f19ru src_end/enfile.c — END.EXE file-service wrappers.
 * The int21 primitives map onto the f19 module's resFile* shims (f19file.c
 * / enfile.c); loadFileSection/writeFileSection are shared with the EGAME
 * copies already in enfile.c and are not duplicated here. */
#include "f19en.h"

extern int16 resFileOpen(const char *name, int16 mode);
extern int16 resFileCreate(const char *name, int16 attr);
extern int16 resFileClose(int16 handle);
extern int16 resFileRead(int16 handle, int16 count, int16 bufOff);
extern int16 resFileReadFar(int16 handle, int16 count, int16 bufOff, int16 bufSeg);
extern int16 resFileWrite(int16 handle, int16 off, int16 bufOff,
                          int16 bufSeg, int16 addend);

int16 openFileWrapper(const char *name, int16 mode) {
    return resFileOpen(name, mode);
}

int16 createFileWrapper(const char *name, int16 attr) {
    return resFileCreate(name, attr);
}

void closeFileWrapper(int16 fd) {
    resFileClose(fd);
}

int16 readFile1Wrapper(int16 fd, int16 count, int16 off) {
    return resFileRead(fd, count, off);
}

int16 readFile2Wrapper(int16 fd, int16 count, int16 off, int16 seg) {
    return resFileReadFar(fd, count, off, seg);
}

int16 writeFileAtRawWrapper(int16 fd, int16 count, int16 off, int16 seg, int16 addend) {
    return resFileWrite(fd, count, off, seg, addend);
}

/* seg000:0x1a3e — buffered byte reader over the 0x200 block refill; the
 * shared buffer cursor is word_1BADC and picStreamRead refills the block. */
int16 readPicStream(uint8 *dst, int16 count, int16 fd) {
    int16 i;
    for (i = 0; i < count; i++) {
        if (word_1BADC > 0x1ff) {
            picStreamRead(fd);
            word_1BADC = 0;
        }
        *dst++ = picStreamBuf[word_1BADC++];
    }
    return i;
}

/* seg000:0x214c — staged-stream variant used by runMapView (refills via
 * picStageRefill instead of a file read) */
int16 readStageStream(uint8 *dst, int16 count) {
    int16 i;
    for (i = 0; i < count; i++) {
        if (word_1BADC > 0x1ff) {
            picStageRefill();
            word_1BADC = 0;
        }
        *dst++ = picStreamBuf[word_1BADC++];
    }
    return i;
}

/* seg000:0x212c — read n staged bytes at the word_1C6EA write cursor and
 * advance it. Returns the cursor start (a dseg offset the caller casts
 * via f19_dsegAt). */
int16 stageAppend(int16 n) {
    int16 off = f19_dsegOff(word_1C6EA);
    readStageStream(word_1C6EA, n);
    word_1C6EA += n;
    return off;
}

/* seg000:0x20fc — allocate a section buffer and clear it */
int16 allocClearBuf(int16 size) {
    int16 seg;
    seg = f19en_allocBuffer(size);
    memset(f19_segPtr(seg), 0, (uint16)size);
    return seg;
}
