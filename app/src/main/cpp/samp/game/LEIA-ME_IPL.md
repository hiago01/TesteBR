# IPL no cliente Máximo RP — remover objeto/LOD e instalar locais novos

Tudo fica em `Android/data/com.maximo.roleplay/files/`. Log: `samp_log.txt`, prefixo `[IPL]`.
O sistema está sempre ligado (não existe mais `ipl_off.on` nem a chave `ipl` no `diag.ini`).

## 1. Três mecanismos (não confundir)
| Mecanismo | Onde | Serve para |
|---|---|---|
| `RemoveBuildingForPlayer` (RPC do servidor) | script do servidor | tira predio na hora do render, por modelo + raio (`-1` = qualquer predio) |
| `hide` (arquivo) | `texdb/ipl/<ipl_principal>.ipl` ou `SAMP/ipl/` | esconde instância do IPL **em texto** (gta.dat/data/maps), mantendo o índice |
| `replace` / `remove` / `inst` (arquivo) | `texdb/ipl/<nome_stream>.ipl` | altera/remove/acrescenta instância dos IPLs **binários** em streaming (`*_stream*.ipl` do gta3.img) |

## 2. Remover objeto E o LOD dele
O LOD é outro objeto com **modelo próprio** (ex. `lodxxxx`). Remover só o modelo alto deixa o LOD "fantasma" à distância.
- Servidor: chame `RemoveBuildingForPlayer` **duas vezes**: com o id do modelo e com o id do modelo do LOD, mesma posição e raio.
- Arquivo (IPL em texto): uma linha `hide` para o id do objeto e outra para o id do LOD (o cliente troca o modelo por um invisível, 19300, sem mudar o índice dos outros).
- Arquivo (IPL binário): `remove` tira a instância; se o objeto tinha LOD, remova também a instância do LOD (o campo `lod` das demais instâncias não é reindexado).

## 3. Instalar um local novo
1. Coloque `.dff/.txd/.col` do local no jeito que o projeto já carrega modelos (id novo registrado — o IPL só posiciona).
2. Crie `texdb/ipl/<nome_do_ipl_stream>.ipl` (nome = o do IPL do IMG, sem `.ipl`; o `samp_log.txt` lista os nomes reais: `IPL binario carregado: ... nome='...'`).
3. Seção `inst`, uma linha por objeto, formato do IPL texto do GTA:

```
inst
id, nome, interior, x, y, z, rx, ry, rz, rw, lod
end
```
- `lod = -1` para sem LOD; para ter LOD copie o índice de uma instância vizinha existente.
- O modelo precisa existir (`CModelInfo`). Se não existir o cliente **ignora a linha** (e loga) para não crashar.
- Posicione perto da caixa do IPL escolhido (aviso no log se ficar a mais de 400 m).

## 4. Conferir
No `samp_log.txt` procure `[IPL] MISTURA '<nome>'`: mostra `trocadas / removidas / acrescentadas / ignoradas`. `ignoradas > 0` = leia as notas logo abaixo.
