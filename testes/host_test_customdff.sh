#!/bin/bash
# bash host_test_customdff.sh /caminho/laehospital1.dff
set -e
D=$(mktemp -d); mkdir -p $D/samp/game/Core
cp ../app/src/main/cpp/samp/game/CustomDff.h $D/samp/game/
cp stub/samp/main.h $D/samp/main.h
cat > $D/samp/game/Core/KeyGen.h <<'K'
#pragma once
#include <cstdint>
#include <cctype>
struct CKeyGen { static uint32_t GetUppercaseKey(const char* s){ uint32_t c=0xFFFFFFFF; for(;*s;++s){ c^=(uint8_t)toupper((unsigned char)*s); for(int k=0;k<8;k++) c=(c>>1)^(0xEDB88320u&-(c&1)); } return c; } };
K
g++ -std=c++20 -Wall -I$D -I$D/samp -o $D/t host_test_customdff.cpp
rc=0
for i in 1 2 3 4 5 6 7 8; do echo "## cenario $i"; $D/t "$1" $i || rc=1; done
[ $rc = 0 ] && echo "TODOS OK" || echo "HOUVE FALHAS"
exit $rc
