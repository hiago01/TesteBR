#!/bin/sh
# Teste de PC do Gram contra o servico Node real + PostgreSQL de TESTE (porta 5544 por padrao; veja celular-service/test/gram_server.js).
SRV=${SRV:-/tmp/dom/srv/celular-service}
cd "$(dirname "$0")" || exit 1
g++ -std=c++17 -Wall -Wextra -Wno-unused-parameter -O1 -pthread -x c++ -o /tmp/test_gram test_gram.test || exit 1
export GRAM_PORT=${GRAM_PORT:-18788}
( cd "$SRV" && node test/gram_server.js ) > /tmp/gram_srv.out 2>&1 &
PID=$!
for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do grep -q '^READY' /tmp/gram_srv.out 2>/dev/null && break; sleep 0.5; done
J=$(grep '^READY' /tmp/gram_srv.out | sed 's/^READY //')
[ -z "$J" ] && { echo "servidor de teste nao subiu:"; cat /tmp/gram_srv.out; kill $PID 2>/dev/null; exit 1; }
export TOK_A=$(echo "$J" | python3 -c 'import sys,json;print(json.load(sys.stdin)["ta"])')
export TOK_B=$(echo "$J" | python3 -c 'import sys,json;print(json.load(sys.stdin)["tb"])')
/tmp/test_gram; R=$?
kill $PID 2>/dev/null; wait $PID 2>/dev/null
exit $R
