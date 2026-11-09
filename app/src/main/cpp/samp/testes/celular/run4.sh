#!/bin/sh
# Teste de PC da Fase 5 (Camera/Galeria/Gram: interface + token). Nao compila o cliente Android.
cd "$(dirname "$0")" && g++ -std=c++17 -Wall -Wextra -Wno-unused-parameter -O1 -pthread -x c++ -o /tmp/test_celular4 test_celular4.test && /tmp/test_celular4 "$@"
