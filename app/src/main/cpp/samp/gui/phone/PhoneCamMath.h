#pragma once
// Celular do Maximo RP - camera da FOTO. Logica pura (sem jogo), testada no PC.
//
// Problema: o aparelho fica no canto da tela e a camera do jogo enquadra o personagem no CENTRO da tela. A foto sai do que
// aparece dentro do visor, entao o assunto ficava fora do visor. Solucao sem mexer no aparelho: recriar a camera do jogo com o
// EIXO ótico girado, de modo que o assunto caia exatamente no CENTRO DO VISOR (projecao fora de eixo). Equivale a apontar a
// camera para um lado/altura diferente do assunto na medida certa.
//
// Modo 3 (selfie): camera na frente do personagem, olhando para o peito/rosto; a distancia sai do tamanho do visor para caber
//                  cabeca e ombros. Modo 2 (traseira): camera na altura dos olhos, olhando na direcao do personagem.
#include <cmath>
#include <algorithm>

namespace Phone
{
	struct CamIn
	{
		int   mode = 2;                      // 2 = traseira, 3 = selfie
		float px = 0, py = 0, pz = 0;        // posicao do personagem
		float heading = 0;                   // rotacao do personagem (rad; frente = (-sin h, cos h))
		float screenW = 0, screenH = 0;      // tela do jogo (px)
		float vfX = 0, vfY = 0, vfW = 0, vfH = 0;   // visor do celular (px); w/h <= 0: sem visor (usa a tela toda)
		float fovDeg = 70.0f;                // FOV horizontal atual do jogo (graus)
	};
	struct CamOut
	{
		bool  ok = false;
		float cx = 0, cy = 0, cz = 0;        // posicao da camera
		float lx = 0, ly = 0, lz = 0;        // ponto para onde olha
		float tx = 0, ty = 0, tz = 0;        // ponto do assunto (para teste/diagnostico)
		float dist = 0;                      // distancia camera-assunto
	};

	inline CamOut ComputePhotoCamera(const CamIn& in)
	{
		CamOut o;
		if (!(in.screenW > 50.0f) || !(in.screenH > 50.0f) || !std::isfinite(in.px + in.py + in.pz + in.heading)) return o;
		float fov = in.fovDeg; if (!(fov > 20.0f && fov < 130.0f)) fov = 70.0f;
		const float tanH = std::tan(fov * 0.5f * 3.14159265f / 180.0f), tanV = tanH * in.screenH / in.screenW;
		const bool hasVf = in.vfW > 8.0f && in.vfH > 8.0f;
		float nx = 0, ny = 0;                                                       // centro do visor em coordenadas normalizadas (-1..1)
		if (hasVf)
		{
			nx = ((in.vfX + in.vfW * 0.5f) - in.screenW * 0.5f) / (in.screenW * 0.5f);
			ny = ((in.vfY + in.vfH * 0.5f) - in.screenH * 0.5f) / (in.screenH * 0.5f);
			nx = std::clamp(nx, -0.95f, 0.95f); ny = std::clamp(ny, -0.95f, 0.95f);
		}
		const float fx = -std::sin(in.heading), fy = std::cos(in.heading);          // frente do personagem
		float dx, dy;                                                               // direcao horizontal camera -> assunto
		float dist;
		if (in.mode == 3)
		{
			o.tx = in.px; o.ty = in.py; o.tz = in.pz + 0.50f;                       // assunto: rosto/peito
			float dw = 0.85f / (2.0f * tanH * (hasVf ? in.vfW / in.screenW : 1.0f));    // largura visivel ~0,85 m (cabeca + ombros)
			float dh = 0.95f / (2.0f * tanV * (hasVf ? in.vfH / in.screenH : 1.0f));    // altura visivel ~0,95 m
			dist = std::clamp(std::max(dw, dh), 1.15f, 4.5f);
			o.cx = o.tx + fx * dist; o.cy = o.ty + fy * dist; o.cz = o.tz;          // na frente do personagem
			dx = -fx; dy = -fy;
		}
		else
		{
			o.cx = in.px + fx * 0.15f; o.cy = in.py + fy * 0.15f; o.cz = in.pz + 0.70f;   // altura dos olhos
			dist = 12.0f;
			o.tx = o.cx + fx * dist; o.ty = o.cy + fy * dist; o.tz = o.cz;          // assunto: ponto a frente
			dx = fx; dy = fy;
		}
		o.dist = dist;
		// assunto aparece em (nx, ny): o eixo da camera fica girado para o lado oposto
		const float a = std::atan(nx * tanH);                                       // giro horizontal (para a esquerda se o assunto vai a direita)
		const float b = std::atan(ny * tanV);                                       // giro vertical (para cima se o assunto vai para baixo)
		const float ca = std::cos(a), sa = std::sin(a);
		const float hx = dx * ca - dy * sa, hy = dx * sa + dy * ca;                 // direcao de olhar (rotacao anti-horaria de 'a')
		const float L = 20.0f;
		o.lx = o.cx + hx * std::cos(b) * L; o.ly = o.cy + hy * std::cos(b) * L; o.lz = o.cz + std::sin(b) * L;
		o.ok = std::isfinite(o.lx + o.ly + o.lz + o.cx + o.cy + o.cz);
		return o;
	}
}
