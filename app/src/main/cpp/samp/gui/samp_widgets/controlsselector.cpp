#include "../../main.h"
#include "../gui.h"
#include "../../game/game.h"
#include "../../net/netgame.h"
#include "../../net/localplayer.h"
#include "controlmode.h"
#include "maximomenu.h"
#include "controlsselector.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <mutex>

using namespace CtlLogic;

extern CNetGame* pNetGame;
extern CGame*    pGame;
extern UI*       pUI;

namespace
{
	std::mutex            g_mu;
	SelectorState         g_st;
	std::atomic<bool>     g_open{ false };
	std::atomic<bool>     g_flush{ false };
	float                 g_sw = 1920, g_sh = 1080;
	int                   g_finger = -1;
	SelAction             g_pushAct = SelAction::None;
	bool                  g_claimed[4] = { false, false, false, false };

	int64_t NowMs()
	{
		using namespace std::chrono;
		return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
	}

	bool UiBlocked()
	{
		if (pGame && pGame->IsGamePaused()) return true;
		if (MaximoMenu::IsOpen()) return true;
		if (!pUI) return true;
		return (pUI->keyboard()      && pUI->keyboard()->visible())      ||
		       (pUI->dialog()        && pUI->dialog()->visible())        ||
		       (pUI->playertablist() && pUI->playertablist()->visible()) ||
		       (pUI->spawn()         && pUI->spawn()->visible())         ||
		       (pUI->splashscreen()  && pUI->splashscreen()->visible());
	}

	bool InGame()
	{
		if (!pNetGame || !pNetGame->GetPlayerPool()) return false;
		CLocalPlayer* lp = pNetGame->GetPlayerPool()->GetLocalPlayer();
		return lp && lp->GetPlayerPed() && lp->GetPlayerPed()->m_pPed;
	}

	void Finish()       // g_mu travado
	{
		if (g_st.apply)                      ControlMode::Set(g_st.selected);   // grava e aplica na hora (sem reiniciar)
		else if (!ControlMode::Chosen())     ControlMode::MarkChosen();         // VOLTAR na 1a vez: segue classico, nao repete
		g_open.store(false);
	}

	void Circle2(ImGuiRenderer* r, float cx, float cy, float rad, const ImColor& fill, const ImColor& line, float lw)
	{
		constexpr int N = 32;
		ImVec2 p[N];
		for (int i = 0; i < N; i++) { const float a = (float)i * 6.2831853f / (float)N; p[i] = ImVec2(cx + std::cos(a) * rad, cy + std::sin(a) * rad); }
		r->drawConvexPolyFilled(p, N, fill);
		r->drawPolyline(p, N, line, true, lw);
	}

	// previa miniatura do esquema dentro do cartao
	void Preview(ImGuiRenderer* r, const CtlRect& R, bool modern)
	{
		const float w = R.r - R.l, h = R.b - R.t;
		r->drawRect(ImVec2(R.l, R.t), ImVec2(R.r, R.b), ImColor(12, 16, 22, 255), true);
		r->drawRect(ImVec2(R.l, R.t), ImVec2(R.r, R.b), ImColor(70, 80, 90, 255), false, 1.5f);
		const float u = std::min(w, h * 1.7f) * 0.1f;
		const ImColor line(255, 255, 255, 220), dark(25, 25, 25, 200);
		if (modern)
		{
			Circle2(r, R.l + w * 0.2f, R.t + h * 0.66f, u * 1.9f, ImColor(30, 30, 30, 120), line, 2.0f);   // base translucida
			Circle2(r, R.l + w * 0.24f, R.t + h * 0.62f, u * 0.8f, ImColor(235, 235, 235, 200), line, 2.0f); // centro movel
			Circle2(r, R.l + w * 0.86f, R.t + h * 0.62f, u * 1.3f, dark, line, 2.0f);                       // ataque
			Circle2(r, R.l + w * 0.70f, R.t + h * 0.80f, u * 0.85f, dark, line, 2.0f);                      // corrida
			Circle2(r, R.l + w * 0.72f, R.t + h * 0.46f, u * 0.85f, dark, line, 2.0f);                      // pulo
			Circle2(r, R.l + w * 0.88f, R.t + h * 0.86f, u * 0.85f, dark, line, 2.0f);                      // agachar
		}
		else
		{
			Circle2(r, R.l + w * 0.2f, R.t + h * 0.66f, u * 1.7f, ImColor(0, 0, 0, 0), line, 3.0f);         // anel nativo
			Circle2(r, R.l + w * 0.86f, R.t + h * 0.66f, u * 1.1f, dark, line, 2.0f);
			Circle2(r, R.l + w * 0.70f, R.t + h * 0.80f, u * 0.8f, dark, line, 2.0f);
			Circle2(r, R.l + w * 0.88f, R.t + h * 0.38f, u * 0.7f, dark, line, 2.0f);
		}
	}
}

