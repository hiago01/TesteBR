#!/bin/sh
# Testes de unidade da logica pura do launcher (sem Android). Requer JDK 11+ (javac).
# org/json aqui e um STUB de teste; no app real e o org.json do Android.
set -e
cd "$(dirname "$0")"
SRC=../../app/src/main/java/com/samp/mobile/launcher/util
rm -rf out && mkdir -p out
JAVAC="javac"; command -v javac >/dev/null 2>&1 || JAVAC="java -m jdk.compiler/com.sun.tools.javac.Main"
$JAVAC -d out org/json/*.java $SRC/ServerConfigParser.java $SRC/SampQueryAPI.java test/*.java
java -cp out test.ParserTest
java -cp out test.QueryTest
