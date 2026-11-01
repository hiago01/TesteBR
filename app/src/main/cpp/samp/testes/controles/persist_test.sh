#!/bin/sh
# Testa controlmode.cpp (persistencia por jogador no settings.ini) no PC, com stubs de main.h/settings.h e o SimpleIni real do projeto.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"; SAMP="$HERE/../.."
T=$(mktemp -d); mkdir -p "$T/gui/samp_widgets" "$T/stor/SAMP"
cp "$SAMP/gui/samp_widgets/controlmode.cpp" "$SAMP/gui/samp_widgets/controlmode.h" "$SAMP/gui/samp_widgets/controlmode_logic.h" "$T/gui/samp_widgets/"
ln -s "$SAMP/vendor" "$T/vendor"
cat > "$T/main.h" <<'EOS'
#pragma once
#include <cstdio>
#include <cstdarg>
extern const char* g_pszStorage;
void FLog(const char* fmt, ...);
EOS
cat > "$T/settings.h" <<'EOS'
#pragma once
struct stSettings { char szNickName[25]; };
class CSettings { public: stSettings m; stSettings& Get() { return m; } };
EOS
cp "$HERE/persist/persist_main.test" "$T/"
# so no PC: libstdc++ novo nao tem std::__binary_function, que o SimpleIni antigo do projeto usa (no NDK existe)
cat > "$T/shim.h" <<'EOS'
#pragma once
#include <functional>
namespace std { template<class A, class B, class R> struct __binary_function { typedef A first_argument_type; typedef B second_argument_type; typedef R result_type; }; }
EOS
g++ -std=gnu++17 -include "$T/shim.h" -Wall -Wextra -Wno-deprecated-declarations -I"$T" -o "$T/p" -x c++ "$T/persist_main.test" -x none "$T/gui/samp_widgets/controlmode.cpp" -DSTORAGE="\"$T/stor/\""
"$T/p"
rm -rf "$T"