ControlsSelector::ControlsSelector() : Widget() {}

bool ControlsSelector::IsOpen() { return g_open.load(); }

void ControlsSelector::Open()
{
	std::lock_guard<std::mutex> l(g_mu);
	if (g_open.load()) return;
	ControlMode::EnsureLoaded();
	g_st.Open(ControlMode::Get());
	g_finger = -1; g_pushAct = SelAction::None;
	for (auto& c : g_claimed) c = false;
	g_flush.store(true);
	g_open.store(true);
}

void ControlsSelector::Close() { std::lock_guard<std::mutex> l(g_mu); g_open.store(false); }

bool ControlsSelector::ConsumeFlushRequest() { return g_flush.exchange(false); }

bool ControlsSelector::HandleRawTouch(int type, int num, int x, int y)
{
	const bool open = g_open.load();
	const int n = (num >= 0 && num < 4) ? num : -1;
	if (!open)
	{
		if (n >= 0 && g_claimed[n])             // dedo que comecou na tela: ate o UP, nada vaza para o jogo
		{
			if (type == 1) g_claimed[n] = false;
			return true;
		}
		return false;
	}
	std::lock_guard<std::mutex> l(g_mu);
	if (n >= 0) g_claimed[n] = true;
	const SelectorLayout S = MakeSelectorLayout(g_sw, g_sh);
	const SelAction a = SelectorHit(S, (float)x, (float)y);
	if (type == 2) { if (g_finger < 0) { g_finger = num; g_pushAct = a; } }
	else if (g_finger == num && type == 1)
	{
		const SelAction pushed = g_pushAct;
		g_finger = -1; g_pushAct = SelAction::None;
		if (pushed != SelAction::None && pushed == a)            // toque completo (subiu no mesmo alvo)
		{
			g_st.Act(a);
			if (g_st.done) Finish();
		}
	}
	if (type == 1 && n >= 0 && !g_open.load()) g_claimed[n] = false;
	return true;
}

