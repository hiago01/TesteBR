# Voice sempre ativo (VoiceChatEnable removido)

Copie `app/` por cima do projeto. Originais em `backup_originais/` (controlmode_logic.h: so um comentario mudou).

- settings.h/.cpp: campo bVoiceChatEnable e a leitura de [gui] VoiceChatEnable removidos.
  Mantidos: VoiceChatKey, VoiceChatSize, VoiceChatPosX/Y (a tecla nao foi alterada).
- voicebutton.h: o botao TALK (HUD classico) nao checa mais a opcao; so deixa de desenhar no HUD moderno, como antes.
- moderncontrols.cpp: o botao MIC do HUD moderno nunca e escondido (disabledMask = 0).
- FixedSettings.java: deixa de gravar VoiceChatEnable e APAGA a chave do SAMP/settings.ini do jogador ao abrir
  (e a unica referencia ao nome que sobra, so para limpar o arquivo).

Testes de controles de PC: 324/324 e 29/29 OK. NAO verificado: compilacao NDK/Gradle, aparelho,
e a chamada Profile.Section.remove do ini4j (compilacao Java).
