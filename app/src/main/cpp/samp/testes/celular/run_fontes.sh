#!/bin/sh
# Teste de PC: papeis tipograficos (SF Pro) do celular. Nao compila o cliente Android.
cd "$(dirname "$0")" && g++ -std=c++17 -Wall -Wextra -Wno-unused-parameter -O1 -x c++ -o /tmp/test_fontes test_fontes.test && /tmp/test_fontes "$@"