void ControlsSelector::draw(ImGuiRenderer* r)
{
	ControlMode::EnsureLoaded();
	const ImVec2 screen = ImGui::GetIO().DisplaySize;

	// NAO abre mais sozinho no inicio. So abre por pedido: opcao "Sistema de controles" do menu (configuracao) ou o comando /controles.
	if (!g_open.load()) return;

	SelectorState st;
	SelectorLayout S;
	{
		std::lock_guard<std::mutex> l(g_mu);
		g_sw = screen.x; g_sh = screen.y;
		st = g_st;
		S = MakeSelectorLayout(screen.x, screen.y);
	}

	const ImColor cBg(0, 0, 0, 215), cPanel(22, 26, 32, 250), cBlue(40, 120, 220, 255), cGreen(40, 170, 90, 255),
	              cText(255, 255, 255, 255), cDim(170, 178, 186, 255), cCard(32, 38, 46, 255);
	r->drawRect(ImVec2(0, 0), screen, cBg, true);
	r->drawRect(ImVec2(S.panel.l, S.panel.t), ImVec2(S.panel.r, S.panel.b), cPanel, true);

	const float ph = S.panel.b - S.panel.t;
	const float fsT = ph * 0.065f, fsC = ph * 0.05f, fsD = ph * 0.036f, fsB = ph * 0.042f;
	{
		const char* t = "ESCOLHA SEUS CONTROLES";
		const ImVec2 ts = r->calculateTextSize(t, fsT);
		r->drawText(ImVec2((S.panel.l + S.panel.r) * 0.5f - ts.x * 0.5f, S.panel.t + ph * 0.03f), cText, t, false, fsT);
	}

	const char* const kTitle[2] = { "CONTROLE CLÁSSICO", "CONTROLE MODERNO" };
	const char* const kDesc[2]  = { "Interface tradicional do GTA SA Mobile.", "Joystick analógico com controles independentes." };
	for (int i = 0; i < 2; i++)
	{
		const CtlRect& c = S.card[i];
		const bool sel = (st.selected == (i == 0 ? Mode::Classic : Mode::Modern));
		r->drawRect(ImVec2(c.l, c.t), ImVec2(c.r, c.b), cCard, true);
		r->drawRect(ImVec2(c.l, c.t), ImVec2(c.r, c.b), sel ? cGreen : ImColor(70, 80, 90, 255), false, sel ? 4.0f : 1.5f);

		const float pad = (c.r - c.l) * 0.04f;
		const ImVec2 tt = r->calculateTextSize(kTitle[i], fsC);
		r->drawText(ImVec2((c.l + c.r) * 0.5f - tt.x * 0.5f, c.t + pad), cText, kTitle[i], false, fsC);
		const ImVec2 td = r->calculateTextSize(kDesc[i], fsD);
		r->drawText(ImVec2((c.l + c.r) * 0.5f - td.x * 0.5f, c.t + pad + tt.y + pad * 0.3f), cDim, kDesc[i], false, fsD);

		const float pvT = c.t + pad + tt.y + td.y + pad * 1.2f;
		Preview(r, CtlRect{ c.l + pad, pvT, c.r - pad, S.selectBtn[i].t - pad }, i == 1);

		const CtlRect& b = S.selectBtn[i];
		r->drawRect(ImVec2(b.l, b.t), ImVec2(b.r, b.b), sel ? cGreen : cBlue, true);
		const char* bl = sel ? "SELECIONADO" : "SELECIONAR";
		const ImVec2 tb = r->calculateTextSize(bl, fsB);
		r->drawText(ImVec2((b.l + b.r) * 0.5f - tb.x * 0.5f, (b.t + b.b) * 0.5f - tb.y * 0.5f), cText, bl, false, fsB);

		if (sel)    // indicador claro no canto do cartao
		{
			const float rad = ph * 0.035f, cx = c.r - pad - rad, cy = c.t + pad + rad;
			Circle2(r, cx, cy, rad, cGreen, cText, 2.0f);
			r->drawLine(ImVec2(cx - rad * 0.5f, cy), ImVec2(cx - rad * 0.1f, cy + rad * 0.4f), cText, 3.0f);
			r->drawLine(ImVec2(cx - rad * 0.1f, cy + rad * 0.4f), ImVec2(cx + rad * 0.5f, cy - rad * 0.4f), cText, 3.0f);
		}
	}

	auto button = [&](const CtlRect& b, const char* label, const ImColor& col) {
		r->drawRect(ImVec2(b.l, b.t), ImVec2(b.r, b.b), col, true);
		const ImVec2 tb = r->calculateTextSize(label, fsB);
		r->drawText(ImVec2((b.l + b.r) * 0.5f - tb.x * 0.5f, (b.t + b.b) * 0.5f - tb.y * 0.5f), cText, label, false, fsB);
	};
	button(S.back, "VOLTAR", ImColor(80, 88, 98, 255));
	button(S.confirm, "CONFIRMAR", cGreen);
}
