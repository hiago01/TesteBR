#include "../../main.h"
#include "../gui.h"
#include "../../net/netgame.h"
#include "celofs.h"
#include "celofs_logic.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <vector>

extern CNetGame* pNetGame;

namespace
{
	std::mutex                g_mx;
	CelOfs::State             g_st;
	CelOfs::Hold              g_hold;
	std::vector<std::string>  g_out;                 // comandos de chat a enviar (descarregados no draw)
	std::atomic<bool>         g_open{ false };
	std::atomic<int>          g_finger{ -1 };

	inline long NowMs()
	{
		using namespace std::chrono;
		return (long)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
	}
	inline void Queue(const std::string& c) { if (!c.empty() && g_out.size() < 64) g_out.push_back(c); }

	// executa a acao de um toque (g_mx ja travado)
	void Perform(const CelOfs::Hit& h, long now)
	{
			switch (h.act)
		{
		case CelOfs::Act::Tab:      Queue(CelOfs::SelectTab(g_st, h.arg)); break;
		case CelOfs::Act::Dec:      Queue(CelOfs::Nudge(g_st, h.arg, -1)); g_hold.begin(h.act, h.arg, now); break;
		case CelOfs::Act::Inc:      Queue(CelOfs::Nudge(g_st, h.arg, +1)); g_hold.begin(h.act, h.arg, now); break;
		case CelOfs::Act::PosStep:  g_st.posStep = (g_st.posStep + 1) % 3; break;
		case CelOfs::Act::RotStep:  g_st.rotStep = (g_st.rotStep + 1) % 3; break;
		case CelOfs::Act::Model:    Queue(CelOfs::SetModel(g_st, h.arg)); break;
		case CelOfs::Act::Save:     Queue(CelOfs::kSaveCmd); break;
		case CelOfs::Act::Close:    g_open.store(false); g_finger.store(-1); g_hold.end(); break;
		default: break;
		}
	}

	void DrawBtn(ImGuiRenderer* r, const CelOfs::R& b, const std::string& label, float fs, bool on, bool danger = false)
	{
		const ImColor bg = on ? ImColor(0, 122, 255, 230) : danger ? ImColor(190, 50, 40, 220) : ImColor(70, 70, 76, 220);
		r->drawRoundedRect(ImVec2(b.x, b.y), ImVec2(b.x + b.w, b.y + b.h), bg, 6.0f * fs / 15.0f, true);
		const ImVec2 ts = r->calculateTextSize(label, fs);
		r->drawText(ImVec2(b.x + (b.w - ts.x) * 0.5f, b.y + (b.h - ts.y) * 0.5f), ImColor(255, 255, 255, 255), label, false, fs);
	}
}

CelOfsPanel::CelOfsPanel() : Widget() {}

bool CelOfsPanel::IsOpen() { return g_open.load(); }
void CelOfsPanel::Open()
{
	{ std::lock_guard<std::mutex> l(g_mx); g_out.clear(); Queue(CelOfs::kSyncCmd); g_hold.end(); }
	g_finger.store(-1); g_open.store(true);
}
void CelOfsPanel::Close() { g_open.store(false); g_finger.store(-1); std::lock_guard<std::mutex> l(g_mx); g_hold.end(); }
void CelOfsPanel::Toggle() { if (g_open.load()) Close(); else Open(); }

bool CelOfsPanel::OnServerLine(const std::string& msg)
{
	if (!CelOfs::IsLine(msg)) return false;
	std::lock_guard<std::mutex> l(g_mx);
	CelOfs::ParseLine(msg, g_st);
	return true;
}

bool CelOfsPanel::HandleRawTouch(int type, int num, int x, int y)
{
	if (!g_open.load()) return false;
	if (type == 2)                                                   // push
	{
		if (g_finger.load() >= 0) return false;
		const ImVec2 sc = ImGui::GetIO().DisplaySize;
		const CelOfs::PanelLayout L = CelOfs::Compute(sc.x, sc.y);
		const CelOfs::Hit h = CelOfs::HitTest(L, (float)x, (float)y);
		if (h.act == CelOfs::Act::None) return false;                // fora do painel: joystick/camera seguem para o jogo
		g_finger.store(num);
		std::lock_guard<std::mutex> l(g_mx);
		Perform(h, NowMs());
		return true;
	}
	if (g_finger.load() != num) return false;                        // move/pop: so do dedo dono
	if (type == 1) { g_finger.store(-1); std::lock_guard<std::mutex> l(g_mx); g_hold.end(); }
	return true;
}

