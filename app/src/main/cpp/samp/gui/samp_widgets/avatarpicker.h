#pragma once
// Seletor PADRONIZADO de aparencia (Maximo RP): foto do avatar + moldura do avatar + modelo de fundo da linha (Central).
// Usado no Menu jogador (pagina Personalizar), na Central da Cidade (engrenagem) e na aba Adesivos do inventario.
// Molduras / fundos NAO liberados na conta aparecem com CADEADO e nao podem ser escolhidos (so a administracao libera:
// /liberarmoldura e /liberarlinha no servidor, maximo_avatar.inc). Quem chama so passa a area, a escala e o toque.
class ImGuiRenderer;

namespace AvatarPicker
{
	// altura (em pixels) que o seletor ocupa para a largura w (px) e escala u (px por unidade de desenho)
	float Height(float w, float u);
	// desenha em (x0,y0), largura w. pressed = codigo do HitTest do toque em andamento (0 = nenhum)
	void  Draw(ImGuiRenderer* r, float x0, float y0, float w, float u, double tsec, int pressed);
	// codigo do que esta sob (px,py): 0 nada | 1 botao de foto | 100+i moldura i | 200+i fundo i
	int   HitTest(float x0, float y0, float w, float u, float px, float py);
	// executa o toque (foto: abre a Galeria; moldura/fundo: aplica; bloqueado: mostra o aviso do cadeado)
	void  Activate(int code);
}
