#!/bin/sh
# Teste de PC da logica dos controles (nao compila o cliente Android).
cd "$(dirname "$0")" && g++ -std=c++17 -Wall -Wextra -O1 -x c++ -o /tmp/test_controles test_controles.test && /tmp/test_controles
