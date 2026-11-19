# Cabelo 6 — "Tecido caído" (pontas em dente, 2 cores, mexe com o vento) · Máximo RP

Slot novo **hair6** (id de rede **63**; nada dos 62 anteriores mudou). Inspirado no print: **raiz numa cor e PONTAS em outra** (ex.: vermelho → verde),
mas caído, como tecido: uma folha de malha que pende da cabeça, com **pontas em dente** (zigue-zague), e que balança com o vento e com o movimento do ator.

## Como funciona (e por que é diferente do cabelo 5)
- **Tudo é desenhado em tempo real** (RwIm3D, o mesmo método da mangueira/bandeira/cartão 3D): uma **calota** no couro cabeludo + a **folha de tecido** (32 colunas x 10 linhas, 8 dentes).
  As **cores vão por vértice** (gradiente RAIZ→PONTA): **não dependem da cor do material do ped** — que foi justamente o que deixou o cabelo 5 branco.
- **Física própria** (HairPhysics: Verlet, passo fixo 1/60 s, colisão com cabeça e tronco, vento com rajadas): só no cliente, cada jogador simula a cópia que vê.
  Custo medido no PC: ~0,15 ms/quadro por cabelo; limite de 14 cabelos por quadro (o seu primeiro), até 40 m de distância.
- O DFF só ganha o **91º material `fantasy_m_hair6` com 1 triângulo degenerado (invisível)** — existe para o slot funcionar no provador/rede. Sem vértice novo relevante (+3, total 65.262 ≤ 65.535).
- Variantes = comprimento: **1 ombro (0,30 m) · 2 meio das costas (0,46 m) · 3 cintura (0,62 m)**. Grupo exclusivo de cabelo (liga o 6, o 5 e os outros desligam).
- **Provador**: ao escolher o Cabelo 6 o painel de cor ganha as abas **RAIZ | PONTA** (cada uma com R,G,B,A e COR PADRÃO). Rede: a 2ª cor vai no mesmo trecho opcional do RPC 232 com `id | 0x80`.
- Banco de texturas: só 3 texturas **16x16** (`hair/6/1..3`, 432 bytes) entram no `skins` existente (sem IMG novo); servem só para o slot ter variantes (a folha não usa textura).

## O que vai em cada zip (todos com caminhos a partir da raiz)
| Zip | Conteúdo | Onde |
|---|---|---|
| `Cabelo6-Assets-COMPLETO.zip` | `SAMP/skins/skin.dff` (hair5 v3 + hair6) e `texdb/skins/*` (banco COMPLETO, 1244 texturas; **substitua os 6 arquivos juntos**) | raiz / aparelho |
| `Cabelo6-Cliente.zip` | HairPhysics, HairCloth, HairClothMesh (novos) + SkinSlots, LookSlots, ProvadorModel, CorpTex, CustomSkin, provador.cpp + `testes/` + `tools/aplicar_hair6_cliente.py` | raiz do repo |
| `Cabelo6-Servidor.zip` | `qawno/include/maximo_look.inc` e `maximo_look_slots.inc` | raiz do servidor; **recompile o .amx** |

**IMPORTANTE (cliente):** hooks.cpp, chat.cpp e scriptrpc.cpp **não vão inteiros** (para não sobrescrever outras alterações suas). Depois de extrair o zip, na raiz do repo rode:
`python3 tools/aplicar_hair6_cliente.py` — insere 3 ligações pequenas (chamada no RenderEffects, comando /cabelo6, reset na conexão). É idempotente e avisa se alguma âncora não existir.
(Também inclui as correções do cabelo 5 v3: textura preta embutida, tinta padrão branca, log de diagnóstico.)

## Testar no jogo
1. `/cabelo6 teste 2` — força o cabelo no SEU ator (sem servidor). `/cabelo6 teste off` volta.
2. `/cabelo6 cor E95151 2ECC40` — raiz vermelha, ponta verde (RRGGBB).
3. `/cabelo6 vento 90 1.5` — direção em graus (PARA ONDE sopra) e intensidade 0..4; `/cabelo6 deriva off` fixa o vento.
4. `/cabelo6 info` — ativos, custo ms/quadro, resets, estiramento máx. · `/cabelo6 tune lista` — ajustes ao vivo (skullUp, skullR, rootElev, rootRadius, gravity, damping...).
5. Pelo provador: categoria CABELO → Cabelo 6 → variante → abas RAIZ/PONTA → APLICAR. Com `/lookx`: `/lookx set 63 2 <var> <RGB24 dec> <A> <RGB24 ponta> <A ponta>`.
6. Servidor: se usar `variantes.ini`, coloque `hair6=3`.

## Verificado aqui (host, sem aparelho)
- Testes C++20 com ASan/UBSan: **slots/RGBA/protocolo/provador (38 checks, inclui hair6 e 2ª cor), física (27: estabilidade, vento, FPS 20–144, NaN, teleporte, veículo, dentes parado/correndo),
  geometria (32: índices, NaN, calota fora do crânio ≥ 4 mm, testa livre, gradiente raiz→ponta, dentes, folga ≥ 7 mm do crânio nas 3 variantes) e proteção do banco.**
- Banco: `skin_texdb.verify` OK (1244); os 260.722.576 bytes do banco anterior estão intactos (cmp) e só 432 bytes foram acrescentados.
- skin.dff relido do zero: 91 materiais, 65.262 vértices, geometria/ossos do hair5 idênticos ao v3.
- Servidor: `maximo_look.inc` **compila com pawncc 3.10** (0 erros; 64-bit cell só para checar sintaxe — não use o .amx daqui).
- HairCloth.cpp: sintaxe conferida contra stubs das APIs usadas (Hose/Flag/IdCard3D); **não é o compilador Android**.
- Corrigi um bug que achei nos testes: com dentes, as molas laterais puxavam as pontas longas para cima (cabelo ficava 1/3 mais curto). Agora a ponta cai o comprimento todo.

## NÃO verificado (seja cético)
- **Não há NDK aqui**: HairCloth.cpp, provador.cpp, CorpTex.cpp e CustomSkin.cpp **não foram compilados**; **nada foi visto no aparelho**. Rode o build do GitHub Actions e me mande os erros.
- Aparência real (iluminação: o Im3D não é iluminado, uso sombreamento "baked" + cor por vértice; à noite pode parecer claro demais), encaixe na cabeça com a animação real,
  e o desempenho em aparelho fraco. A calota usa a esfera do crânio da simulação (centro 7,5 cm acima do osso 8, raio 11,5 cm): se atravessar/flutuar, ajuste com `/cabelo6 tune skullUp|rootRadius|rootElev`.
- O cabelo de tecido **não** aparece no modelo 271 (male_police.dff não tem o material).
- Tecido ≠ textura: a folha é cor lisa com gradiente; não tem trama de tecido (posso adicionar textura opaca depois, por isso `u,v` já estão nos vértices).
