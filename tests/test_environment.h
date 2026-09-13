#pragma once
#include <cstdlib>

inline void testSetEnvironment(const char *name, const char *value) {
#ifdef _WIN32
    const int result = _putenv_s(name, value ? value : "");
#else
    const int result = value ? setenv(name, value, 1) : unsetenv(name);
#endif
    if (result != 0) std::abort();
}
