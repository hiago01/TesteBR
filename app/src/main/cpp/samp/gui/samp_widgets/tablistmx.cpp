#include "../../main.h"
#include "../gui.h"
#include "../../game/game.h"
#include "../../game/RW/RenderWare.h"
#include "../../net/netgame.h"
#include "../../net/localplayer.h"
#include "../../vendor/stb/stb_image.h"          // implementacao (STB_IMAGE_IMPLEMENTATION) esta em game/sprite2d.cpp
#include "../../vendor/SimpleIni/SimpleIni.h"
#include "hudstate.h"
#include "chat.h"
#include "keyboard.h"
#include "tablistmx_logic.h"
#include "tablistmx_atlas.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <string>

extern CNetGame* pNetGame;
extern CGame*    pGame;
extern UI*       pUI;
extern volatile bool g_bTouchCancel;             // game/multitouch.cpp
bool InventoryIsOpenNow();                       // gui/samp_widgets/inventory.cpp
int  MissoesPendingCount();                      // gui/samp_widgets/missoespanel.cpp: missoes completas para coletar
bool GarageIsOpenNow();                          // gui/samp_widgets/inventory.cpp / jobs.h

namespace
{
	namespace TL = TabLogic;

	std::atomic<int>   g_style{ TL::kDefaultStyle };
	std::atomic<int>   g_posY{ TL::kDefaultPosY };
	std::atomic<int>   g_sel{ TL::kDefaultSel };
	std::atomic<int>   g_lastStyle{ TL::kDefaultStyle };
	std::atomic<bool>  g_avail{ false };
	std::atomic<float> g_sw{ 0.0f }, g_sh{ 0.0f };
	std::atomic<int>   g_finger{ -1 };
	std::atomic<int>   g_startTab{ -1 };
	std::atomic<int>   g_lastTab{ -1 };
	std::atomic<bool>  g_loaded{ false };
	std::mutex         g_cmdMu;
	std::string        g_cmd[TL::kTabs];             // comandos de chat configurados (vazio = nenhum)

	inline int64_t NowMs()
	{
		using namespace std::chrono;
		return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
	}

	std::string IniPath()
	{
		if (!g_pszStorage) return std::string();
		return std::string(g_pszStorage) + "SAMP/settings.ini";
	}

	const char* const kCmdKey[TL::kTabs] = { "cmd_dialogo", "cmd_inventario", "cmd_perfil", "cmd_tarefas", "cmd_ajuda" };

	void EnsureLoaded()
	{
		if (g_loaded.load() || !g_pszStorage) return;
		g_loaded.store(true);
		{ std::lock_guard<std::mutex> l0(g_cmdMu); g_cmd[TL::Perfil] = "/perfil"; g_cmd[TL::Tarefas] = "/missoes"; g_cmd[TL::Ajuda] = "/ajuda"; }      // aba Perfil abre o MENU JOGADOR (a Central da Cidade continua em /central; settings.ini cmd_perfil= troca)
		const std::string path = IniPath();
		CSimpleIniA ini;
		ini.SetUnicode();
		if (ini.LoadFile(path.c_str()) < 0) return;
		const long st = ini.GetLongValue("tablistmx", "estilo", TL::kDefaultStyle);
		g_style.store((st >= 0 && st <= 2) ? (int)st : TL::kDefaultStyle);
		if (g_style.load() != 0) g_lastStyle.store(g_style.load());
		const long py = ini.GetLongValue("tablistmx", "layout", 0) >= 2 ? ini.GetLongValue("tablistmx", "pos", TL::kDefaultPosY) : TL::kDefaultPosY;   // layout antigo (base da tela): usa a nova posicao padrao
		g_posY.store((int)std::min<long>(std::max<long>(py, TL::kMinPosY), TL::kMaxPosY));
		std::lock_guard<std::mutex> l(g_cmdMu);
		for (int i = 0; i < TL::kTabs; i++) g_cmd[i] = TL::SanitizeCommand(ini.GetValue("tablistmx", kCmdKey[i], i == TL::Perfil ? "/perfil" : (i == TL::Tarefas ? "/missoes" : (i == TL::Ajuda ? "/ajuda" : ""))));
		if (g_cmd[TL::Perfil] == "/central") g_cmd[TL::Perfil] = "/perfil";      // settings.ini antigo (padrao da versao anterior): a aba Perfil agora abre o Menu Jogador; a Central abre pelo circulo de pessoas da HUD
	}

