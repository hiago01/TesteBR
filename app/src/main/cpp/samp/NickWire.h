// NickWire.h - rede de seguranca no cliente nativo: SA-MP nao aceita espaco no nick.
// O launcher ja grava o nick com "_" no settings.ini; isto garante o mesmo se um settings.ini antigo/editado trouxer espacos.
#pragma once
#include <cstring>

// Tira espacos das pontas e troca cada grupo de espacos/tabs por UM '_' (in-place). Retorna o novo tamanho.
inline size_t NickToWire(char* s)
{
	if (!s) return 0;
	size_t n = strlen(s), out = 0;
	bool pending = false;
	for (size_t i = 0; i < n; i++)
	{
		char c = s[i];
		if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { pending = out > 0; continue; }
		if (pending) { s[out++] = '_'; pending = false; }
		s[out++] = c;
	}
	s[out] = '\0';
	return out;
}
