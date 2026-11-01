#include "../../main.h"
#include "../gui.h"
#include "../../game/game.h"
#include "../maximoos/OSManager.h"
#include "maximoos.h"
#include "maximomenu.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <mutex>
#include <vector>

extern UI* pUI;
// definido em game/multitouch.cpp: true enquanto o evento em despacho veio de ACTION_CANCEL
extern volatile bool g_bTouchCancel;

using namespace MaximoOS;

namespace
{
	OSManager& OS() { static OSManager os; return os; }

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

	// Adaptador Canvas -> ImGuiRenderer (a UNICA ligacao do Maximo OS com o motor grafico).
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

MaximoOSWidget::MaximoOSWidget() {}

bool MaximoOSWidget::IsOpen() { return g_wantOpen.load(); }
void MaximoOSWidget::Open()
{
	if (MaximoMenu::IsOpen()) return;                 // nunca dois overlays com captura de toque ao mesmo tempo
	g_wantOpen.store(true);
	OS().input().setOpen(true);                       // a captura comeca ja (atomico); o desenho abre no proximo quadro
}
void MaximoOSWidget::Close() { g_wantOpen.store(false); }
void MaximoOSWidget::Toggle() { if (g_wantOpen.load()) Close(); else Open(); }

bool MaximoOSWidget::HandleRawTouch(int type, int num, int x, int y)
{
	return OS().input().handleRaw(type, num, x, y, g_bTouchCancel);
}
bool MaximoOSWidget::ConsumeFlushRequest() { return OS().input().consumeFlush(); }

bool MaximoOSWidget::OnBackPressed()
{
	if (!g_wantOpen.load()) return false;
	// debounce: onKeyDown e onBackPressed podem disparar no mesmo toque (Android 13+)
	static int64_t s_last = 0;
	const int64_t now = NowMs();
	if (now - s_last >= 300) { s_last = now; g_backReq.fetch_add(1); }
	return true;
}

void MaximoOSWidget::PostNotification(const std::string& title, const std::string& text, const std::string& appId, int level)
{
	std::lock_guard<std::mutex> l(g_nmx);
	if (g_pending.size() < 32) g_pending.push_back({ title, text, appId, level });
}

void MaximoOSWidget::draw(ImGuiRenderer* r)
{
	OSManager& os = OS();
	static int64_t s_prev = 0;
	const int64_t now = NowMs();
	const float dt = s_prev ? (float)std::min<int64_t>(now - s_prev, 100) : 16.0f;
	s_prev = now;

	// pedidos vindos de outras threads
	if (g_wantOpen.load() && !os.isOpen()) { const ImVec2 sc = pUI ? pUI->displaySize() : ImVec2(1920, 1080); os.setScreenSize(sc.x, sc.y); os.open(); }
	if (!g_wantOpen.load() && os.isOpen()) os.close();
	if (!os.isOpen())
	{
		g_backReq.store(0);
		return;
	}
	if (pUI) { const ImVec2 sc = pUI->displaySize(); os.setScreenSize(sc.x, sc.y); }

	for (int n = g_backReq.exchange(0); n > 0 && os.isOpen(); --n) os.back();
	{
		std::lock_guard<std::mutex> l(g_nmx);
		for (auto& p : g_pending) os.notifications().post(p.title, p.text, p.app, now, p.level == 2 ? Notification::Alert : p.level == 1 ? Notification::Warn : Notification::Info);
		g_pending.clear();
	}

	{
		ImGuiCanvas cv(r);
		os.frame(cv, now, dt);
	}
	if (!os.isOpen()) g_wantOpen.store(false);        // o proprio tablet se fechou (Home na tela inicial / toque fora / Voltar)
}