	void Save()
	{
		const std::string path = IniPath();
		if (path.empty()) return;
		CSimpleIniA ini;
		ini.SetUnicode();
		ini.LoadFile(path.c_str());                       // se nao existir, cria so esta secao
		ini.SetLongValue("tablistmx", "estilo", g_style.load());
		ini.SetLongValue("tablistmx", "pos", g_posY.load());
		ini.SetLongValue("tablistmx", "layout", 2);
		if (ini.SaveFile(path.c_str()) < 0) FLog("[TABLISTMX] falha ao gravar %s", path.c_str());
	}

	bool InGame()
	{
		if (!pNetGame || !pNetGame->GetPlayerPool()) return false;
		CLocalPlayer* lp = pNetGame->GetPlayerPool()->GetLocalPlayer();
		return lp && lp->GetPlayerPed() && lp->GetPlayerPed()->m_pPed;
	}

	bool Blocked()
	{
		if (!InGame()) return true;
		if (pGame && pGame->IsGamePaused()) return true;
		if (MaximoMenu::IsOpen() || ControlsSelector::IsOpen() || InventoryIsOpenNow() || GarageIsOpenNow()) return true;
		if (OrgPanelWidget::IsOpen() || PhoneWidget::IsOpen() || WorldMapWidget::IsOpen() || Provador::IsOpen()) return true;
		if (JobsUI::IsOpen() || CelOfsPanel::IsOpen() || MaximoOSWidget::IsOpen() || ModernControls::IsEditing()) return true;
		if (Hud::g.hideAll.load() || Hud::g.photoCam.load()) return true;
		if (!pUI) return false;
		return (pUI->keyboard()      && pUI->keyboard()->visible())      ||
		       (pUI->dialog()        && pUI->dialog()->visible())        ||
		       (pUI->playertablist() && pUI->playertablist()->visible()) ||
		       (pUI->spawn()         && pUI->spawn()->visible())         ||
		       (pUI->splashscreen()  && pUI->splashscreen()->visible());
	}

	// ---- atlas (PNG embutido -> raster; mesmo caminho do HUD) ----
	RwRaster* CreateAtlasRaster()
	{
		int w = 0, h = 0, ch = 0;
		unsigned char* px = stbi_load_from_memory(TabAtlas::kPng, (int)TabAtlas::kPngSize, &w, &h, &ch, 4);
		if (!px) { FLog("[TABLISTMX] stbi falhou: %s", stbi_failure_reason()); return nullptr; }
		if (!RwImageCreate || !RwImageAllocatePixels || !RwImageFindRasterFormat || !RwRasterCreate || !RwRasterSetFromImage)
		{
			FLog("[TABLISTMX] funcoes RenderWare indisponiveis");
			stbi_image_free(px);
			return nullptr;
		}
		RwImage* image = RwImageCreate(w, h, 32);
		if (!image || !RwImageAllocatePixels(image))
		{
			if (image) RwImageDestroy(image);
			stbi_image_free(px);
			return nullptr;
		}
		for (int y = 0; y < h; ++y)
			memcpy(image->cpPixels + (size_t)y * image->stride, px + (size_t)y * w * 4, (size_t)w * 4);
		stbi_image_free(px);

		int rw = w, rh = h, rd = 32, rf = 0;
		RwImageFindRasterFormat(image, rwRASTERTYPETEXTURE, &rw, &rh, &rd, &rf);
		rf &= ~(rwRASTERFORMATMIPMAP | rwRASTERFORMATAUTOMIPMAP);
		if (rf & (rwRASTERFORMATPAL8 | rwRASTERFORMATPAL4))
		{
			rf &= ~(rwRASTERFORMATPAL8 | rwRASTERFORMATPAL4 | rwRASTERFORMATPIXELFORMATMASK);
			rf |= rwRASTERFORMAT8888;
			rd = 32;
		}
		if ((rf & rwRASTERFORMATPIXELFORMATMASK) == 0) rf |= rwRASTERFORMAT8888;

		RwRaster* raster = RwRasterCreate(rw, rh, rd, rf);
		if (!raster) { RwImageDestroy(image); return nullptr; }
		if (!RwRasterSetFromImage(raster, image))
		{
			if (RwRasterDestroy) RwRasterDestroy(raster);
			RwImageDestroy(image);
			return nullptr;
		}
		RwImageDestroy(image);
		FLog("[TABLISTMX] atlas %dx%d criado (raster %dx%d fmt=0x%x)", w, h, rw, rh, rf);
		return raster;
	}

	RwRaster* GetAtlas()
	{
		static RwRaster* s_atlas = nullptr;
		static bool s_failed = false;
		if (s_atlas) return s_atlas;
		if (s_failed) return nullptr;
		s_atlas = CreateAtlasRaster();
		if (!s_atlas) s_failed = true;           // nao fica tentando todo quadro; sem atlas a barra nao aparece (e nao captura toque)
		return s_atlas;
	}

