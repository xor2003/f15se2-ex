#pragma once
// Minimal dos.h stub for 64-bit native builds

#ifndef _DOS_H_COMPAT64
#define _DOS_H_COMPAT64

// 16-bit calling-convention / pointer-size decorations. All no-ops natively;
// kept only so the ported DOS signatures still parse. Single source for the
// whole tree (uppercase FAR/NEAR/CDECL were formerly duplicated in pointers.h,
// the __far/__cdecl forms in egtypes.h).
#define cdecl
#define far
#define near
#define FAR
#define NEAR
#define CDECL
#define __far

struct WORDREGS {
    unsigned short ax;
    unsigned short bx;
    unsigned short cx;
    unsigned short dx;
    unsigned short si;
    unsigned short di;
    unsigned short cflag;
};

struct BYTEREGS {
    unsigned char al, ah;
    unsigned char bl, bh;
    unsigned char cl, ch;
    unsigned char dl, dh;
};

union REGS {
    struct WORDREGS x;
    struct BYTEREGS h;
};

struct SREGS {
    unsigned short es;
    unsigned short cs;
    unsigned short ss;
    unsigned short ds;
};

inline int int86(int intno, union REGS *inregs, union REGS *outregs) {
    (void)intno;
    (void)inregs;
    (void)outregs;
    return 0;
}
inline int int86x(int intno, union REGS *inregs, union REGS *outregs, struct SREGS *segregs) {
    (void)intno;
    (void)inregs;
    (void)outregs;
    (void)segregs;
    return 0;
}
inline void movedata(unsigned int srcseg, unsigned int srcoff, unsigned int dstseg, unsigned int dstoff, unsigned int nbytes) {
    (void)srcseg;
    (void)srcoff;
    (void)dstseg;
    (void)dstoff;
    (void)nbytes;
}

// Non-standard C functions used by the codebase
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#if !defined(MSDOS) && !defined(_WIN32)
#include <unistd.h>
#else
#include <io.h>
#endif

inline int putch(int c) {
    (void)c;
    return 0;
}
/* Backed by the SDL keyboard layer in eginput.c (egame flight loop). */
int kbhit(void);

inline char *itoa(int value, char *str, int base) {
    if (base == 10) {
        sprintf(str, "%d", value);
    } else if (base == 16) {
        sprintf(str, "%x", value);
    } else {
        str[0] = '\0';
    }
    return str;
}

inline char *strupr(char *s) {
    for (char *p = s; *p; p++) {
        unsigned char b = (unsigned char)*p;
        /* UTF-8 Cyrillic: fold lowercase to uppercase in place (length-stable).
         * D0 B0-BF (а-п) -> D0 90-9F (А-П); D1 80-8F (р-я) -> D0 A0-AF (Р-Я);
         * D1 91 (ё) -> D0 81 (Ё). Lead bytes are left for the next pass. */
        if (b == 0xD0 && (unsigned char)p[1] >= 0xB0 && (unsigned char)p[1] <= 0xBF) {
            p++; *p = (char)((unsigned char)*p - 0x20);   /* а-п -> А-П */
        } else if (b == 0xD1 && (unsigned char)p[1] >= 0x80 && (unsigned char)p[1] <= 0x8F) {
            *p++ = (char)0xD0; *p = (char)((unsigned char)*p + 0x20); /* р-я -> Р-Я */
        } else if (b == 0xD1 && (unsigned char)p[1] == 0x91) {
            *p++ = (char)0xD0; *p = (char)0x81;           /* ё -> Ё */
        } else {
            *p = (char)toupper(b);
        }
    }
    return s;
}

#endif // _DOS_H_COMPAT64
