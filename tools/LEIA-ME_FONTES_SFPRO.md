# Tipografia SF Pro do celular (MaxOS CEL)

## Onde fica
Os arquivos ficam na MESMA pasta da fonte do jogo: `<armazenamento do jogo>/fonts/` (a mesma de `FONT_NAME`):

    SFPro-Text-Regular.ttf        texto comum (padrao de TODO o celular)
    SFPro-Text-Medium.ttf         nomes dos apps na tela inicial
    SFPro-Text-Semibold.ttf       relogio da barra, botoes, abas, linhas de Ajustes/listas, titulos de notificacao
    SFPro-Display-Semibold.ttf    titulos, cabecalhos, secoes, estados vazios, relogio grande
    SFPro-Display-Bold.ttf        titulo de destaque (Gram: Entrar / Criar conta)
    SFPro-Rounded-Semibold.ttf    so os contadores redondos (badges)

Arquivo ausente = aquele papel usa a fonte antiga (nada quebra). Chat, dialogos e o resto do jogo NAO mudam.

## Como esta ligado (codigo, em app/src/main/cpp/samp/)
- `gui/sffonts.h`            papeis + nomes dos arquivos (unico lugar que lista as fontes)
- `gui/imguiwrapper.cpp`     `initialize()` carrega os 6 TTF no MESMO atlas do ImGui (so se o arquivo existir)
- `gui/imguirenderer.h`      `swapFont()` troca a fonte so durante um desenho do celular
- `gui/maximoos/Canvas.h`    `FontRole`, `Canvas::setFont()`, `DrawTextAs()` (papel por linha, volta ao padrao)
- `gui/samp_widgets/phone.cpp` e `maximoos.cpp`   adaptadores do Canvas: `text()` e `measure()` usam a fonte do papel
- Papeis aplicados em: `gui/phone/PhoneUI.h`, `PhoneForm.h`, `PhoneManager.h`, `PhoneApps*.h`, `PhoneAppCall.h`, `gui/maximoos/{Components,StatusBar,HomeScreen}.h`

## Como os TTF foram gerados (SF Pro do .dmg)
OTF (CFF) -> TrueType quadratico, subconjunto U+0020-00FF + pontuacao (aspas, travessao, reticencias, marcador, euro, setas), sem layout/hinting.
O ImGui 1.71 do projeto so le TrueType e cada OTF completo tem ~6 MB; cada TTF gerado tem ~25 KB.
Sem cirilico/tailandes: texto digitado nesses alfabetos aparece como `?` no celular (portugues nao e afetado).

Atlas: as 6 fontes entram com OversampleH/V = 1 e so o subconjunto acima (com o padrao 3x1 do ImGui o atlas passava de ~0,5 MB para 16-32 MB de textura + copias).

Teste de PC: `app/src/main/cpp/samp/testes/celular/run_fontes.sh`
