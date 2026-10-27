# Launcher Máximo RP — nova tela de Servidores (landscape)

## O que mudou
**Tela principal (`activity_main.xml`)**: cabeçalho com a logo + título da página + botão ATUALIZAR; menu lateral
(Início, Servidores, Notícias, Configurações, Comunidade, Sair); painel central (o mesmo `ViewPager`
`fragment_place`); rodapé com música, volume, versão do cliente, estado do servidor e redes sociais.
Tudo é componente Android (`Button`, `TextView`, `SeekBar`, `RecyclerView`...). As imagens (`mx_bg_launcher`,
`mx_bg_character`, `mx_logo`) são só decoração.

**Reaproveitado**: `MainActivity` (mesma lógica de `servers.json` + consulta UDP), `ServersFragment`,
`ServerPagesItemFragment`, `ServerAdapter`, `ServerInformationFragment`, `FavouriteServerAdapter`,
`HomeFragment`, `SettingsFragment` e os IDs existentes (`fragment_place`, `server_recycler`, `server_item_layout`...).

| Arquivo | Alteração |
|---|---|
| `AndroidManifest.xml` | `MainActivity` agora `sensorLandscape` (antes `portrait`) |
| `MainActivity.java` | menu lateral, rodapé, `showPage()`, `refreshAll()`, imersivo + recuo de notch |
| `ServerAdapter.java` | usa o cartão novo `server_card.xml` |
| `ServerInformationFragment.java` | Connect passou a chamar `ServerConnector` (lógica idêntica) |
| `HomeFragment.java` | botão que abre Servidores (links sociais foram para o rodapé) |
| novos | `ServerConnector`, `MusicController`, `SocialLinks`, `NewsFragment` |
| layouts | `activity_main`, `fragment_servers`, `fragment_internet`, `fragment_home`, `fragment_news`, `fragment_settings` (agora em `ScrollView`), novo `server_card` |

Os originais substituídos estão em `backup_originais/launcher_ui_antes/`.

## Comportamento (regras do pedido)
- **Atualizar** baixa o `servers.json` e refaz a consulta UDP real. A lista principal é *substituída* e a de
  favoritos é limpa antes de recarregar: não duplica. O botão fica bloqueado enquanto consulta (destrava sozinho em 15 s).
- **Estado exibido** = resultado real: `ONLINE`, `OFFLINE` (consultou e não respondeu), `ERRO DE CONEXÃO`
  (não conseguiu obter o `servers.json`) ou `VERIFICANDO...`.
- **Jogadores, ping e modalidade** só aparecem quando a consulta devolveu; caso contrário mostra `—`.
- **Conectar** usa `ServerConnector`, cópia fiel do fluxo antigo: grava host/porta em `SAMP/settings.ini`, confere
  os arquivos do jogo e abre `SAMP.class`. Fica desabilitado enquanto não há servidor real (carregando/erro).
  Tocar no cartão abre o diálogo de detalhes original.
- A etapa de **atualização do cliente** continua na `SplashActivity`/`UpdateActivity`, antes de chegar aqui.

## Compilar
1. Android Studio (Hedgehog ou mais novo) com **JDK 17**; abra a pasta `2.10-OpenSource`. O projeto usa Gradle 8.9 e AGP 8.2.1.
2. *Sync Project with Gradle Files*.
3. Linha de comando: `./gradlew assembleDebug` (ou `assembleRelease`). O APK sai em `app/build/outputs/apk/`.
4. **Assine com o keystore do projeto** (já configurado em `app/build.gradle`). O `SignatureChecker` recusa
   APK assinado com outra chave ("Use original launcher!").
5. Instale no aparelho e abra. A tela entra direto em **Servidores**.

## O que você precisa configurar (nada disso foi inventado)
- **Links sociais**: `res/values/social_links.xml` (Discord, YouTube, Instagram, site). Vazio = ícone oculto.
  O menu *Comunidade* abre o Discord (ou o site, se for o único configurado).
- **Servidor/endereço**: `res/values/endpoints.xml` ainda aponta para IP de LAN de desenvolvimento
  (`http://192.168.1.83:8080`). Crie `app/src/release/res/values/endpoints.xml` com a URL real (de preferência HTTPS).
- **Música**: coloque a faixa em `res/raw/launcher_music.mp3` (ou .ogg). Sem o arquivo, play/volume ficam
  desabilitados e o rodapé mostra "Sem faixa de música". Não toca sozinha; pausa ao sair do primeiro plano.
- **Notícias**: a aba mostra estado vazio até existir uma fonte real.
- **Personagem**: `res/drawable-nodpi/mx_bg_character.webp` é uma silhueta provisória. Troque pela arte final
  (PNG/WebP com fundo transparente, mesmo nome, ~1000 px de altura).
- **Logo**: `mx_logo.webp` foi extraída da arte de divulgação. Se tiver o arquivo original em alta, substitua.

## Testar
- Tamanhos: 16:9, 18:9, 20:9 e um aparelho com notch, sempre em landscape.
- Servidor online → chip verde, jogadores/ping reais. Servidor desligado → `OFFLINE`.
  `servers.json` inacessível → `ERRO DE CONEXÃO` e dica para atualizar.
- Toque em ATUALIZAR várias vezes: continua um único cartão.
- Conectar: o jogo (GTA SA + SA-MP) abre e o launcher fecha, como antes.

## Observações
- O botão "ADD SERVER" da aba Servidores era um anúncio do autor original (contato de terceiros); ficou oculto
  (o id foi mantido porque o código o referencia).
- Não foi possível compilar aqui (sem Android SDK/Gradle). Foram verificados: XML bem-formado, sintaxe Java,
  existência de todos os recursos referenciados e os testes de unidade existentes do launcher (12/12).
  A primeira compilação no seu ambiente é a validação final.
