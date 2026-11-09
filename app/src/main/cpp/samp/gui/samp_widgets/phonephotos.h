#pragma once
// Fotos do celular (adaptador com o jogo): captura da tela (glReadPixels), JPEG pequeno em arquivo, texturas das fotos locais e do Gram.
// Tudo que e pesado (reduzir + codificar + gravar) roda em thread propria; a thread de desenho so le os pixels uma vez por foto.
#include "../phone/PhoneApp.h"
#include <functional>
#include <string>

namespace PhonePhotos
{
	Phone::PhotoApi* Api();
	// thread de desenho, uma vez por quadro, ANTES de o ImGui desenhar: faz a captura pedida e libera texturas antigas
	void FrameHook();
	// baixa uma foto publica do Gram: cb(ok, bytes) volta na thread de desenho
	typedef std::function<bool(const std::string& media, std::function<void(bool, const std::string&)> cb)> Fetcher;
	void SetFetcher(Fetcher f);
}
