#!/bin/sh
# Teste de PC da Fase 2 do celular (nao compila o cliente Android).
cd "$(dirname "$0")" && g++ -std=c++17 -Wall -Wextra -Wno-unused-parameter -O1 -x c++ -o /tmp/test_celular2 test_celular2.test && /tmp/test_celular2 "$@"
