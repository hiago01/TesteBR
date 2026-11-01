#!/bin/sh
# Testes NO PC (g++, C++17) da rota no minimapa. NAO roda no Android. Uso (na raiz do projeto): sh testes/rodar_testes_minimapa.sh
set -e
CXX=${CXX:-g++}
IMG=app/src/main/cpp/samp/vendor/imgui
SRC="$IMG/imgui.cpp $IMG/imgui_draw.cpp $IMG/imgui_widgets.cpp app/src/main/cpp/samp/gui/imguirenderer.cpp"
F="-std=c++17 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -Iapp/src/main/cpp/samp -include cstdint -include unistd.h -include sys/stat.h"
echo "== 1/2 logica (recorte no circulo/poligono, casco convexo, mascara, TrimStart) =="
$CXX $F -Wall -o /tmp/mr_test testes/test_miniroute.cpp && /tmp/mr_test
echo "== 2/2 interface com ImGui real (gera /tmp/minimapa_A.ppm e _B.ppm) =="
$CXX $F -o /tmp/mm_test testes/host_test_minimapa.cpp $SRC && /tmp/mm_test | grep -v "^   w\|run0"
