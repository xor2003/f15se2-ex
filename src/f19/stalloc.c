/* F-19 buffer alloc — native port of src_start/stalloc.c.
 * The DOS "segment" result is an f19seg handle; callers keep it in int16
 * globals and pass (off,seg) pairs to resFileReadBlock, matching the
 * reconstruction's shapes. */
#include "f19.h"

extern void f19_cleanup(void);
extern void dos_printstring(const char *s);

/* original took a BYTE count; dos_alloc rounded up to paragraphs */
int16 f19_allocBuffer(uint16 bytes) {
    int16 seg = f19_allocSeg((uint16)((bytes + 0xF) >> 4));
    extern char *getenv(const char*);
    if (getenv("F19_DBG")) fprintf(stderr, "[ALLOCBUF] bytes=%x -> %d\n", bytes, seg);
    if (seg < 0x10) {
        f19_cleanup();
        dos_printstring("Insufficient system memory - AllocBuffer$");
        exit(0);
    }
    return seg;
}

void f19_freeBuffer(uint16 seg) {
    extern char *getenv(const char*);
    if (getenv("F19_DBG")) fprintf(stderr, "[FREEBUF] h=%d\n", seg);
    if (f19_freeSeg(seg) != 0) {
        f19_cleanup();
        dos_printstring("...dealloc error...$");
        exit(0);
    }
}
