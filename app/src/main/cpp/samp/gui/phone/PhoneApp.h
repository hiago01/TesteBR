#pragma once
// Celular - contrato dos apps: PhoneAppDef (como o app aparece e e criado), PhoneContext (servicos compartilhados do aparelho) e
// PhoneSettings (preferencias do aparelho). Um app NUNCA conhece ImGui nem o jogo: so Canvas/Metrics, os componentes e o PhoneContext.
#include "PhoneMetrics.h"
#include "../maximoos/AppModel.h"
#include "../maximoos/StatusBar.h"
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace Phone
{
	using namespace MaximoOS;
	namespace Gram { class Service; }

	// Fotos do aparelho (Camera / Galeria / Gram). Implementado pelo jogo (captura de tela, arquivos, texturas); nos testes de PC e um stub.
	// Chaves de textura: "L:<arquivo>" (foto local) e "R:<id da midia>" (foto do Gram, baixada do servidor).
	struct PhotoTex { uint64_t id = 0; int w = 0, h = 0; };
	struct PhotoApi
	{
		virtual ~PhotoApi() {}
		virtual bool canShoot() = 0;                                   // a captura de tela existe neste jogo
		virtual bool shoot() = 0;                                      // pede uma foto (assincrona); false = ocupado/indisponivel
		virtual bool shooting() = 0;
		virtual int  shotSerial() = 0;                                 // sobe a cada foto concluida
		virtual std::string lastError() = 0;
		virtual std::vector<std::string> list() = 0;                   // fotos locais, mais nova primeiro
		virtual bool read(const std::string& name, std::string& jpeg) = 0;
		virtual bool remove(const std::string& name) = 0;
		virtual bool texture(const std::string& key, PhotoTex& out) = 0;   // false = ainda nao pronta (carregando) ou falhou
		virtual bool failed(const std::string& key) = 0;               // a textura desta chave nao vai ficar pronta
	};

	struct PhoneSettings
	{
		bool airplane = false, wifi = true, sound = true, notif = true;
		bool handsFree = true;            // ligacao com voz: o microfone fica aberto sozinho (sem apertar nada). Desligado = segurar para falar
		int  sizePct = 100;               // tamanho do aparelho: 75 / 100 / 125
	};

	// Estado da ligacao (escrito pelo PhoneNet; os apps so leem). state: 0 livre, 1 chamando, 2 tocando (recebida), 3 em ligacao.
	struct PhoneCall
	{
		int state = 0;
		std::string number;
		bool incoming = false;          // a ligacao atual foi recebida
		bool confirmed = false;         // o servidor ja respondeu ao pedido de ligar
		bool muted = false;             // viva-voz: microfone mudo (botao da tela de ligacao)
		bool held = false;              // segurar para falar: dedo no botao
		bool voice = false;             // audio de voz disponivel nesta ligacao (os dois lados com SampVoice)
		int64_t startMs = 0;            // inicio da ligacao ativa
		int64_t endAtMs = 0;            // tela "encerrada" visivel ate este instante (state 0)
		std::string endText;            // texto da tela "encerrada" (vazio = sem tela)
	};

	// Servicos compartilhados. Os ponteiros/funcoes sao preenchidos pela integracao com o jogo (phone.cpp); nos testes de PC sao stubs.
	struct PhoneContext
	{
		PhoneSettings settings;
		int64_t nowMs = 0;
		std::function<void()> settingsChanged;                                                    // chamado quando o usuario muda algo (persistir)
		std::function<std::string(const std::string&, const std::string&)> getString;             // armazenamento local: chave, padrao
		std::function<void(const std::string&, const std::string&)> setString;
		std::function<StatusInfo()> status;                                                       // relogio/data/bateria
		// ---- ganchos de SERVICO (preenchidos pela integracao com o servidor, Fase 3). Retornam "" em sucesso ou o texto do erro a mostrar. ----
		// Sem gancho = servico indisponivel: o app avisa de forma honesta e NAO simula sucesso.
		std::function<std::string(const std::string& number)> dial;
		std::function<std::string(const std::string& number, const std::string& text)> sendMessage;
		// ---- rede do celular (preenchido pelo PhoneNet) ----
		std::string myNumber;                                                                     // numero do jogador ("" = ainda desconhecido)
		bool networked = false;                                                                   // o servidor respondeu ao celular (servico existe)
		PhoneCall call;
		std::function<void()> answer, hangup;
		std::function<void(bool)> talk;                                                           // ligacao com voz: true = comecou a falar (segurando), false = parou                                                     // ligacao: atender / desligar (recusar / cancelar)
		// ---- navegacao entre apps ----
		bool cameraSelfie = false;                                                                // Camera: modo selfie (camera na frente do personagem)
		PhotoApi* photos = nullptr;                                                               // Camera/Galeria (nullptr = sem suporte; o app avisa)
		Gram::Service* gram = nullptr;                                                            // rede social (nullptr = sem servico)
		std::function<bool(const std::string& appId)> openApp;                                    // preenchido pelo PhoneManager
		std::string launchArg;                                                                    // argumento para o app aberto (ex.: numero); o destino consome em onResume
		void changed() { if (settingsChanged) settingsChanged(); }
		std::string get(const std::string& k, const std::string& def = std::string()) const { return getString ? getString(k, def) : def; }
		void set(const std::string& k, const std::string& v) { if (setString) setString(k, v); }
	};

	struct PhoneAppDef
	{
		std::string id, title;
		Color color = Colors::Accent;
		int   page = 0;                  // pagina da grade (-1 = so no dock)
		int   dock = -1;                 // 0..3
		bool  light = true;              // app com fundo claro (barra de status escura)
		bool  seeThrough = false;        // a tela do app NAO e pintada pelo sistema (a Camera mostra o jogo por tras do aparelho)
		std::function<std::unique_ptr<App>()> factory;
	};
}
