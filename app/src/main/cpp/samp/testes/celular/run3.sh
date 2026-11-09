#!/bin/sh
# Teste de PC da Fase 3 do celular (protocolo + PhoneNet + tela de ligacao). Nao compila o cliente Android.
cd "$(dirname "$0")" && g++ -std=c++17 -Wall -Wextra -Wno-unused-parameter -O1 -x c++ -o /tmp/test_celular3 test_celular3.test && /tmp/test_celular3 "$@"
