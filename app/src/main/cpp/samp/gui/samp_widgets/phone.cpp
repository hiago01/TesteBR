#include "../../main.h"
#include "../gui.h"
#include "../../game/game.h"
#include "../../vendor/SimpleIni/SimpleIni.h"
#include "../phone/PhoneApps.h"
#include "phone.h"
#include "maximomenu.h"
#include "maximoos.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <ctime>
#include <map>
#include <mutex>
#include <vector>

extern UI* pUI;
// definido em game/multitouch.cpp: true enquanto o evento em despacho veio de ACTION_CANCEL
extern volatile bool g_bTouchCancel;

using namespace Phone;

namespace
{
	std::atomic<bool> g_wantOpen{ false };
	std::atomic<int>  g_backReq{ 0 };

	struct PendingNotif { std::string title, text, app; int level; };
	std::mutex g_nmx;
	std::vector<PendingNotif> g_pending;

	int64_t NowMs()
	{
		using namespace std::chrono;
		return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
	}

	// ---------------- persistencia: SAMP/settings.ini, secao [celular] (preferencias) e [celular_dados] (texto dos apps) ----------------
	struct Store
	{
		std::map<std::string, std::string> data;
		bool loaded = false, dirty = false;
		int64_t lastSave = 0;
	} g_store;

	std::string IniPath()
	{
		if (!g_pszStorage) return std::string();
		return std::string(g_pszStorage) + "SAMP/settings.ini";
	}

	void LoadPrefs(PhoneContext& c)
	{
		const std::string path = IniPath();
		if (path.empty()) return;              // armazenamento ainda nao pronto: tenta no proximo quadro
		g_store.loaded = true;
		CSimpleIniA ini; ini.SetUnicode();
		if (ini.LoadFile(path.c_str()) < 0) return;
		PhoneSettings& s = c.settings;
		s.airplane = ini.GetLongValue("celular", "aviao", 0) != 0;
		s.wifi     = ini.GetLongValue("celular", "wifi", 1) != 0;
		s.sound    = ini.GetLongValue("celular", "som", 1) != 0;
		s.notif    = ini.GetLongValue("celular", "notificacoes", 1) != 0;
		const long sz = ini.GetLongValue("celular", "tamanho", 100);
		s.sizePct  = (sz == 75 || sz == 100 || sz == 125) ? (int)sz : 100;        // valor invalido -> padrao
		CSimpleIniA::TNamesDepend keys;
		ini.GetAllKeys("celular_dados", keys);
		for (auto& k : keys) { const char* v = ini.GetValue("celular_dados", k.pItem, ""); if (v) g_store.data[k.pItem] = v; }
	}

	void SavePrefs(PhoneContext& c)
	{
		g_store.dirty = false;
		const std::string path = IniPath();
		if (path.empty()) return;
		CSimpleIniA ini; ini.SetUnicode();
		ini.LoadFile(path.c_str());
		const PhoneSettings& s = c.settings;
		ini.SetLongValue("celular", "aviao", s.airplane ? 1 : 0);
		ini.SetLongValue("celular", "wifi", s.wifi ? 1 : 0);
		ini.SetLongValue("celular", "som", s.sound ? 1 : 0);
		ini.SetLongValue("celular", "notificacoes", s.notif ? 1 : 0);
		ini.SetLongValue("celular", "tamanho", s.sizePct);
		for (auto& kv : g_store.data) ini.SetValue("celular_dados", kv.first.c_str(), kv.second.c_str());
		if (ini.SaveFile(path.c_str()) < 0) FLog("[Celular] falha ao salvar %s", path.c_str());
		g_store.lastSave = NowMs();
	}

