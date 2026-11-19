# Cabelo 7 — "Disfarçado Carioca" (low / mid fade + topo cacheado texturizado) · Máximo RP

Slot novo **hair7** (id de rede **64**; os 63 anteriores não mudam). Grupo exclusivo de cabelo (liga o 7, os outros desligam). Sem cor RGBA: **preto natural embutido na textura**.
Variantes (provador/`/lookx`): **1 = Low fade · 2 = Mid fade · 3 = Mid-alto**.

## O que é
- **2 cascas 3D no skin.dff** (92º material `fantasy_m_hair7`, 2.911 triângulos, 100 % no osso da cabeça, como o hair5): conformes ao crânio das 5 cabeças (ray-cast), folga ≥ 1,2 mm.
  **A** = base rente (1,6 mm) onde acontece o DEGRADÊ; **B** = topo com volume (3 mm na borda → ~24 mm na coroa) com ondulação de cachos e borda recortada.
- **Degradê** feito na textura 512x512 por elevação real do texel: tom contínuo do cabelo cheio → barbinha → pele (sem faixa reta), costeleta/“pézinho” com canto definido, linha da testa nítida.
- Textura RGBA4444 (3 variantes, 512 KB cada) **dentro do banco `skins` existente** (`hair/07/1..3`) — sem IMG novo.
- Pasta **`hair/07`** (não `hair/7`): `hair_7_1` colide em 16 bits com `necklace_1_21` no banco.

## skin.dff: vértices duplicados soldados
O skin.dff estava com 65.262/65.535 vértices (sobravam 273). Soldei 4.397 vértices **idênticos** (posição+normal+UV+bones+pesos): agora 62.512 mesmo com o cabelo 7.
Conferido: **os 3 vértices de cada um dos 80.372+ triângulos originais continuam idênticos em todos os atributos** (91 materiais). Some também o aviso “mais de 65000 vértices” do DffPreflight.

## Zips (caminhos a partir da raiz)
| Zip | Conteúdo |
|---|---|
| `Cabelo7-Assets-COMPLETO.zip` | `SAMP/skins/skin.dff` + `texdb/skins/*` (banco COMPLETO, 1247 texturas — **substitua os 6 arquivos juntos**; inclui hair5 v3 e hair6) |
| `Cabelo7-Cliente.zip` | `SkinSlots.h`, `provador.cpp`, `testes/host_test_hair5.cpp` — **aplicar por cima do Cabelo6** |
| `Cabelo7-Servidor.zip` | `qawno/include/maximo_look.inc` e `maximo_look_slots.inc` — **recompile o .amx** |

## Testar
`/skin7 slot hair7 on` / `/skin7 slot hair7 2` (variante) ou Provador → CABELO → “Cabelo 7 · Disfarçado Carioca”. `/lookx set 64 2 <1-3>`.
Servidor com `variantes.ini`: acrescente `hair7=3` (sem o arquivo, aceita 0..1023).

## Verificado no PC (nada testado no aparelho)
- Testes C++20 com ASan/UBSan (slots/protocolo/provador): TUDO OK, inclui hair7. `SkinTexDbCore::CheckBankFiles` (o mesmo do cliente) aprova o banco de 1247. `skin_texdb.verify` OK; texturas novas decodificadas == PNG.
- `DffPreflight::Parse` (o do cliente) lê o skin.dff novo sem erro; só restam avisos antigos (triângulos/materiais).
- `maximo_look.inc` compila no pawncc 3.10 (0 erros; não use o .amx daqui).
- Renders de teste em software (ortográfico) — NÃO são o motor do jogo: brilho/iluminação reais e as animações (andar, correr, mexer a cabeça) só no aparelho.
