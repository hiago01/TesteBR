#!/bin/sh
# Teste de PC da logica dos controles (nao compila o cliente Android).
cd "$(dirname "$0")" && g++ -std=c++17 -Wall -Wextra -O1 -o /tmp/test_controles test_controles.cpp && /tmp/test_controles
