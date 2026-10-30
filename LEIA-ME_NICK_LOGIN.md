# Nick no launcher + Login/Registro (DOF2) no servidor

## Cliente (launcher)
- `NickManager` (novo): guarda o nick em SharedPreferences e em `SAMP/settings.ini` ([client] name).
- 1a vez que o jogador clica em CONECTAR: abre a tela "Escolha seu Nick" (obrigatoria, sem cancelar).
  O nick antigo das Configuracoes e IGNORADO (so vale o que for confirmado nessa tela).
- Na tela Servidores, na barra de baixo, aparece o chip "Nick: <nome>" para trocar o nick depois.
- O campo Nickname saiu das Configuracoes (o cartao fica oculto).
- Regras do nick: 3 a 20 caracteres, letras/numeros e `_ [ ] ( ) $ @ . =`, sem espaco.
- Textos: res/values/maximo_launcher.xml (`mx_nick_*`). Layout: res/layout/mx_dialog_nick.xml.

## Servidor (gamemodes/maximorp.pwn + qawno/include/maximo_conta.inc)
- Conta por nick em `scriptfiles/MaximoRP/Contas/<nick>.ini` (DOF2). Senha = SHA-256 + salt aleatorio (2000 rodadas), nunca em texto.
- Nick sem conta -> dialogo Registro (senha + confirmacao). Nick com conta -> dialogo Login.
- 3 senhas erradas, botao Sair ou 120 s sem logar = Kick. Sem login: sem spawn, sem comandos, sem chat.
- Salva: admin, level, kills, dinheiro, skin, vida, colete e posicao/interior/mundo. Grava ao sair e a cada 60 s.
  No 1o spawn apos o login a posicao/vida salvas sao restauradas.
- Comandos novos: `/mudarsenha [atual] [nova]` e `/setadmin [id] [0-5]` (so RCON).
- Inventario e Visual continuam por nick (nao mudaram).
- Instalacao: copie `maximo_conta.inc` para `qawno/include/`, substitua `gamemodes/maximorp.pwn`, COMPILE no qawno
  (gera o novo maximorp.amx) e reinicie. A pasta `scriptfiles/MaximoRP/Contas/` e criada sozinha.
