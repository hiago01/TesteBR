# Noticias lidas do site (noticias.json)

A aba Noticias do launcher le `https://maximoroleplay.com.br/noticias.json` (res/values/endpoints.xml: endpoint_news_path).
Para publicar uma noticia nova: edite o arquivo no site, acrescentando um item em "news". Nao precisa de Firebase nem de APK novo.

    { "news": [ { "id": "etapa1", "title": "...", "message": "...", "image": "https://.../x.png",
                  "url": "https://...", "type": "news|update|event", "date": "2026-10-30 23:00" } ] }

- Obrigatorio: "title" ou "message". Opcionais: id, image (banner 1024x512 recomendado), url (abre ao tocar), type, date.
- "date": "yyyy-MM-dd HH:mm" ou "yyyy-MM-dd" (horario de Brasilia). Ou "time" em milissegundos. Sem data o item vai para o fim e nao mostra hora.
- image e url so http/https (outros sao descartados). Maximo 50 itens, mais recentes primeiro; arquivo ate 512 KB.
- O launcher guarda a ultima copia valida: se o site cair, mostra a copia. Se o JSON for invalido, ele e ignorado e a copia anterior continua.
- As notificacoes push recebidas continuam na lista (sem repetir titulo+mensagem iguais as do JSON).
- Imagem: suba em site\noticias\ e use a URL completa no campo image.

## Verificado / nao verificado
Verificado (JVM): NewsParser (11 testes: ordem, datas, body/message, url/imagem seguras, descarte de vazios, array na raiz, erros).
NAO verificado: compilacao Android do NewsFragment/layout/Glide, exibicao no aparelho, cache offline no aparelho.