	PhoneManager& PM()
	{
		static PhoneManager* pm = []()
		{
			PhoneManager* p = new PhoneManager();
			InstallDefaultApps(*p);
			p->context().settingsChanged = []() { g_store.dirty = true; };          // grava ao fechar/depois de alguns segundos (sem I/O por toque)
			p->context().getString = [](const std::string& k, const std::string& d) { auto it = g_store.data.find(k); return it == g_store.data.end() ? d : it->second; };
			p->context().setString = [](const std::string& k, const std::string& v) { g_store.data[k] = v; g_store.dirty = true; };
			p->context().status = []()
			{
				StatusInfo st; time_t t = time(nullptr); struct tm lt; localtime_r(&t, &lt);
				char b[8]; snprintf(b, sizeof(b), "%02d:%02d", lt.tm_hour, lt.tm_min);
				st.clock = b; st.weekday = lt.tm_wday; st.day = lt.tm_mday; st.month = lt.tm_mon + 1; st.year = lt.tm_year + 1900;
				st.signal = 4; st.battery = -1;                         // bateria do Android nao e lida (oculta; nada inventado)
				return st;
			};
			p->setStatusProvider(p->context().status);
			return p;
		}();
		return *pm;
	}

	// Adaptador Canvas -> ImGuiRenderer (mesma ligacao do Maximo OS; o celular nao cria engine grafica propria).
	class ImGuiCanvas : public Canvas
	{
	public:
		explicit ImGuiCanvas(ImGuiRenderer* r) : m_r(r) {}
		~ImGuiCanvas() { while (m_clips-- > 0) m_r->popClipRect(); }

		void fillRect(const Rect& r, Color c, float rad) override
		{
			if (c.a == 0 || r.empty()) return;
			if (rad > 0.5f) m_r->drawRoundedRect(V(r.x, r.y), V(r.right(), r.bottom()), C(c), std::min(rad, std::min(r.w, r.h) * 0.5f), true);
			else m_r->drawRect(V(r.x, r.y), V(r.right(), r.bottom()), C(c), true);
		}
		void strokeRect(const Rect& r, Color c, float rad, float th) override
		{
			if (c.a == 0 || r.empty()) return;
			if (rad > 0.5f) m_r->drawRoundedRect(V(r.x, r.y), V(r.right(), r.bottom()), C(c), std::min(rad, std::min(r.w, r.h) * 0.5f), false, th);
			else m_r->drawRect(V(r.x, r.y), V(r.right(), r.bottom()), C(c), false, th);
		}
		void fillGradientV(const Rect& r, Color top, Color bot) override
		{
			if (r.empty()) return;
			m_r->drawRectFilledMulticolor(V(r.x, r.y), V(r.right(), r.bottom()), C(top), C(top), C(bot), C(bot));
		}
		void fillCircle(Vec2 c, float rad, Color col) override
		{
			if (col.a == 0 || rad <= 0) return;
			ImVec2 p[40]; const int n = poly(p, c, rad);
			m_r->drawConvexPolyFilled(p, n, C(col));
		}
		void strokeCircle(Vec2 c, float rad, Color col, float th) override
		{
			if (col.a == 0 || rad <= 0) return;
			ImVec2 p[40]; const int n = poly(p, c, rad);
			m_r->drawPolyline(p, n, C(col), true, th);
		}
		void line(Vec2 a, Vec2 b, Color c, float th) override { if (c.a) m_r->drawLine(V(a.x, a.y), V(b.x, b.y), C(c), th); }
		void fillTriangle(Vec2 a, Vec2 b, Vec2 c, Color col) override { if (col.a) m_r->drawTriangle(V(a.x, a.y), V(b.x, b.y), V(c.x, c.y), C(col), true); }
		void text(Vec2 p, Color c, const std::string& s, float size) override { if (c.a && !s.empty()) m_r->drawText(V(p.x, p.y), C(c), s, false, size); }
		Vec2 measure(const std::string& s, float size) override { const ImVec2 v = m_r->calculateTextSize(s, size); return { v.x, v.y }; }
		void pushClip(const Rect& r) override { m_r->pushClipRect(V(r.x, r.y), V(r.right(), r.bottom()), true); ++m_clips; }
		void popClip() override { if (m_clips > 0) { m_r->popClipRect(); --m_clips; } }
		void setAlpha(float a) override { m_alpha = std::clamp(a, 0.0f, 1.0f); }
	private:
		static ImVec2 V(float x, float y) { return ImVec2(x, y); }
		ImColor C(Color c) const { return ImColor(c.r, c.g, c.b, (int)std::lround(c.a * m_alpha)); }
		static int poly(ImVec2* p, Vec2 c, float rad)
		{
			const int n = rad < 6 ? 12 : rad < 20 ? 24 : 40;
			for (int i = 0; i < n; ++i) { const float a = 6.2831853f * i / n; p[i] = ImVec2(c.x + std::cos(a) * rad, c.y + std::sin(a) * rad); }
			return n;
		}
		ImGuiRenderer* m_r; float m_alpha = 1.0f; int m_clips = 0;
	};
}

