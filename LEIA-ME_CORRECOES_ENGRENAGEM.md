# Engrenagem de Correções (menu lateral "CORREÇÕES")

Fluxo SEPARADO do `UpdateService` e dos pacotes ZIP (nenhum deles foi alterado). Escopo: `files/SAMP` e `files/texdb`.

## O que faz
- **Verificar arquivos**: baixa client_config -> samp.json (opcional) -> files.json; confere cada arquivo por tamanho e SHA-256
  (quando a lista traz `sha256`). Aponta: ausente, tamanho errado, conteúdo corrompido de mesmo tamanho, arquivo existente
  com outra CAIXA (Windows x Android), pastas duplicadas por caixa, arquivos fora da lista. Oferece "Corrigir agora".
- **Reparar texdb/SAMP do zero**: rebaixa tudo da lista. Cada arquivo vai para `.downloads/repair`, é conferido (tamanho + SHA)
  e SÓ ENTÃO substitui o antigo (se o servidor entregar lixo, o arquivo local fica intacto). Arquivos que o jogo regrava
  (settings.ini, favoritos, CINFO, svconfig...) só são repostos se faltarem. Exige espaço livre = maior arquivo + 100 MB.
- **Exportar relatório**: `relatorio_correcao.txt` (aparelho, Android, ABI, espaço, versão do app, problemas, final do
  samp_log.txt) e abre o compartilhar.
- **Limpar temporários**: `AtomicInstaller.recoverOnStartup` + remove `*.part/*.tmp` em texdb/SAMP.

## Arquivos
Novos: `pack/FileDownloader.java`, `pack/RepairEngine.java`, `fragments/RepairFragment.java`, `res/layout/fragment_repair.xml`,
`res/values/maximo_repair.xml`, `res/drawable/mx_ic_repair.xml`, `testes/launcher_unit/test/RepairTest.java`.
Alterados: `MainActivity.java` (+ item de menu antes do SAIR, `PAGE_REPAIR=5`, `MENU_EXIT=6`, `getCount()=6`),
`testes/launcher_unit/run.sh` (+ RepairTest). Nenhuma outra classe existente foi alterada.

## Testado / NÃO testado
- Executado: `sh testes/launcher_unit/run.sh` -> PackTest 88 ok, RepairTest 27 ok (servidor HTTP local real: 503 instável com
  retentativa, 404, download truncado, conteúdo adulterado, corrupção de mesmo tamanho, caixa, volátil, espaço, cancelamento).
- Compilado só contra STUBS de Android (sintaxe/tipos do meu código). NÃO compilado com o SDK/Gradle real, NÃO rodado em aparelho,
  NÃO visto na tela (layout/tema/ícone). O menu ficou com 7 itens: em telas muito baixas o último pode ficar apertado.
- Não prova que o `python http.server` é a causa dos celulares quebrados. Use "Exportar relatório" num celular com defeito.

## Atualização (video do celular que não passava na correção)
Vídeo: a verificação apontou 30 "ausentes" = `gta3.dxt.dat/.tmb/.toc`, `gta3.pvr.*`: texturas de OUTRA GPU (o aparelho usa ETC).
O fluxo antigo filtra por GPU (`GameFilePolicy.wantedForDevice`); a minha verificação NÃO filtrava. Corrigido:
- `RepairEngine.forGpu`: só core + GPU do aparelho. GPU lida de `SharedPreferences "com.samp.mobile"/gpu_type` (gravada pelo
  `SplashActivity`, 1 linha nova) ou `installed.json`. GPU desconhecida = só confere o que já existe (nunca baixa textura a esmo).
  O valor só aparece depois de abrir o launcher uma vez com o APK novo.
- Reparo parecia "travado" porque só mostrava "Baixando (1/30)". Agora mostra MB baixados / total, MB/s e barra por byte;
  se ficar 8 s sem receber dados avisa "sem receber dados há N s".
- Download com retomada (Range) quando o servidor responde `Accept-Ranges: bytes`; se ignorar o Range, recomeça do zero.
  O `python http.server` NÃO suporta Range -> com ele nada muda: cada tentativa recomeça.
- Relatório agora traz, por arquivo baixado, tamanho/tempo/MB/s e, nas falhas, quanto tempo levou até falhar.
Arquivos a mais neste pacote: `SplashActivity.java`.
Testes: RepairTest 37 ok (inclui GPU ETC/DXT/PVR/desconhecida, retomada por Range, Range ignorado, progresso por byte), PackTest 88 ok.
NÃO compilado com o SDK real, NÃO rodado em aparelho.

