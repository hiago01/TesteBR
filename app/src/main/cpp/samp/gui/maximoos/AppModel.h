#pragma once
// Maximo OS - modelo de aplicativos: App (ciclo de vida), AppInfo, AppRegistry e AppManager. Logica pura (testada no PC).
//
// Ciclo de vida:  onCreate -> onResume <-> onPause -> onDestroy.   (o app so desenha/recebe toque entre onResume e onPause)
// Regras: UM app em primeiro plano por vez; os demais ficam "pausados" na lista de recentes (maximo kMaxRecents).
// Um app NUNCA conhece ImGui nem o jogo: so Canvas, Metrics e os componentes de Components.h.
#include "Canvas.h"
#include "Components.h"
#include "GestureTracker.h"
#include "ResolutionManager.h"
#include "Theme.h"
#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace MaximoOS
{
	class App
	{
	public:
		virtual ~App() {}
		virtual void onCreate() {}
		virtual void onResume() {}
		virtual void onPause() {}
		virtual void onDestroy() {}
		virtual void onResize(const Metrics&) {}
		virtual void update(float /*dtMs*/) {}
		virtual void draw(Canvas& cv, const Metrics& m, const Rect& content) = 0;
		// true = consumiu o toque
		virtual bool onTouch(const Touch&, const Metrics&, const Rect& /*content*/) { return false; }
		// Botao Voltar (Android Back ou tecla do tablet): true = o app tratou (navegou internamente); false = o sistema fecha o app.
		virtual bool onBack() { return false; }
		// o app pede para ser fechado (volta a tela inicial); o OSManager consulta isso a cada quadro
		bool consumeFinish() { bool f = m_finish; m_finish = false; return f; }
	protected:
		void finish() { m_finish = true; }
	private:
		bool m_finish = false;
	};

	struct AppInfo
	{
		std::string id;                 // identificador estavel (minusculo, sem espaco)
		std::string title;
		Icon        icon = Icon::None;
		Color       accent = Colors::Accent;
		bool        placeholder = false;   // etapa 1: app ainda sem funcionalidade (abre a tela "Em breve")
		std::function<std::unique_ptr<App>()> factory;
	};

	class AppRegistry
	{
	public:
		bool add(const AppInfo& i)
		{
			if (i.id.empty() || !i.factory || find(i.id)) return false;      // ids unicos
			m_apps.push_back(i); return true;
		}
		const AppInfo* find(const std::string& id) const
		{
			for (auto& a : m_apps) if (a.id == id) return &a;
			return nullptr;
		}
		const std::vector<AppInfo>& list() const { return m_apps; }
		size_t size() const { return m_apps.size(); }
		void clear() { m_apps.clear(); }
	private:
		std::vector<AppInfo> m_apps;
	};

	class AppManager
	{
	public:
		static constexpr int kMaxRecents = 6;
		enum class State { Resumed, Paused };

		explicit AppManager(AppRegistry& reg) : m_reg(reg) {}
		~AppManager() { closeAll(); }

		bool open(const std::string& id, const Metrics& m)
		{
			const AppInfo* info = m_reg.find(id);
			if (!info) return false;
			if (m_fg == id) return true;
			pauseForeground();
			auto it = m_inst.find(id);
			if (it == m_inst.end())
			{
				Inst in; in.app = info->factory();
				if (!in.app) return false;
				in.app->onCreate();
				in.app->onResize(m);
				it = m_inst.emplace(id, std::move(in)).first;
			}
			m_mru.erase(std::remove(m_mru.begin(), m_mru.end(), id), m_mru.end());
			m_mru.insert(m_mru.begin(), id);
			m_fg = id;
			it->second.state = State::Resumed;
			it->second.app->onResume();
			trim();
			return true;
		}

		void goHome() { pauseForeground(); m_fg.clear(); }

		// Voltar: 1) o app tenta; 2) senao volta para a tela inicial. false = ja estava na tela inicial.
		bool back()
		{
			if (m_fg.empty()) return false;
			App* a = foreground();
			if (a && a->onBack()) return true;
			goHome();
			return true;
		}

		void close(const std::string& id)
		{
			auto it = m_inst.find(id);
			if (it == m_inst.end()) return;
			if (id == m_fg) { it->second.app->onPause(); m_fg.clear(); }
			it->second.app->onDestroy();
			m_inst.erase(it);
			m_mru.erase(std::remove(m_mru.begin(), m_mru.end(), id), m_mru.end());
		}
		void closeAll() { std::vector<std::string> ids = m_mru; for (auto& i : ids) close(i); m_fg.clear(); }

		// tablet fechado: tudo pausa, nada e destruido (o estado do app e mantido ate o proximo uso)
		void pauseAll() { pauseForeground(); }
		// tablet reaberto: volta ao app em primeiro plano, se havia
		void resumeAll() { if (!m_fg.empty()) { auto it = m_inst.find(m_fg); if (it != m_inst.end() && it->second.state == State::Paused) { it->second.state = State::Resumed; it->second.app->onResume(); } } }

		void resize(const Metrics& m) { for (auto& kv : m_inst) kv.second.app->onResize(m); }

		App* foreground() { auto it = m_inst.find(m_fg); return it == m_inst.end() ? nullptr : it->second.app.get(); }
		const std::string& foregroundId() const { return m_fg; }
		const std::vector<std::string>& recents() const { return m_mru; }
		bool isRunning(const std::string& id) const { return m_inst.count(id) != 0; }
		State stateOf(const std::string& id) const { auto it = m_inst.find(id); return it == m_inst.end() ? State::Paused : it->second.state; }
		size_t instanceCount() const { return m_inst.size(); }

	private:
		struct Inst { std::unique_ptr<App> app; State state = State::Paused; };
		void pauseForeground()
		{
			auto it = m_inst.find(m_fg);
			if (it != m_inst.end() && it->second.state == State::Resumed) { it->second.state = State::Paused; it->second.app->onPause(); }
		}
		void trim()   // descarta o app mais antigo que nao e o primeiro plano
		{
			while ((int)m_mru.size() > kMaxRecents) close(m_mru.back());
		}
		AppRegistry& m_reg;
		std::map<std::string, Inst> m_inst;
		std::vector<std::string> m_mru;      // mais recente primeiro
		std::string m_fg;
	};
}
