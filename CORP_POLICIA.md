# Policia (skin 271) com o corpo do 7: corps instaladas + comandos de teste

**Status:** **não compilado no Android e não testado no aparelho.** Os trechos puros do `CorpTex.cpp` (caminhos, tabelas e o comando `/cop`, extraídos do arquivo real) compilam e passam no host contra os dados finais: 16 corps listadas, 46 rostos, 22 cores de cabelo, 18 peles, roupas e todas as combinações de `/cop`. O resto do `CorpTex.cpp` (render, RenderWare, RPC) só roda no jogo.

## O que foi instalado (SAMP_skins_policia.zip -> raiz do jogo)
- `SAMP/skins/skin271.dff` = seu `_MasculinaCorp_skin.dff` (bytes idênticos ao `male_police.dff`); `skin.dff` = test34 (modelo 7). O 271 também aceita `_MasculinaCorp_skin.dff`, `male_police.dff` ou `male.dff` (o primeiro que existir).
- `SAMP/skins/pack/corp/<corp>/fantasy_m_cop_<peça>.png`: **16 corps** a partir do `271.zip`: `psp`, `psp_goe`, `psp_negociador`, `psp_transito`, `gnr`, `gnr_negociador`, `gnr_transito`, `goe`, `bombeiros`, `bombeiros_2`, `inem`, `maritima`, `mec`, `municipal`, `training` e `base` (peças base: boot2, hat3, pants1, torso3, torso4, vest3).
- `SAMP/skins/pack/271/<tipo>/<pasta>/<n>.png`: as peças base (tshirts, hat, boot, pants, vest, belt) para o `/cop ... base`.
- `SAMP/skins/pack/corp_extra/`: arquivos que **não têm peça no DFF** (não são carregados). Relatório em `pack/corp_instalado.txt`.
- O pack male completo (`heads`, `hair`, `torso`, `leg`, `feet`, `boot`, `pants`, `tshirts`...) e `textures/272/aparencia/roupa/mapa.txt`.

### Como converti os nomes
O nome do arquivo termina com o material: `blusa_gnr_torso4.png` vira `fantasy_m_cop_torso4.png`. Arquivo com etiqueta (`colete_gnr_negociador_vest3`) vira a variante `gnr_negociador` (corp padrão com esse colete no lugar). Peças com sufixo que **não existe** no DFF ficaram de fora: `torso5`, `torso55`, `hat8`, `hat55`, `hat3333`, `pants2`, `pants4` (corps `cerimonia` inteira, mais um ou dois arquivos de `gnr` e `psp`). Se me disser a qual peça cada uma corresponde, eu incluo.
O DFF tem 15 peças: `belt1 boot1 boot2 hat1-3 pants1 short1 torso1-4 vest1-3`. O `belt` ficou fora das corps: a imagem é um molde de UV (2048x4096, igual ao `template`), não uma textura pronta.

## Instalar
1. Use o projeto `2_10-OpenSource_corp.zip` (já com tudo aplicado) e recompile. `kAutoApply` está `false`: o visual vem do CorpTex, por jogador.
2. **Apague `SAMP/skins/autoexec.txt`** (ele oculta roupas no molde inteiro).
3. Copie o `SAMP_skins_policia.zip` para a raiz do jogo (mescla com `SAMP/skins/`).

## Comandos de teste (conectado ao servidor)
- `/corp 271` veste o 271 no seu ator (`/corp 7` volta ao male normal). `/corp lista`, `/corp psp`, `/corp limpar`, `/corp info`.
- `/cop lista` mostra as peças e as trocas ativas. `/cop vest3 gnr_negociador` (peça de outra corp), `/cop hat2 base 1` (peça base de `pack/271`), `/cop torso4 off`, `/cop limpar`.
- Aparência (igual para 7 e 271): `/pele 1`, `/rosto 1 3`, `/cabelo 1 2`. Roupa do 7: `/camisa 2 3`, `/calca 1 1`, `/bota 1 1`, `/roupa lista`.
- Servidor: `SetPlayerSkin(playerid, 271)` + RPC 231 com `corp="psp"` e o mesmo tom, rosto, estilo e cor do visual civil.

## Limites e hipóteses
- Corp = uma textura por peça; peça sem PNG fica oculta. `pants/2 = short1` (para `/cop short1 base`) e `pants 5-7 = bermudas` são **hipóteses**.
- `/cop` e `/corp 271` são só de teste local; o RPC 231 do servidor sobrescreve.
- Texturas 2048 são reduzidas a 1024 no celular (`kMaxTexSize`).
- Chapéu/óculos/mochila do male normal não são trocados por jogador.
- Testes de host: `tools_host/run_host_tests.sh`. Reinstalar corps de outro pacote: `tools_corp_install.py`.
