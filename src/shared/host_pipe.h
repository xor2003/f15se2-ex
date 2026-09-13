#pragma once
#include <stdio.h>

// Importers emit binary data; Windows text pipes would translate newline bytes.
static inline FILE *openBinaryImportPipe(const char *command) {
#ifdef _WIN32
    return _popen(command, "rb");
#else
    return popen(command, "r");
#endif
}

static inline int closeImportPipe(FILE *pipe) {
#ifdef _WIN32
    return _pclose(pipe);
#else
    return pclose(pipe);
#endif
}
