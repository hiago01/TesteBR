#!/bin/sh
# Testes de host (sem Android): compilam o trecho REAL do CorpTex.cpp e testam contra os seus arquivos.
#   sh run_host_tests.sh <pasta game> <pasta que contem SAMP/skins/pack/>   (ex.: .../cpp/samp/game  /sdcard/.../files/)
# O 1o teste lê o pack de verdade (rosto/cabelo/pele/roupa); o 2o cria uma copia temporaria com as corps para o /cop.
set -e
GAME="$1"; ROOT="$2"
python3 extract_regions.py "$GAME"
g++ -std=c++17 -I"$GAME" -o corp_host corp_host.cpp && ./corp_host "$ROOT"
g++ -std=c++17 -I"$GAME" -o cop_host cop_host.cpp && ./cop_host "$ROOT"
