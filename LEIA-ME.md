# Freada: botoes somem (fade) e NAO aceitam toque

Copie `app/` por cima do projeto. Originais em `backup_originais/`.

- Durante a animacao de freada (RUN_STOP/RUN_STOPR) o joystick e os botoes fazem fade e NAO aceitam toque novo.
  Dedos que ja estavam apertados seguem normais (mover/soltar), para nada ficar preso.
- Sempre ativo: opcao do menu e campo de config REMOVIDOS. A chave `fade_freada` e apagada do settings.ini
  na proxima vez que as configuracoes de controle forem gravadas.
- Trava de seguranca: no maximo 1,5 s seguidos. Nunca no editor de HUD.

NAO verificado: compilacao NDK/Gradle e comportamento no aparelho.
Testes de PC existentes OK (nao cobrem o fade/toque).
