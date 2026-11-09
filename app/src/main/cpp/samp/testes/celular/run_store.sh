#!/bin/sh
# Teste de PC da Loja / IMEI / papel de parede contra o servico Node real + PostgreSQL de TESTE (veja celular-service/test/store_server.js).
SRV=${SRV:-/tmp/dom/srv/celular-service}
cd "$(dirname "$0")" || exit 1
g++ -std=c++17 -Wall -Wextra -Wno-unused-parameter -O1 -pthread -x c++ -o /tmp/test_store test_store.test || exit 1
export GRAM_PORT=${GRAM_PORT:-18789}
( cd "$SRV" && node test/store_server.js ) > /tmp/store_srv.out 2>&1 &
PID=$!
for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do grep -q '^READY' /tmp/store_srv.out 2>/dev/null && break; sleep 0.5; done
J=$(grep '^READY' /tmp/store_srv.out | sed 's/^READY //')
[ -z "$J" ] && { echo "servidor de teste nao subiu:"; cat /tmp/store_srv.out; kill $PID 2>/dev/null; exit 1; }
export TOK_A=$(echo "$J" | python3 -c 'import sys,json;print(json.load(sys.stdin)["ta"])')
export TOK_B=$(echo "$J" | python3 -c 'import sys,json;print(json.load(sys.stdin)["tb"])')
export JPG=${JPG:-$SRV/test/foto_320x240.jpg}
/tmp/test_store; R=$?
kill $PID 2>/dev/null; wait $PID 2>/dev/null
exit $R
