#!/bin/bash
# Roda no PC: (1) logica do mapa (WorldMapCore.h), (2) interface real (WorldMapUI.h + ImGui sem tela) em varias resolucoes.
# Nada de celular. Uso:  bash testes/run_host_worldmap.sh
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/app/src/main/cpp/samp"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
echo "== 1/2 logica (WorldMapCore.h)"
g++ -std=c++14 -Wall -Wextra -I "$SRC/gui/samp_widgets" "$ROOT/testes/host_test_worldmap_core.cpp" -o "$T/wmc" && "$T/wmc"
echo "== 2/2 interface (desenho + toque, ImGui sem tela)"
mkdir -p "$T/h/samp/gui/samp_widgets" "$T/h/samp/vendor"
H="$T/h/samp"
cp -r "$SRC/vendor/imgui" "$H/vendor/imgui"
cp "$SRC/gui/imguirenderer.h" "$SRC/gui/imguirenderer.cpp" "$SRC/gui/uisettings.h" "$SRC/gui/uisettings.cpp" "$H/gui/"
mkdir -p "$T/h/inc"
printf '#pragma once\n#include <cstdint>\n#include <string>\n#include <jni.h>\nuint32_t GetTickCount();\n' > "$H/main.h"
printf '#pragma once\n' > "$H/log.h"
printf '#pragma once\nstruct SettingsData { int iChatMaxMessages = 8; };\nclass CSettings { public: const SettingsData& Get() const { static SettingsData d; return d; } };\nextern CSettings* pSettings;\n' > "$H/settings.h"
printf '#pragma once\n#include <cstdint>\ntypedef int32_t jsize; typedef int8_t jbyte; typedef void* jobject; typedef void* jbyteArray; typedef uint8_t jboolean;\n' > "$T/h/inc/jni.h"
cp "$SRC/gui/samp_widgets/WorldMapUI.h" "$SRC/gui/samp_widgets/WorldMapCore.h" "$H/gui/samp_widgets/"
cp "$ROOT/testes/host_test_worldmap_render.cpp" "$T/h/test.cpp"
( cd "$T/h" && g++ -std=c++14 -w -include cstdint -include cstdlib -include cfloat -include algorithm -include cassert -include cmath -Iinc -I. -I samp \
   test.cpp samp/gui/uisettings.cpp samp/gui/imguirenderer.cpp samp/vendor/imgui/imgui.cpp samp/vendor/imgui/imgui_draw.cpp samp/vendor/imgui/imgui_widgets.cpp -o wmr )
for r in "2400 1080" "1920 1080" "1600 720" "1280 720" "800 480"; do "$T/h/wmr" $r; done
if [ -n "$MAP_DUMP" ]; then echo "(quadros gravados em $MAP_DUMP_*)"; fi
echo "TUDO OK"
