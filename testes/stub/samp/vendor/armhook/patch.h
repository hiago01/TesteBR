#pragma once
#include <cstdint>
#include <vector>
struct CHook { static inline std::vector<uintptr_t> calls; template<class R,class...A> static R CallFunction(uintptr_t,A...){ return R(); } };
