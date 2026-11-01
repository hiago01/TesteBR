#!/bin/sh
# Roda no PC: teste C++ do indice/nomes + selftest do conversor (precisa do banco do mapa so para o selftest completo).
set -e
cd "$(dirname "$0")"
g++ -std=c++17 -Wall -Wextra -o /tmp/t_stdb test_skintexdb.cpp && /tmp/t_stdb
python3 ../../tools/skin_texdb.py selftest "${1:-../../mapa}" || true
