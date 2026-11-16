# 26 skins do gamemode no cliente (artconfig.txt)

## O que instalar (nesta ordem)
1. `Skins-texdb.zip`: extraia DENTRO da pasta `texdb/` do jogo, substituindo `favela.img` e a pasta `favela/` (só entram 26 DFF e 50 texturas novas; nada do que já existia foi mexido). Nenhum IMG novo foi criado: tudo foi embutido no favela.img + banco texdb/favela.
2. `Skins-SAMP.zip`: extraia na pasta `SAMP/`. Traz `skinsnovas.ide` (26 linhas de `peds`) e o `gta.dat` com UMA linha a mais (`IDE SAMP\skinsnovas.IDE`, depois do hqpd). Se você já mexeu no gta.dat depois de 14/11, não substitua: só acrescente essa linha.
3. `Source-skins.zip`: código do cliente (compilar o APK).

## Como funciona
- O jogo só tem modelos de 0 a 19999, então os IDs 20001..20026 do gamemode não existem como modelo. Cada skin ganhou um ID LOCAL único e livre (11754..11779, conferido em `model_ids_livres.txt`): `local = 11754 + (servidor - 20001)`. Tabela completa em `tabela.md`.
- `SkinMap.h` converte o ID quando o servidor entrega a skin (SetSpawnInfo, RequestClass, WorldPlayerAdd, SetPlayerSkin, ShowActor). Se o modelo local faltar, cai na skin base do artconfig (299, 287...), nunca fica inválido.
- Limite de peds do cliente: era 315 e já havia 299 (só 16 livres para 26 skins). Subi para 360 (`ModelInfo.h`).
- Texturas: cada textura única virou `sk<hash>` no banco favela (ETC, máx. 512 px; as de 1024 foram reduzidas), os DFF/TXD das skins originais foram trocados por nomes `sk_*` para não colidir com nada do jogo. Os DFF não foram alterados além do nome das texturas (skin/HAnim preservados).
- `wallzzz` (AddSimpleModel 19379, é uma parede do mapa, não uma skin) NÃO foi instalado.

## Servidor
- Gamemode base (SA-MP 0.3.DL) já usa `SetPlayerSkin(playerid, 20001..20026)`: não precisa mudar nada.
- Servidor Máximo (open.mp): `Servidor-skins.zip` tem `maximo_skins.inc` só com os `#define SKIN_*`. Eu NÃO consegui confirmar se o open.mp envia o ID 20001+ a um cliente 0.3.7 (ele pode trocar pela skin base). Se o jogador aparecer com a skin base, me avise que eu ajusto.

## Não testado
- Nada foi testado no jogo nem compilado (sem NDK aqui). Conferido: banco (303 texturas, hash/decodificação OK, PSNR>24 dB em todas as novas), IMG (151 entradas, todos os DFF com skin+HAnim), IDE e gta.dat.
- Peso: favela.img de 38,5 → 41,6 MB; favela.etc.dat de 12,9 → 15,5 MB.
