# Relatório — Máximo OS, etapa 2 (tela inicial completa)

Entrega: moldura, papel de parede, relógio com data, 4 widgets, grade 6×4 com 2 páginas deslizáveis, indicadores, dock de 8 apps, 31 apps registrados (todos placeholders "Módulo não implementado", exceto Teste), 17 ícones novos. Reutiliza a infraestrutura da etapa 1; integração com o jogo (gui/hooks/main/chat) não mudou.
Compilação: igual à etapa 1 (CMake GLOB). Alterados: `gui/maximoos/{Theme,Components,AppModel,StatusBar,HomeScreen,OSManager,BuiltinApps}.h`, `testes/maximoos/*`. Backups dos arquivos do jogo: no zip da etapa 1.

## Verificado (PC, g++ C++17): 934 verificações, 0 falhas
- Layout em 10 resoluções: tudo dentro da área de conteúdo, dock centralizado e dentro da tela, slots do dock iguais, widgets/grade/dock/indicadores sem sobreposição, ícones cabem nas células.
- Toque: abrir app pela grade e pelo dock (prioridade do dock), troca de página por deslize nos dois sentidos, limite nas pontas, arrasto curto volta, arrasto não abre app, toque nos indicadores.
- Texto visível sempre dentro do clip, fonte ≥ 11 px. Renderização aproximada (PIL) conferida visualmente.
- Etapa 1 anterior continua passando (captura de toque, Voltar, Recentes, notificações).

## NÃO verificado
- Compilação NDK/Gradle desta etapa (os cabeçalhos compilam no PC; o adaptador do jogo não mudou desde que você o testou).
- Aparência com a fonte/ImGui reais, desempenho, deslize e inércia no dedo real, vários resoluções reais de celular.
- Textos longos em fonte real (medidos aqui com largura aproximada).

## Limitações
- Clima, Comunicados e Mídia sem fonte de dados no cliente: mostram "indisponível".
- Calendário mostra só a data do aparelho (sem compromissos).
- Sinal da barra é decorativo; bateria oculta.
