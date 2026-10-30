# Linha de diagnóstico (bissecção) - achar qual módulo causa o bug dos jogadores

## Ideia
Começar com TODOS os módulos customizados desligados (cliente "base") e ligar UM por vez. O primeiro que faz o bug voltar é o culpado;
daí se corrige só ele. Isso é feito por `files/SAMP/diag.ini` (modelo em `diag_presets/`), sem recompilar a cada etapa.

## Módulos (chaves do diag.ini)
| chave | o que desliga |
|---|---|
| touch | touch.img (carros/skins customizados) + bypass do MINFO.BIN + registro do IMG |
| skin | /skin7 (CCustomSkin) |
| clothes | CJ real (roupas/tatuagens, RPC) |
| dff | DFF por entrada (texdb/DFF) + hook de GetTexture |
| ipl | IPL por instância (texdb/ipl) |
| col | mistura de COL + guarda do CINFO.BIN |
| anim | animações customizadas (RPC 230, poses.ifp) |
| corptex | textura por corporação (RPC 231) |
| radarskin | radar/minimapa Google |

Sem `diag.ini` = tudo ligado (como hoje). O comando `/diag` mostra o que ficou ligado/desligado. **Mudou o arquivo? Feche e abra o jogo** (os ganchos são instalados na abertura).
Importante: o "base" NÃO é o GTA puro. Continuam ativos o resto do cliente (HUD, inventário, mapa, cartões 3D, dano, etc.) e o servidor Pawn.

## Passo a passo (uma etapa por vez; anotar cada uma)
1. Copie `diag_presets/etapa0_base_tudo_desligado.ini` para `Android/data/com.samp.mobile/files/SAMP/diag.ini`. Abra o jogo, `/diag` deve listar tudo em DESLIGADOS.
2. Reproduza o cenário do bug (mesmo servidor, mesmos jogadores/ação, mesmo tempo) e anote: **bug apareceu? SIM/NÃO**.
   - Se aparecer já na etapa 0: o culpado NÃO é nenhum módulo da lista (olhar servidor, rede/sync, outros hooks).
3. Troque pelo arquivo da próxima etapa (cada uma é acumulativa): +touch → +skin → +clothes → +dff → +ipl → +col → +anim → +corptex → +radarskin. Reinicie o jogo, `/diag` para confirmar, repita o teste.
4. A 1ª etapa em que o bug volta aponta o módulo. Para confirmar, ligue SÓ ele (e nada mais) e teste de novo.
5. Envie: etapa em que apareceu, o `/diag` impresso e o log do cliente (linhas `[DIAG]`, `[TOUCH]`, `[DFF]`, `[SKIN7]`, `[CJ]`, `[RADARSKIN]`).

## Relatório (preencher por etapa)
| etapa | ligados | bug? | observação (o que o jogador via, quem, quando) |
|---|---|---|---|
| 0 | nenhum | | |
| 1 | touch | | |
| 2 | +skin | | |
| 3 | +clothes | | |
| 4 | +dff | | |
| 5 | +ipl | | |
| 6 | +col | | |
| 7 | +anim | | |
| 8 | +corptex | | |
| 9 | +radarskin | | |

## Alternativa só com arquivos (sem o diag.ini)
Tirar do aparelho `texdb/touch.img`, `texdb/DFF/`, `texdb/ipl/`, `SAMP/skins/skin.dff` tem efeito parecido para touch/dff/ipl/skin, mas não desliga os ganchos nem o RPC.

## Não verificado
Compilação Android e execução no aparelho. O leitor do `diag.ini` foi testado no computador (arquivo ausente / `all=0` / chaves individuais). O comportamento de cada módulo desligado no jogo ainda não foi visto.
