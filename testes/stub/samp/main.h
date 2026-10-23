#pragma once
#include <cstdio>
#include <cstdarg>
extern char* g_pszStorage;
inline void FLog(const char* f, ...) { va_list a; va_start(a,f); vprintf(f,a); va_end(a); printf("\n"); }