PhoneWidget::PhoneWidget() {}

bool PhoneWidget::IsOpen() { return g_wantOpen.load(); }
void PhoneWidget::Open()
{
	if (MaximoMenu::IsOpen() || MaximoOSWidget::IsOpen()) return;      // nunca dois overlays com captura de toque ao mesmo tempo
	g_wantOpen.store(true);
}
void PhoneWidget::Close() { g_wantOpen.store(false); }
void PhoneWidget::Toggle() { if (g_wantOpen.load()) Close(); else Open(); }

bool PhoneWidget::HandleRawTouch(int type, int num, int x, int y)
{
	return PM().input().handleRaw(type, num, x, y, g_bTouchCancel);
}

bool PhoneWidget::OnBackPressed()
{
	if (!g_wantOpen.load()) return false;
	static int64_t s_last = 0;                    // debounce: onKeyDown e onBackPressed podem disparar no mesmo toque (Android 13+)
	const int64_t now = NowMs();
	if (now - s_last >= 300) { s_last = now; g_backReq.fetch_add(1); }
	return true;
}

void PhoneWidget::PostNotification(const std::string& title, const std::string& text, const std::string& appId, int level)
{
	std::lock_guard<std::mutex> l(g_nmx);
	if (g_pending.size() < 32) g_pending.push_back({ title, text, appId, level });
}

void PhoneWidget::draw(ImGuiRenderer* r)
{
	PhoneManager& pm = PM();
	static int64_t s_prev = 0;
	const int64_t now = NowMs();
	const float dt = s_prev ? (float)std::min<int64_t>(now - s_prev, 100) : 16.0f;
	s_prev = now;

	if (!g_store.loaded) LoadPrefs(pm.context());
	if (g_store.dirty && now - g_store.lastSave > 3000) SavePrefs(pm.context());      // no maximo 1 gravacao a cada 3 s

	if (g_wantOpen.load() && !pm.isOpen()) { const ImVec2 sc = pUI ? pUI->displaySize() : ImVec2(1920, 1080); pm.setScreenSize(sc.x, sc.y); pm.open(); }
	if (!g_wantOpen.load() && pm.isOpen()) pm.close();
	if (!pm.isOpen()) { g_backReq.store(0); return; }
	if (pUI) { const ImVec2 sc = pUI->displaySize(); pm.setScreenSize(sc.x, sc.y); }

	for (int n = g_backReq.exchange(0); n > 0 && pm.isOpen(); --n) pm.back();
	{
		std::lock_guard<std::mutex> l(g_nmx);
		if (pm.context().settings.notif && !pm.context().settings.airplane)
			for (auto& p : g_pending) pm.notifications().post(p.title, p.text, p.app, now, p.level == 2 ? Notification::Alert : p.level == 1 ? Notification::Warn : Notification::Info);
		g_pending.clear();
	}

	{
		ImGuiCanvas cv(r);
		pm.frame(cv, now, dt);
	}
	if (!pm.isOpen())
	{
		g_wantOpen.store(false);                 // o proprio celular se fechou (animacao de saida terminou)
		if (g_store.dirty) SavePrefs(pm.context());
	}
}
