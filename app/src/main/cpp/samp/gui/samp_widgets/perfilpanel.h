#pragma once

#include <vector>
#include "shoppanel.h"      // ShopTouch (mesmo canal de toques da loja)

class ImGuiRenderer;
namespace RakNet { class BitStream; }

// =====================================================================================
//  MENU JOGADOR (perfil) do Maximo RP: tela cheia no layout do video de referencia (titulo "Menu jogador", barra lateral com Inicio / Loja premium /
//  Ranking / Regras / Comandos e funcoes / Configuracoes / Duvidas frequentes). Funcionam Inicio, Ranking e Regras; o resto mostra "Em breve".
//  Fica dentro da tela do jobs.cpp (como a loja, a nota fiscal e a garagem): mesmo canal de toques e mesmos RPCs.
//   servidor -> cliente  RPC 245 op 21 (abre) / op 22 (ranking) / op 23 (regras) / op 24 (fecha)         (ver include maximo_perfil.inc)
//        op 21: u16 numero | str nome | u32 rg | str telefone | str ultima_sessao | str conta_criada | str organizacao | str profissao |
//               i32 dinheiro | u32 mbpontos | u32 vips | u8 emGrupo | u16 chaves(veiculos) | u8 nCat + nCat x u8 idCategoria | u8 nAbas + nAbas x str nomeDaAba
//        op 22: u16 numero | u8 idCategoria | u8 n(<=20) + n x { u32 pos | str nome | u32 valor } | u32 minhaPos (0 = fora) | u32 meuValor
//        op 23: u16 numero | u8 aba | u8 n(<=120) + n x { u8 estilo(0 texto,1 titulo,2 item,3 vazia,4 continuacao de item) | str linha }
//   cliente -> servidor  RPC 246 op 20 + u16 numero (a tela abriu) | op 21 + u16 numero + u8 idCategoria | op 22 + u16 numero + u8 aba | op 23 + u16 numero (fechou)
//  Banner "Painel do Grupo": so aparece se emGrupo = 1; toque = comando /grupo. Banner "Eleve sua experiencia / acessar loja": "Em breve" (loja VIP ainda nao existe).
//  O cliente so desenha e PEDE: quem decide o que o ranking mostra e o servidor (banco), o jogador so escolhe a aba.
//  Banners = texturas "perfil_grupos", "perfil_vip" e "perfil_mb" (moeda) do banco texdb/perfil (PerfilTexDb.h); sem o banco o menu desenha o banner so com cor e texto.
//  Todo o resto (icones, cartoes, tabelas) e desenhado por codigo: nenhum IMG novo.
// =====================================================================================

// definido em jobs.cpp: textura nativa do banner (0 = painel de grupos, 1 = eleve sua experiencia) como ImTextureID; nullptr = sem textura
void* PerfilRankBanner(int catId);      // moldura do ranking: 0 = Titas (4a), 1 = Clube da Coroa (1a), 2 = Magnatas (5a); banco texdb/perfil; nullptr = faixa por codigo
void* PerfilMoneyTexture(int which);    // 0 = dinheiro em maos, 1 = banco, 2 = moeda MB Pontos (banco texdb/perfil; nullptr = sem a textura)
void* PerfilBannerTexture(int which);   // 0 = Painel do Grupo, 1 = eleve sua experiencia, 2 = moeda do MB Pontos
// definido em jobs.cpp: desenha a foto de perfil com a moldura do Central da Cidade (centro cx,cy; raio em pixels). false = nao desenhou
bool PerfilDrawAvatar(ImGuiRenderer* r, float cx, float cy, float radius, int alpha);

namespace PerfilPanel
{
	bool OnOpen(RakNet::BitStream& bs);    // op 21 (depois do byte da op). false = pacote invalido (a tela anterior e mantida)
	bool OnRank(RakNet::BitStream& bs);    // op 22
	bool OnRules(RakNet::BitStream& bs);   // op 23
	void OnOrgTag(RakNet::BitStream& bs);  // op 58: str tag da organizacao do jogador (vazio = sem organizacao); o Inicio desenha o brasao
	void Reset();                          // desconexao / op 24: apaga o menu da tela
	int  Seq();                            // numero da tela (0 = nenhuma)

	// Desenha e processa os toques.
	// answer: 0 nada | 1 Fechar | 2 pediu ranking (arg = id da categoria) | 3 pediu regras (arg = aba) | 4 tocou no banner de grupos (/grupos)
	void Draw(ImGuiRenderer* r, float scrW, float scrH, const std::vector<ShopTouch>& events, int& answer, int& arg);
}
