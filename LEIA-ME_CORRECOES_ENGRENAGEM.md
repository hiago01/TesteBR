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
