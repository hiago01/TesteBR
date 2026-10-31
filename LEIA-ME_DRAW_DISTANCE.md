# Distancia de renderizacao (menu Maximo RP) - o que estava errado

## Causa comprovada (assembly do libGTASA.so do projeto)
- O jogo so aplica a opcao em `Menu_SwitchOffToGame()` (ao sair do menu de pausa NATIVO):
  `CRenderer::ms_lodDistScale = valor * 0.875 * 0.01 + 0.875`, e `CCamera::Process()` usa esse float para calcular o far clip.
- O menu nativo esta desativado (`MobileMenu::InitForPause` bloqueado em hooks.cpp) e o menu Maximo RP so gravava o numero em
  `MobileSettings::settings[2]`. Resultado: o slider mexia na tabela, mas a escala do motor nunca mudava.

## Correcao (maximomenu.cpp)
- Ao mexer no slider: grava na tabela E escreve `ms_lodDistScale` com a formula acima (aplica na hora).
- Guarda a escolha em SAMP/settings.ini: `[maximo] draw_dist` e `brightness`, e reaplica ao abrir o jogo.
- Primeira vez (sem draw_dist no ini): o slider assume o que o motor usa hoje (escala 1.2 = 37%), sem mudar o visual.
  100% = escala 1.75 (bem mais longe, mais pesado). Para outro padrao, ponha `draw_dist=NN` no settings.ini.

## Voltava para 100% / brilho tambem
- Causa NAO identificada (a tabela nativa e reescrita por varias rotinas do motor). Mitigacao: o menu guarda a escolha e,
  a cada frame, restaura tabela/escala/brilho se algo os desfizer, escrevendo no log: `[MaximoMenu] ... desfeita pelo jogo`.
  Se aparecer essa linha em SAMP/samp_log.txt, me mande o log.

## Verificado
- Formula e constantes (0.875, 0.01) lidas do binario; round-trip valor<->escala 0..100 testado (g++).
- Bloco novo compilado e executado no host com stubs (restaura tabela/escala/brilho; sem log espurio no inicio).
## NAO verificado
- Compilacao com o NDK do projeto, efeito visual no aparelho, e se algo mais desfaz a distancia depois do menu fechado.

# Presets de qualidade grafica (novo)
Gráficos > primeira linha "Qualidade gráfica (preset)": cada toque troca Mínima > Baixa > Média > Alta > Ultra > (volta a Mínima).
Se voce mudar qualquer opcao a mao, a linha mostra "Personalizada".
| Preset | Distancia | Efeitos | Sombras | Reflexos |
| Minima | 20% | 0 | 0 | 0 |
| Baixa  | 37% (padrao do motor) | 1 | 0 | 0 |
| Media  | 60% | 1 | 1 | 1 |
| Alta   | 80% | 2 | 2 | 2 |
| Ultra  | 100% | 3 | 2 | 3 |
NAO mexe em brilho, resolucao nem marcas de derrapagem. Faixas (efeitos 0..3, sombras 0..2, reflexos 0..3) lidas do libGTASA.
Os valores de cada preset sao ESCOLHA MINHA (seguindo os niveis que o jogo usa por aparelho); ajuste a tabela kPresets em maximomenu.cpp.
Verificado (host, g++): cada preset e aplicado e reconhecido; valores fora dos presets = Personalizada; ciclo do toque.
NAO verificado: compilar no NDK, aparencia no aparelho, desempenho real de cada preset, e se "Efeitos visuais" muda algo no motor alem da tabela.