	void DrawSprite(ImGuiRenderer* r, RwRaster* tex, const TabAtlas::Rect& s, const ImVec2& a, const ImVec2& b, const ImColor& tint)
	{
		const float iw = 1.0f / (float)TabAtlas::kWidth, ih = 1.0f / (float)TabAtlas::kHeight;
		r->drawImage(a, b, (ImTextureID)tex, tint, ImVec2(s.x * iw, s.y * ih), ImVec2((s.x + s.w) * iw, (s.y + s.h) * ih));
	}

	// Acao da aba (thread do jogo)
	void Activate(int tab)
	{
		static int64_t s_last = 0;
		const int64_t now = NowMs();
		if (now - s_last < 300) return;
		s_last = now;
		g_sel.store(tab);
		if (tab == TL::Dialogo && ChatAllowed() && pUI && pUI->keyboard() && pUI->chat())      // Dialogo = abre o teclado do chat (o toque no texto do chat nao abre mais)
		{
			std::string c0; { std::lock_guard<std::mutex> l(g_cmdMu); c0 = g_cmd[tab]; }
			if (c0.empty()) { pUI->keyboard()->show(pUI->chat()); return; }
		}
		std::string cmd;
		{ std::lock_guard<std::mutex> l(g_cmdMu); cmd = g_cmd[tab]; }
		if (!cmd.empty())
		{
			if (pNetGame) pNetGame->SendChatCommand(cmd.c_str());
			return;
		}
		if (tab == TL::Inventario) InventoryUI::RequestOpen();   // /inv (so envia se o inventario ainda nao estiver aberto)
		// Dialogo/Perfil/Tarefas/Ajuda: sem comando configurado = so marca a aba
	}
}

TabListMX::TabListMX() : Widget() {}

void TabListMX::draw(ImGuiRenderer* r)
{
	EnsureLoaded();
	const int style = g_style.load();
	const ImVec2 screen = ImGui::GetIO().DisplaySize;
	g_sw.store(screen.x); g_sh.store(screen.y);

	if (style == 0 || Blocked()) { g_avail.store(false); g_finger.store(-1); return; }
	RwRaster* tex = GetAtlas();
	if (!tex) { g_avail.store(false); g_finger.store(-1); return; }

	const int sel = g_sel.load();
	const TL::BarLayout L = TL::Compute(style, screen.x, screen.y, g_posY.load(), sel);
	if (L.style == 0) { g_avail.store(false); return; }
	g_avail.store(true);

	const float k = L.k;
	const int pressed = (g_finger.load() >= 0) ? g_startTab.load() : -1;
	const ImColor white(255, 255, 255, 255);
	const float lw = std::max(2.0f, 3.0f * k);

	if (style == 1)
	{
		const float H = L.bar.h();
		for (int pi = 0; pi < 3; pi++)                     // grupo + Dialogo + Ajuda: cada um na sua pilula
		{
			const TL::Rect& pl = L.pill[pi];
			r->drawRoundedRect(ImVec2(pl.l, pl.t), ImVec2(pl.r, pl.b), ImColor(0, 0, 0, 205), H * 0.5f, true);
			r->drawRoundedRect(ImVec2(pl.l, pl.t), ImVec2(pl.r, pl.b), white, H * 0.5f, false, lw);
		}
		for (int i = 0; i < L.nSep; i++)
			r->drawLine(ImVec2(L.sepX[i], L.sepT), ImVec2(L.sepX[i], L.sepB), white, lw);
		// caixa da aba escolhida (cobre o pedaco da pilula e do separador atras dela)
		r->drawRoundedRect(ImVec2(L.box.l, L.box.t), ImVec2(L.box.r, L.box.b), ImColor(0, 0, 0, 240), 14.0f * k, true);
		r->drawRoundedRect(ImVec2(L.box.l, L.box.t), ImVec2(L.box.r, L.box.b), white, 14.0f * k, false, lw);
		for (int i = 0; i < TL::kTabs; i++)
		{
			const TabAtlas::Rect& s = TabAtlas::kS1[i];
			const float w = s.w * k, h = s.h * k;
			const float cx = (L.cell[i].l + L.cell[i].r) * 0.5f;
			const float y = L.cell[i].t + 10.0f * k;       // recorte do atlas comeca 10 px (ref.) abaixo do topo da pilula
			const ImColor tint = (i == pressed) ? ImColor(255, 220, 90, 255) : white;
			DrawSprite(r, tex, s, ImVec2(cx - w * 0.5f, y), ImVec2(cx + w * 0.5f, y + h), tint);
		}
	}
	else
	{
		{
			const TabAtlas::Rect& s = TabAtlas::kSep2;
			const float w = s.w * k, h = s.h * k;
			for (int i = 0; i < L.nSep; i++)
				DrawSprite(r, tex, s, ImVec2(L.sepX[i] - w * 0.5f, L.sepT), ImVec2(L.sepX[i] + w * 0.5f, L.sepT + h), white);
		}
		float maxH = 0.0f;
		for (int i = 0; i < TL::kTabs; i++) maxH = std::max(maxH, TabAtlas::kS2[i].h * k);
		for (int i = 0; i < TL::kTabs; i++)
		{
			const TabAtlas::Rect& s = TabAtlas::kS2[i];
			const float sc = (i == sel ? 1.12f : 1.0f) * (i == pressed ? 0.94f : 1.0f);
			const float w = s.w * k * sc, h = s.h * k * sc;
			const float cx = (L.cell[i].l + L.cell[i].r) * 0.5f;
			const float bottom = (L.cell[i].t + L.cell[i].b) * 0.5f + maxH * 0.5f;      // todos os icones com a BASE alinhada (rotulos na mesma linha)
			const ImColor tint = (i == pressed) ? ImColor(255, 220, 90, 255) : (i == sel ? white : ImColor(255, 255, 255, 215));
			DrawSprite(r, tex, s, ImVec2(cx - w * 0.5f, bottom - h), ImVec2(cx + w * 0.5f, bottom), tint);
		}
	}
	// aviso na aba Tarefas: quantas missoes estao COMPLETAS esperando para coletar (numero no canto do icone; some quando zera)
	{
		const int n = MissoesPendingCount();
		if (n > 0)
		{
			const TL::Rect& c = L.cell[TL::Tarefas];
			const float rad = std::max(9.0f, 10.5f * k * 1.35f);
			const ImVec2 ctr(c.r - rad * 1.1f, c.t + rad * 0.9f + (style == 1 ? 4.0f * k : 6.0f * k));
			const std::string t = n > 99 ? std::string("99+") : std::to_string(n);
			r->drawRoundedRect(ImVec2(ctr.x - rad - 1.6f, ctr.y - rad - 1.6f), ImVec2(ctr.x + rad + 1.6f, ctr.y + rad + 1.6f), ImColor(255, 255, 255, 235), rad + 1.6f, true);
			r->drawRoundedRect(ImVec2(ctr.x - rad, ctr.y - rad), ImVec2(ctr.x + rad, ctr.y + rad), ImColor(226, 54, 62, 255), rad, true);
			const float fs = rad * (t.size() > 2 ? 0.95f : 1.25f);
			const ImVec2 ts = r->calculateTextSize(t, fs);
			r->drawText(ImVec2(ctr.x - ts.x * 0.5f, ctr.y - ts.y * 0.5f), ImColor(255, 255, 255, 255), t, true, fs);
		}
	}
}

