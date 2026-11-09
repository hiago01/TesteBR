#!/bin/sh
# Teste de PC: celular como item + chip (protocolo op 8/9, PhoneNet sem chip, Ajustes, Loja sem celular). Nao compila o cliente Android.
cd "$(dirname "$0")" && g++ -std=c++17 -Wall -Wextra -Wno-unused-parameter -O1 -x c++ -o /tmp/test_celular7 test_celular7.test && /tmp/test_celular7 "$@"
