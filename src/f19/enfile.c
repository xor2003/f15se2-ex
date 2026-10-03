#include "f19eg.h"

/* enfile.c — resident file loaders (F19)
 * open/create -> read/write -> close wrappers around the resident
 * file-service shims in seg000:0xe19a+ (resFileOpen/resFileCreate/
 * resFileRead/resFileReadFar/resFileWrite/resFileClose in egui.c,
 * themselves thin shims over the INT21-backed file services).
 */

int16 resFileOpen(const char *name, int16 mode);                       /* sub_1E19A egui.c */
int16 resFileCreate(const char *name, int16 attr);                     /* sub_1E1AC egui.c */
int16 resFileClose(int16 handle);                                      /* sub_1E1BE egui.c */
int16 resFileRead(int16 handle, int16 count, int16 bufOff);            /* sub_1E1CC egui.c */
int16 resFileReadFar(int16 handle, int16 count, int16 bufOff, int16 bufSeg); /* sub_1E1E0 egui.c */
int16 resFileWrite(int16 handle, int16 off, int16 bufOff,
                   int16 bufSeg, int16 count);                          /* sub_1E1F8 egui.c */

/* ==== seg000:0xe0f4 ==== */
int16 loadFileNear(const char *name, int16 bufOff) {
    int16 fd, result;
    fd = resFileOpen(name, 0);
    result = resFileRead(fd, -1, bufOff);
    resFileClose(fd);
    return result;
}

/* ==== seg000:0xe12a ==== */
int16 loadFileSection(const char *name, int16 bufOff, int16 bufSeg) {
    int16 fd, result;
    fd = resFileOpen(name, 0);
    result = resFileReadFar(fd, -1, bufOff, bufSeg);
    resFileClose(fd);
    return result;
}

/* ==== seg000:0xe162 ==== */
int16 writeFileSection(const char *name, int16 b, int16 c, int16 d, int16 e) {
    int16 fd, result;
    fd = resFileCreate(name, 0);
    result = resFileWrite(fd, e, b, c, d);
    resFileClose(fd);
    return result;
}
