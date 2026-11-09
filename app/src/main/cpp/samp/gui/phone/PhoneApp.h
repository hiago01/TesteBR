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

namespace Phone
{
	using namespace MaximoOS;

	struct PhoneSettings
	{
		bool airplane = false, wifi = true, sound = true, notif = true;
		int  sizePct = 100;               // tamanho do aparelho: 75 / 100 / 125
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
		std::function<std::unique_ptr<App>()> factory;
	};
}
