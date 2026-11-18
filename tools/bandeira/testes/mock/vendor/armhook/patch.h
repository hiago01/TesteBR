#pragma once
#include <cstdint>
struct CHook { template <class R, class... A> static R CallFunction(uintptr_t, A...) { return R(); } };
