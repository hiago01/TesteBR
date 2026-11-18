#!/bin/sh
# Testes da bandeira no PC (g++ >= 11, Linux/WSL). Rode na RAIZ do repositorio:  sh tools/bandeira/testes/build_e_rodar.sh
# Nao usa o NDK nem o jogo: compila os modulos puros (FlagPhysics/FlagModel/FlagMesh) e o Flag.cpp contra cabecalhos "mock".
set -e
G=app/src/main/cpp/samp/game; T=tools/bandeira/testes; O=${TMPDIR:-/tmp}/flag_build; mkdir -p $O
export FLAG_DFF=SAMP/models/flag_maximo.dff
CXX="g++ -std=c++20 -O2 -Wall -Wextra"
echo "== fisica";  $CXX -I$G -o $O/flag_test $T/flag_test.cpp $G/FlagPhysics.cpp $G/FlagModel.cpp && $O/flag_test
echo "== malha";   $CXX -I$G -o $O/mesh_test $T/mesh_test.cpp $G/FlagMesh.cpp $G/FlagPhysics.cpp $G/FlagModel.cpp && $O/mesh_test
echo "== fuzz DFF (ASan+UBSan)"; g++ -std=c++20 -O1 -g -fsanitize=address,undefined -I$G -o $O/fuzz $T/fuzz.cpp $G/FlagModel.cpp && $O/fuzz
echo "== cola com o jogo (cabecalhos mock)"
rm -rf $O/mock && mkdir -p $O/mock/game && cp -r $T/mock/* $O/mock/ && cp $G/Flag.cpp $G/Flag.h $G/FlagMesh.* $G/FlagModel.* $G/FlagPhysics.* $O/mock/game/
mkdir -p $O/store/SAMP/models && cp SAMP/models/flag_maximo.dff $O/store/SAMP/models/
FLAG_STORE=$O/store g++ -std=c++20 -O1 -g -fsanitize=address,undefined -I$O/mock -o $O/glue $T/glue_mock.cpp $O/mock/game/Flag.cpp $O/mock/game/FlagMesh.cpp $O/mock/game/FlagModel.cpp $O/mock/game/FlagPhysics.cpp
FLAG_STORE=$O/store $O/glue | tail -3
