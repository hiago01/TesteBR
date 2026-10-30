#pragma once
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
extern char* g_pszStorage;
inline void FLog(const char* f, ...) { static const bool v = std::getenv("HOST_VERBOSE") != nullptr; if (!v) return; va_list a; va_start(a,f); vprintf(f,a); va_end(a); printf("\n"); }
