#pragma once
// Tipografia do velocimetro (VehHud). Tres fontes OFL, na MESMA pasta da fonte do jogo: <armazenamento>/fonts/
//   BarlowCondensed-Black.ttf        numero grande da velocidade
//   BarlowCondensed-BoldItalic.ttf   "KM/H"
//   Rajdhani-Bold.ttf                numeros auxiliares (hodometro)
// Arquivo ausente = o velocimetro usa a fonte de sempre (nada quebra). So o velocimetro escolhe estas fontes.
// Cada papel e "assado" no atlas num tamanho proprio e so com os caracteres que o HUD usa (atlas minusculo).
#include "../vendor/imgui/imgui.h"

namespace VehFonts
{
	enum Role { Speed = 0, Unit, Aux, Count };
	inline ImFont** table() { static ImFont* t[Count] = { nullptr, nullptr, nullptr }; return t; }
	inline ImFont* get(int role) { return (role >= 0 && role < Count) ? table()[role] : nullptr; }
	inline const char* fileName(int role)
	{
		static const char* n[Count] = { "BarlowCondensed-Black.ttf", "BarlowCondensed-BoldItalic.ttf", "Rajdhani-Bold.ttf" };
		return (role >= 0 && role < Count) ? n[role] : "";
	}
	// tamanho (px) em que cada fonte e assada: o HUD desenha menor que isto em telas ate ~1080p, entao nao fica borrado
	inline float bakeSize(int role) { static const float s[Count] = { 128.0f, 40.0f, 48.0f }; return (role >= 0 && role < Count) ? s[role] : 32.0f; }
	// proporcoes medidas nos TTF (unidades/em): altura do algarismo e ascendente, ambas sobre (ascendente - descendente) = o "px" do ImGui
	//   Barlow: digito 700, asc 1000, desc 200 | Rajdhani: digito 643, asc 1050, desc 350
	inline float digitHeight(int role)  { return role == Aux ? 643.0f / 1400.0f : 700.0f / 1200.0f; }
	inline float ascent(int role)       { return role == Aux ? 1050.0f / 1400.0f : 1000.0f / 1200.0f; }
}
