#!/bin/sh
# Testes de host do /provador (sem Android, sem NDK): compila o CustomSkin.cpp REAL com stubs do RenderWare/rede.
#   sh testes/run_host_provador.sh            (rode da raiz do projeto)
# Verde = a logica (estados, preparacao automatica, previa/commit/cancelar, confirmacao do servidor, rede) passou
# com materiais/texturas SIMULADOS. NAO prova nada sobre o RenderWare real nem sobre o aparelho.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/app/src/main/cpp/samp"
T="$(mktemp -d)"
trap 'rm -rf "$T"' EXIT
mkdir -p "$T/samp/game"
cp -r "$ROOT/testes/stub/samp/." "$T/samp/"
for f in CustomSkin.cpp CustomSkin.h ProvadorModel.h SkinSlots.h DffPreflight.h SkinImageUtil.h; do cp "$SRC/game/$f" "$T/samp/game/"; done
cp "$ROOT/testes/host_test_provador.cpp" "$T/t.cpp"
g++ -std=c++17 -Wall -Wno-unused-function -DCUSTOMSKIN_HOST_STUBS -I"$T/samp" -I"$T/samp/game" -o "$T/t" "$T/t.cpp"
"$T/t"
g++ -std=c++17 -Wall -I"$SRC/game" -o "$T/m" "$ROOT/testes/host_test_provador_model.cpp"
"$T/m"
g++ -std=c++17 -Wall -I"$SRC/game" -o "$T/ld" "$ROOT/testes/host_test_lookdefaults.cpp"
"$T/ld"
g++ -std=c++17 -Wall -I"$SRC/game" -o "$T/rt" "$ROOT/testes/host_test_rosto_tom.cpp"
"$T/rt"
g++ -std=c++17 -Wall -o "$T/pb" "$ROOT/testes/host_test_passengerbutton.cpp"
"$T/pb"