## Teclado: análise antes do jogo (KeyboardGuard)
Sintoma: mesmo com "Keyboard" travado em Configurações, alguns clientes abrem o teclado ANTIGO do jogo (ENG/RUS).
Causas encontradas no código (confirmadas com o INIReader REAL do projeto, vendor/inih):
- O nativo lê `SAMP/settings.ini` UMA vez na partida e usa `GetBoolean("gui","androidkeyboard", false)`: chave ausente, valor 0,
  arquivo vazio ou secao quebrada (`[gui` sem fechar) = teclado antigo. Arquivo AUSENTE = `std::terminate()` (o jogo fecha).
- O `settings.ini` padrão do APK (assets) traz `androidkeyboard = 0`; quem corrige é o `FixedSettings.apply`, que só chama em
  Splash/Configurações/`launchGameNow` e simplesmente retorna se o arquivo não existe ou se o Wini falha.
- Dois dos três caminhos que abrem o jogo (`ServerConnector.connect` e o CONECTAR dos Favoritos) NÃO chamavam o `FixedSettings.apply`.
Correção:
- `util/KeyboardGuard.java`: roda no topo dos 3 caminhos que abrem a SAMP (ANTES de gravar host/porta) e ao abrir o launcher.
  Garante settings.ini (ausente/vazio/quebrado -> guarda como `settings.ini.bad` e restaura o do APK), grava
  `[gui] androidkeyboard = 1` por edição de texto tolerante (`pack/IniFix.java`, não depende do Wini), relê e CONFERE, e lista os
  teclados do Android (padrão + ativos); avisa por Toast se não houver teclado ativo ou o padrão estiver desinstalado. Nunca bloqueia a entrada.
- `settings.cpp`: padrão de `androidkeyboard` passou de `false` para `true` (cinto e suspensório; o launcher já trava em ligado).
- O relatório de CORREÇÕES agora inclui a seção `[Teclado]`.
Testes: `IniFixTest` (36) + conferência com o `INIReader` real compilado em C++ (10 variações: padrão do APK, sem chave, sem seção,
chave repetida, caixa mista, BOM+CRLF, chave em outra seção, comentários, `:`): todas dão androidkeyboard=1.
NÃO testado: aparelho real, SDK/Gradle real (KeyboardGuard compilado só contra stubs), compilação NDK da mudança em settings.cpp (1 linha).
Não prova que ESTA foi a causa nos celulares dos jogadores: peça o relatório (seção [Teclado]) de um que ficou com o teclado antigo.

## Atalho no topo (⚙ SOLUÇÕES DE PROBLEMAS)
Engrenagem pequena no canto superior direito do launcher, com "SOLUÇÕES DE PROBLEMAS" em texto minúsculo embaixo; toque abre a
página CORREÇÕES (`openPage(PAGE_REPAIR)`, o item do menu lateral também fica marcado). Arquivos: `res/layout/activity_main.xml`
(novo `mx_btn_help` dentro de `mx_safe_area`, respeita recortes de tela), `res/values/maximo_repair.xml` (`mx_help_label`),
`MainActivity.java` (clique). Só XML/Java: NÃO visto em aparelho (posição/tamanho do texto, 6sp, podem precisar de ajuste).

## Nick com espaço (visual) -> "_" na conexão
- Na tela "Escolha seu Nick"/"Trocar Nick" o jogador pode digitar COM espaços ("Jose Luis Silva"). O launcher continua mostrando assim (chip "Nick: ...").
  Enquanto digita aparece "No servidor seu nome será: Jose_Luis_Silva".
- Para o jogo/servidor SEMPRE vai com "_": `NickManager.writeToSettings` converte (ponto único de gravação em SAMP/settings.ini) e
  o cliente nativo ainda reforça (`NickWire.h` em `settings.cpp`), caso um settings.ini antigo/editado traga espaços.
- Regras (`util/NickRules.java`): espaços nas pontas somem; vários espaços/tab/NBSP viram UM "_"; "_" digitado fica como está; o limite
  de 20 e o conjunto de caracteres (letras, números, `_ [ ] ( ) $ @ . =`) valem para o nome JÁ convertido.
- Testes: NickRulesTest (19) + `nickwire_check.cpp` (host, C++) passam. NÃO compilado com o SDK/NDK reais, NÃO visto em aparelho.
- Dentro do jogo, nomes de jogadores continuam vindo do servidor (com "_"); mostrar "_" como espaço em nametag/chat/scoreboard seria outra mudança (não feita).
