#pragma once
// =====================================================================================
//  Botao SAIR DO VEICULO (WIDGET_ENTER_CAR dentro do carro): o desenho nativo (silhueta de carro/moto do jogo) e escondido
//  (alpha 0, o toque continua nativo) e este modulo desenha por cima a silhueta do veiculo ATUAL, no mesmo retangulo:
//  moto, scooter (Faggio/Pizzaboy), carro, van/caminhao, bike/BMX, patinete (ScooterRider), barco, helicoptero, aviao.
//  Efeito igual ao da garagem: ao entrar a silhueta cresce com "pulinho" (easeOutBack, girando); ao sair ela encolhe girando.
//  So desenho: nenhuma logica de entrada/saida do jogo e tocada.
// =====================================================================================
class ImGuiRenderer;
namespace VehicleExit
{
	void Draw(ImGuiRenderer* r);   // 1x por quadro (ModernControls::draw)
	bool NativeHidden();           // true = o botao nativo deve ficar com alpha 0 (lido na thread do jogo, WidgetGta.cpp)
}
