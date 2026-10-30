#!/bin/sh
# Testes de unidade da logica pura do launcher (sem Android). Requer JDK 11+ (javac).
# org/json aqui e um STUB de teste; no app real e o org.json do Android.
set -e
cd "$(dirname "$0")"
SRC=../../app/src/main/java/com/samp/mobile/launcher/util
PACK=../../app/src/main/java/com/samp/mobile/launcher/pack
rm -rf out && mkdir -p out
JAVAC="javac"; command -v javac >/dev/null 2>&1 || JAVAC="java -m jdk.compiler/com.sun.tools.javac.Main"
$JAVAC -d out org/json/*.java $SRC/ServerConfigParser.java $SRC/SampQueryAPI.java $PACK/*.java test/*.java
java -cp out test.ParserTest
java -cp out test.QueryTest
java -cp out test.PackTest
java -cp out test.RepairTest
java -cp out test.IniFixTest
# Conferencia com o INIReader REAL do jogo (vendor/inih): o settings.ini ajustado precisa dar androidkeyboard=1 de verdade.
if command -v g++ >/dev/null 2>&1; then
  V=../../app/src/main/cpp/samp/vendor/inih
  rm -rf out/ini && mkdir -p out/ini
  java -cp out test.IniFixTest out/ini >/dev/null
  g++ -std=c++17 -I$V -I$V/cpp -o out/inih_check inih_check.cpp $V/cpp/INIReader.cpp $V/ini.c
  out/inih_check out/ini/*.ini && echo "INIREADER REAL: androidkeyboard=1 em todos os casos"
fi
