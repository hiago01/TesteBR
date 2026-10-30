# Teclado nativo do chat + correção do dano entre jogadores

## 1. Teclado (campo de digitar + botões ⋁ ⋀ /)
**De onde vinham:** `res/layout/layout_keyboard.xml` + `game/ui/CustomKeyboard.java` — Views do Android (EditText + 3 Button com os
símbolos ⋁ ⋀ de uma fonte do sistema). Dependiam de teclado/IME do Android ativo e de a fonte do aparelho ter os símbolos; sem isso,
quebrava. Texto era enviado em `windows-1251` (russo), então "ã/ç" chegavam como "?".

**Agora:** tudo é nativo (C++/ImGui), nenhum teclado do Android é usado no chat.
- `gui/samp_widgets/KeyboardCore.h` — lógica (digitar, cursor, Shift/Caps com toque duplo, símbolos, acentos, histórico). Testada no PC.
- `gui/samp_widgets/keyboard.cpp/.h` — desenho e toque. Campo com cursor piscando e rolagem; botões ⋁ ⋀ / desenhados por código
  (sem fonte); layout pt-BR com `ç` na linha do "L" e página de acentos (`áã`): á à â ã é ê í ó ô õ ú ü ç ñ ª º ° § « » ¡ ¿ (Shift = maiúscula).
- ⋀ = mensagem anterior, ⋁ = volta até o que você estava digitando, `/` = insere "/" no cursor. Apagar repete ao segurar.
- Toque fora ou botão Voltar fecha e guarda o rascunho (volta ao reabrir no mesmo campo); Enviar limpa.
- Bytes enviados ao servidor: **Latin-1/CP1252** (ç = 0xE7, ã = 0xE3), igual a um .pwn salvo em ANSI.
- `settings.cpp`: `iAndroidKeyboard` fixo em false (o .ini é ignorado). Launcher: `FixedSettings`, `KeyboardGuard` e `SettingsFragment`
  agora gravam `androidkeyboard=0`; a linha "Keyboard" das Configurações some. `SAMP.java` não cria mais o `CustomKeyboard`.
- `gui/imguirenderer`: novo `drawRoundedRect`.

## 2. Dano (ninguém levava tiro)
Dois defeitos no cliente (já vinham no código-base original):
1. `game/hooks.cpp`, `ComputeDamageResponse`: `if (pDamager != pGamePed && IsValidGamePed(pGamePed)) return true;` — `IsValidGamePed(pGamePed)`
   confere o ped LOCAL (sempre válido), então **qualquer** agressor que não fosse o jogador local era ignorado: o cliente nunca mandava
   `SendTakeDamageEvent`, o servidor não recebia `OnPlayerTakeDamage` e a vida não descia. Regra agora em `game/DamageRules.h`:
   só passa a combinação "jogador remoto acerta o ped local" (as outras seguem ignoradas como antes).
2. `net/remoteplayer.cpp`, `StoreBulletFullSyncData`: passava `&m_pPed` (endereço do ponteiro) como entidade atingida, em vez de `m_pPed`
   (3 lugares: ped local, outro jogador, veículo). O hook de ProcessLineOfSight lia posição/matriz de lixo (lag compensation 1) e o tiro não acertava.
Logs novos no `samp_log.txt`: `[TIRO] jogador N atirou no jogador local ...` e `[DANO] jogador N me acertou ...`.

## Testes de PC (rodam sem celular)
`bash testes/run_host_keyboard.sh` → lógica (733 verificações), teclado real com ImGui sem tela em 6 resoluções (239 cada: toques, ç/ã como
byte Latin-1, histórico, caps, repetição do apagar, rascunho, nada fora da tela) e regra de dano (8).
`tools_host/kb_dump_to_png.py` gera PNG do quadro (`KB_DUMP=/tmp/k ./kbr 1280 720`).

## NÃO verificado (precisa de celular)
Build Gradle/NDK; aparência com a fonte real do jogo; toque real; se o dano chega mesmo (procure `[DANO]` no log e `OnPlayerTakeDamage` no servidor).
