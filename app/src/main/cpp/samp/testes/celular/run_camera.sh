#!/bin/sh
# Teste de PC: joystick do personagem com a camera do celular + moldura da Camera. Nao compila o cliente Android.
cd "$(dirname "$0")" && g++ -std=c++17 -Wall -Wextra -Wno-unused-parameter -O1 -x c++ -o /tmp/test_camera test_camera.test && /tmp/test_camera "$@"