bool TabListMX::HandleRawTouch(int type, int num, int x, int y)
{
	const int fin = g_finger.load();
	const float fx = (float)x, fy = (float)y;

	if (type == 2)                                                    // push
	{
		if (!g_avail.load() || fin >= 0) return false;
		if (Blocked()) return false;                                  // draw() pode estar parado (pausa/UI oculta): confere ao vivo
		const TL::BarLayout L = TL::Compute(g_style.load(), g_sw.load(), g_sh.load(), g_posY.load(), g_sel.load());
		const int t = TL::HitTab(L, fx, fy);
		if (t < 0) return false;
		g_finger.store(num); g_startTab.store(t); g_lastTab.store(t);
		return true;
	}

	if (fin < 0 || fin != num) return false;                          // move/pop: so do dedo dono
	const TL::BarLayout L = TL::Compute(g_style.load(), g_sw.load(), g_sh.load(), g_posY.load(), g_sel.load());

	if (type == 3) { g_lastTab.store(TL::HitTab(L, fx, fy)); return true; }
	if (type == 1)                                                    // pop
	{
		const int start = g_startTab.load();
		const int end = (x == 0 && y == 0) ? g_lastTab.load() : TL::HitTab(L, fx, fy);   // pop sintetico (0,0): usa a ultima posicao
		g_finger.store(-1); g_startTab.store(-1);
		if (TL::IsTap(start, end, g_bTouchCancel)) Activate(start);
		return true;
	}
	return false;
}

std::string TabListMX::DebugCommand(const std::string& args)
{
	EnsureLoaded();
	TL::State st;
	st.style = g_style.load(); st.lastStyle = g_lastStyle.load(); st.posY = g_posY.load(); st.sel = g_sel.load();
	bool changed = false;
	const std::string reply = TL::Command(args, st, changed);
	if (changed)
	{
		g_style.store(st.style); g_lastStyle.store(st.lastStyle); g_posY.store(st.posY);
		if (st.style == 0) g_finger.store(-1);
		Save();
	}
	return reply;
}
