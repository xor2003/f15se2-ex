#include <string.h>

// ==== seg000:0x2faf ====
void strcpyFromDot(char *dst, const char *src) {
    char ch;
    while ((ch = *dst) != '.' && ch != 0) {
        dst++;
    }
    strcpy(dst, src);
}
