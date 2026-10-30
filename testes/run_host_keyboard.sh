#!/bin/bash
# Roda no PC: (1) logica do teclado, (2) teclado REAL (keyboard.cpp + ImGui real) com toques simulados em varias resolucoes,
# (3) regra de dano. Nada de celular. Uso:  bash testes/run_host_keyboard.sh
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/app/src/main/cpp/samp"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT

echo "== 1/3 logica (KeyboardCore.h)"
g++ -std=c++14 -Wall -Wextra -I "$SRC/gui/samp_widgets" "$ROOT/testes/host_test_keyboard.cpp" -o "$T/kbt" && "$T/kbt"

echo "== 2/3 teclado real (desenho + toque, ImGui sem tela)"
mkdir -p "$T/h/samp/gui/samp_widgets" "$T/h/samp/game" "$T/h/inc"
H="$T/h/samp"
cp -r "$SRC/vendor/imgui" "$H/vendor_imgui_tmp" && mkdir -p "$H/vendor" && mv "$H/vendor_imgui_tmp" "$H/vendor/imgui"
cp "$SRC/gui/widget.h" "$SRC/gui/widget.cpp" "$SRC/gui/uisettings.h" "$SRC/gui/uisettings.cpp" "$SRC/gui/imguirenderer.h" "$SRC/gui/imguirenderer.cpp" "$H/gui/"
cp "$SRC/gui/samp_widgets/keyboard.cpp" "$SRC/gui/samp_widgets/keyboard.h" "$SRC/gui/samp_widgets/KeyboardCore.h" "$H/gui/samp_widgets/"
printf '#pragma once\n#include <cstdint>\n#include <string>\n#include <jni.h>\nuint32_t GetTickCount();\n' > "$H/main.h"
printf '#pragma once\n' > "$H/log.h"
printf '#pragma once\nstruct SettingsData { int iChatMaxMessages = 8; };\nclass CSettings { public: const SettingsData& Get() const { static SettingsData d; return d; } };\nextern CSettings* pSettings;\n' > "$H/settings.h"
printf '#pragma once\nclass CGame { public: void EnableGameInput(bool b); bool input = true; };\n' > "$H/game/game.h"
printf '#pragma once\n#include <cstdint>\n#include <algorithm>\n#include <cfloat>\n#include <cassert>\n#include "imguirenderer.h"\n#include "uisettings.h"\n#include "widget.h"\n#include "samp_widgets/keyboard.h"\nclass UI {};\n' > "$H/gui/gui.h"
printf '#pragma once\n#include <cstdint>\ntypedef int32_t jsize; typedef int8_t jbyte; typedef void* jobject; typedef void* jbyteArray; typedef uint8_t jboolean;\nstruct JNIEnv { jsize GetArrayLength(jbyteArray) { return 0; } jbyte* GetByteArrayElements(jbyteArray, jboolean*) { return 0; } void ReleaseByteArrayElements(jbyteArray, jbyte*, int) {} };\n#define JNI_ABORT 2\n' > "$T/h/inc/jni.h"
cp "$ROOT/testes/host_test_keyboard_render.cpp" "$T/h/test.cpp"
( cd "$T/h" && g++ -std=c++14 -w -include cstdint -include cstdlib -include cfloat -include algorithm -include cassert -Iinc -I. -I samp \
   test.cpp samp/gui/widget.cpp samp/gui/uisettings.cpp samp/gui/imguirenderer.cpp samp/gui/samp_widgets/keyboard.cpp \
   samp/vendor/imgui/imgui.cpp samp/vendor/imgui/imgui_draw.cpp samp/vendor/imgui/imgui_widgets.cpp -o kbr )
for r in "2400 1080" "2340 1080" "1920 1080" "1600 720" "1280 720" "800 480"; do "$T/h/kbr" $r | grep -v '^DBG'; done

echo "== 3/3 regra de dano"
g++ -std=c++14 -Wall -I "$SRC/game" "$ROOT/testes/host_test_damagerules.cpp" -o "$T/dmg" && "$T/dmg"
echo "TUDO OK"
