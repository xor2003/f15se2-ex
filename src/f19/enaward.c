/* ported from f19ru src_end/enaward.c — see that file for seg000 offsets */
/* END.EXE — file-open/seek/decode helpers (f15se2 enaward.c lineage) */
#include "f19en.h"

extern int16 openFileWrapper(const char *name, int16 mode);  /* sub_11336 */
extern void seekFileAt(int16 fd, int16 off, int16 whence);   /* sub_11694 */
extern void closeFileWrapper(int16 fd);                      /* sub_1135A */
extern void picBlit(int16 fd, int16 page);                   /* 0x1706 — asm decode-to-page */
extern void decodePic(int16 fd, int16 page);                 /* 0x17e2 — asm decode */

/* seg000:0x15a6 — open, seek, close (map name: loadPicFromFileAt) */
void loadPicFromFileAt(const char *name, int16 off, int16 whence) {
    int16 handle;
    handle = openFileWrapper(name, 0);
    seekFileAt(handle, off, whence);
    closeFileWrapper(handle);
}

/* seg000:0x15df — open + picBlit + close */
void openBlitClosePic(const char *name, int16 page) {
    int16 handle;
    handle = openFileWrapper(name, 0);
    picBlit(handle, page);
    closeFileWrapper(handle);
}

/* seg000:0x1615 — open + decodePic + close */
void openDecodeClosePic(const char *name, int16 page) {
    int16 handle;
    handle = openFileWrapper(name, 0);
    decodePic(handle, page);
    closeFileWrapper(handle);
}

/* seg000:0x164b — open + lseek(SEEK_SET) + decodePic + close */
void openDecodePicAt(const char *name, int16 page, int32 offset) {
    int16 handle;
    handle = openFileWrapper(name, 0);
    lseek(handle, offset, 0);
    decodePic(handle, page);
    closeFileWrapper(handle);
}
