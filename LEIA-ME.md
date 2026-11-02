# CINFO.BIN / MINFO.BIN ausentes nao geram mensagem

- GameFilePolicy: cinfo.bin, minfo.bin e cinfo.sig (qualquer pasta) entram na lista de arquivos "gerados pelo jogo":
  nao sao baixados, nao sao sobrescritos (ja era assim para CINFO.BIN e models/MINFO.BIN; agora vale em qualquer caminho).
- RepairEngine.verify (tela CORRECOES): arquivo "gerado" ausente conta como OK; nao aparece como "ausente".
  Arquivos comuns ausentes continuam sendo listados.
Testes JVM (PackTest): 104 ok, 0 falhas (102 anteriores + 2 novos). Nao verificado: compilacao Gradle/no aparelho.
