# Ícones do inventário (banco texdb/icones)

O painel do servidor (celular-service, aba **Ícones dos itens**) escolhe o ícone de cada item de duas formas:
- **ícones do jogo** (os 59 já embutidos no cliente) — não precisa de nada;
- **texturas do banco `texdb/icones`** — imagens suas, convertidas pelo conversor abaixo (nada de PNG no cliente: o jogo lê o banco nativo, igual ao do mapa/skins).

## Gerar o banco
1. Coloque as imagens (PNG/JPG, de preferência quadradas, até 128 px) numa pasta, ex. `icones/`. Subpastas são só organização.
   Nome do arquivo = nome do ícone: minúsculas, `a-z 0-9 _`, até 31 letras (`agua_mineral.png`). Nomes repetidos ou inválidos dão erro.
2. `python3 tools/icon_texdb.py build icones saida` (precisa de `numpy` e `Pillow`; `python3 tools/icon_texdb.py selftest` confere a ferramenta).
3. Saem duas pastas:
   - `saida/texdb/icones/` → vai para o **jogo** (`files/texdb/icones/`). Distribua junto dos arquivos do jogo (launcher/atualização). Sem esta pasta o jogo continua normal e usa o ícone padrão.
   - `saida/painel/` → copie o **conteúdo** para `celular-service/icones/` no servidor (lista `icones.lst` + prévias). O painel passa a oferecer essas texturas.
4. No painel, abra o item → **Escolher ícone** → clique na textura. Os jogadores recebem a mudança sozinhos em até ~30 s.

## Regras
- Textura nova só aparece para quem **já atualizou o jogo** com a pasta `texdb/icones` (os outros veem o ícone genérico até atualizar).
- Para trocar uma imagem já usada, gere de novo com o mesmo nome e redistribua.
- Limite: 64 texturas diferentes em uso ao mesmo tempo, 100 itens personalizados.
- Log do jogo: `[ICONTEXDB] ...` mostra se o banco carregou.
