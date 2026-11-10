#pragma once
// Tipografia SF Pro do celular (MaxOS CEL). So o celular usa estas fontes; o resto do jogo (chat, dialogos...) continua na fonte de sempre.
// Os arquivos ficam na MESMA pasta da fonte do jogo: <armazenamento>/fonts/SFPro-*.ttf (TrueType, subconjunto Latin-1 + pontuacao, ~25 KB cada).
// Faltando um arquivo, aquele papel cai na fonte antiga: nada quebra.
#include "../vendor/imgui/imgui.h"

namespace SfFonts
{
	enum Role { Text = 0, TextMedium, TextSemibold, DisplaySemibold, DisplayBold, RoundedSemibold, Count };
	inline ImFont** table() { static ImFont* t[Count] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr }; return t; }
	inline ImFont* get(int role) { return (role >= 0 && role < Count) ? table()[role] : nullptr; }
	inline const char* fileName(int role)
	{
		static const char* n[Count] = { "SFPro-Text-Regular.ttf", "SFPro-Text-Medium.ttf", "SFPro-Text-Semibold.ttf", "SFPro-Display-Semibold.ttf", "SFPro-Display-Bold.ttf", "SFPro-Rounded-Semibold.ttf" };
		return (role >= 0 && role < Count) ? n[role] : "";
	}
}
