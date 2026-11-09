#!/bin/sh
# Teste de PC do celular (nao compila o cliente Android).  Uso: sh run.sh [pasta_de_dumps]
cd "$(dirname "$0")" && g++ -std=c++17 -Wall -Wextra -Wno-unused-parameter -O1 -x c++ -o /tmp/test_celular test_celular.test && /tmp/test_celular "$@"