void CelOfsPanel::draw(ImGuiRenderer* r)
{
	if (!g_open.load()) return;
	const ImVec2 sc = ImGui::GetIO().DisplaySize;
	const CelOfs::PanelLayout L = CelOfs::Compute(sc.x, sc.y);
	if (!(L.panel.w > 0)) return;

	std::vector<std::string> send; CelOfs::State st; CelOfs::Hold hold;
	{
		std::lock_guard<std::mutex> l(g_mx);
		if (g_hold.due(NowMs())) Queue(CelOfs::Nudge(g_st, g_hold.arg, g_hold.act == CelOfs::Act::Dec ? -1 : +1));   // segurar + / -: repete
		send.swap(g_out); st = g_st; hold = g_hold;
	}
	if (pNetGame) for (const std::string& c : send) pNetGame->SendChatCommand(c.c_str());

	const float s = L.s, fs = 15.0f * s, fsS = 13.0f * s;
	r->drawRoundedRect(ImVec2(L.panel.x, L.panel.y), ImVec2(L.panel.x + L.panel.w, L.panel.y + L.panel.h), ImColor(0, 0, 0, 190), 12.0f * s, true);
	r->drawRoundedRect(ImVec2(L.panel.x, L.panel.y), ImVec2(L.panel.x + L.panel.w, L.panel.y + L.panel.h), ImColor(255, 255, 255, 90), 12.0f * s, false, 1.5f);
	r->drawText(ImVec2(L.title.x, L.title.y + 2 * s), ImColor(255, 255, 255, 255), "Celular na mao  (/celofs)", false, fs);

	const int sel = std::clamp(st.sel, 0, CelOfs::kModes - 1);
	for (int i = 0; i < CelOfs::kModes; ++i) DrawBtn(r, L.tab[i], CelOfs::ModeName(i), fs, i == sel);

	const CelOfs::ModeVals& mv = st.m[sel];
	for (int a = 0; a < CelOfs::kAxes; ++a)
	{
		const CelOfs::R& row = L.row[a];
		r->drawText(ImVec2(row.x + 2 * s, row.y + (row.h - fsS) * 0.5f - 1 * s), ImColor(230, 230, 230, 255), CelOfs::AxisName(a), false, fsS);
		const bool hd = hold.on && hold.arg == a;
		DrawBtn(r, L.dec[a], "-", fs * 1.2f, hd && hold.act == CelOfs::Act::Dec);
		DrawBtn(r, L.inc[a], "+", fs * 1.2f, hd && hold.act == CelOfs::Act::Inc);
		const std::string v = CelOfs::Fmt(a, mv.v[a]);
		const ImVec2 ts = r->calculateTextSize(v, fs);
		const float vx = L.dec[a].x + L.dec[a].w, vw = L.inc[a].x - vx;
		r->drawText(ImVec2(vx + (vw - ts.x) * 0.5f, row.y + (row.h - ts.y) * 0.5f), ImColor(255, 214, 10, 255), v, false, fs);
	}
	char b[48];
	std::snprintf(b, sizeof(b), "Passo pos: %g m", (double)CelOfs::kPosSteps[std::clamp(st.posStep, 0, 2)]); DrawBtn(r, L.posStep, b, fsS, false);
	std::snprintf(b, sizeof(b), "Passo giro: %g", (double)CelOfs::kRotSteps[std::clamp(st.rotStep, 0, 2)]); DrawBtn(r, L.rotStep, b, fsS, false);
	std::snprintf(b, sizeof(b), "iPhone 17 (%d)", CelOfs::kModels[0]); DrawBtn(r, L.model[0], b, fsS, mv.model == CelOfs::kModels[0]);
	std::snprintf(b, sizeof(b), "iPhone 16 (%d)", CelOfs::kModels[1]); DrawBtn(r, L.model[1], b, fsS, mv.model == CelOfs::kModels[1]);
	DrawBtn(r, L.save, "Salvar", fs, false);
	DrawBtn(r, L.close, "Fechar", fs, false, true);
}
