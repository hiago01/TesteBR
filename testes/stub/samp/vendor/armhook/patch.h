#pragma once
#include <cstdint>
struct CHook {
  static uintptr_t getSym(const char* s);
  template<class F,class O> static void InlineHook(const char* s, F f, O o){ *(void**)o = nullptr; (void)s;(void)f; }
};
