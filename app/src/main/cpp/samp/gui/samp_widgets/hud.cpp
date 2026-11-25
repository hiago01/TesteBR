#include "../../main.h"
#include "../gui.h"
#include "../UiPrefs.h"
#include "../../game/game.h"
#include "../../game/RW/RenderWare.h"
#include "../../net/netgame.h"
#include "../../net/localplayer.h"
#include "../../vendor/stb/stb_image.h"   // implementacao (STB_IMAGE_IMPLEMENTATION) esta em game/sprite2d.cpp
#include "hudatlas.h"
#include "ZoneNames.h"
#include "MiniCity.h"
#include "minimaptap_logic.h"
#include "worldmap.h"
#include "../../game/stamina.h"
#include "../gfxsettings.h"
#include "jobs.h"

#include <atomic>
#include <chrono>
#include <mutex>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

extern CNetGame* pNetGame;
extern UI* pUI;
extern uintptr_t g_libGTASA;

// =====================================================================================
//  Dados
// =====================================================================================

namespace Hud
{
	Data g;

	namespace
	{
		std::mutex g_stMutex;
		Stamina    g_st;
		long long  g_stLastMs = 0;
		int        g_stServerPending = -1;   // energia vinda do servidor, ainda nao consumida pelo StaminaTick
	}

	int StaminaTick(bool sprinting)
	{
		using namespace std::chrono;
		const long long now = duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
		std::lock_guard<std::mutex> lk(g_stMutex);
		const float dt = g_stLastMs == 0 ? 0.0f : (float)(now - g_stLastMs) / 1000.0f;
		g_stLastMs = now;
		if (g_stServerPending >= 0) { g_st.OnServer(g_stServerPending); g_stServerPending = -1; }
		g_st.Tick(sprinting, dt);
		g.energy.store(g_st.Percent());
		return g_st.Percent();
	}

	bool CanSprint()
	{
		std::lock_guard<std::mutex> lk(g_stMutex);
		return g_st.CanSprint();
	}

	// minimapa + nome do local: so o MOTORISTA dentro de um veiculo (desligavel: /hud mapa sempre)
	bool MapAllowed()
	{
		if (!UiPrefs::MapOn()) return false;                 // Configuracoes: minimapa desligado pelo jogador
		if (!g.mapOnlyDriving.load()) return true;
		if (!pNetGame || !pNetGame->GetPlayerPool()) return false;
		CLocalPlayer* lp = pNetGame->GetPlayerPool()->GetLocalPlayer();
		if (!lp) return false;
		CPlayerPed* pp = lp->GetPlayerPed();
		if (!pp || !pp->m_pPed) return false;
		return pp->IsInVehicle() && !pp->IsAPassenger();
	}

	void OnMoneyGiven(int amount)
	{
		g.money.fetch_add(amount);
	}

	void OnMoneyReset()
	{
		g.money.store(0);
	}

	void OnServerPacket(uint8_t flags, uint8_t hunger, uint8_t thirst, uint8_t energy,
						int32_t money, uint16_t slotA, uint16_t slotB, int32_t extra)
	{
		g.visible.store((flags & 1) != 0);

		g.hunger.store(std::min<int>(hunger, 100));
		g.thirst.store(std::min<int>(thirst, 100));
		g.energy.store(std::min<int>(energy, 100));
		{
			std::lock_guard<std::mutex> lk(g_stMutex);
			g_stServerPending = std::min<int>(energy, 100);   // vira o valor local so se mudou (ver Stamina::OnServer)
		}

		if (flags & 2)
		{
			g.money.store(money);
			g.moneyFromServer.store(true);
		}

		g.slotA.store(slotA);
		g.slotB.store(slotB);
		g.extra.store(extra);
	}

	void Reset()
	{
		g.hideAll.store(false);
		g.visible.store(true);
		g.hunger.store(100);
		g.thirst.store(100);
		g.energy.store(100);
		g.accountId.store(0);
		{
			std::lock_guard<std::mutex> lk(g_stMutex);
			g_st.Reset();
			g_stServerPending = -1;
			g_stLastMs = 0;
		}
		g.money.store(0);
		g.moneyFromServer.store(false);
		g.slotA.store(0);
		g.slotB.store(0);
		g.peopleManual.store(false);
		g.extra.store(0);
		g.shop.store(false);
	}

	bool CoordsGet(float& x, float& y, float& z, float& ang, bool& veh)
	{
		if (!pNetGame || !pNetGame->GetPlayerPool()) return false;
		CLocalPlayer* lp = pNetGame->GetPlayerPool()->GetLocalPlayer();
		CPlayerPed* pp = lp ? lp->GetPlayerPed() : nullptr;
		if (!pp || !pp->m_pPed) return false;
		const CVector p = pp->m_pPed->GetPosition();
		x = p.x; y = p.y; z = p.z;
		veh = pp->m_pPed->pVehicle != nullptr;
		float hd = (veh ? pp->m_pPed->pVehicle->GetHeading() : pp->m_pPed->m_fCurrentRotation) * 57.2957795f;
		hd = std::fmod(hd, 360.0f); if (hd < 0.0f) hd += 360.0f;
		ang = hd;
		return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
	}

	std::string DebugCommand(const std::string& args)
	{
		auto split = [](const std::string& s) {
			std::vector<std::string> out;
			size_t i = 0;
			while (i < s.size())
			{
				while (i < s.size() && s[i] == ' ') i++;
				size_t j = i;
				while (j < s.size() && s[j] != ' ') j++;
				if (j > i) out.push_back(s.substr(i, j - i));
				i = j;
			}
			return out;
		};

		const std::vector<std::string> a = split(args);
		char buf[160];

		if (a.empty())
		{
			snprintf(buf, sizeof(buf),
					 "[HUD] %s | fome=%d sede=%d energia=%d dinheiro=%d pessoa=%d/%d extra=%d escala=%.2f",
					 g.visible.load() ? "ligada" : "desligada",
					 g.hunger.load(), g.thirst.load(), g.energy.load(), g.money.load(),
					 g.slotA.load(), g.slotB.load(), g.extra.load(), g.scale.load());
			return buf;
		}

		if (a[0] == "on")  { g.visible.store(true);  return "[HUD] ligada"; }
		if (a[0] == "off") { g.visible.store(false); return "[HUD] desligada"; }

		if (a[0] == "teste")
		{
			// valores do print de referencia
			g.visible.store(true);
			g.hunger.store(80);
			g.thirst.store(60);
			g.energy.store(45);
			g.money.store(67214);
			g.moneyFromServer.store(true);
			g.slotA.store(0);
			g.slotB.store(10);
			g.peopleManual.store(true);
			g.extra.store(24600);
			return "[HUD] valores de teste aplicados (use /hud reset para voltar)";
		}

		if (a[0] == "reset") { Reset(); return "[HUD] resetada"; }

		if (a[0] == "mapa")
		{
			if (a.size() >= 2 && a[1] == "sempre") { g.mapOnlyDriving.store(false); return "[HUD] minimapa e nome do local: SEMPRE visiveis"; }
			if (a.size() >= 2 && a[1] == "carro")  { g.mapOnlyDriving.store(true);  return "[HUD] minimapa e nome do local: so o MOTORISTA no veiculo"; }
			return g.mapOnlyDriving.load() ? "[HUD] mapa: so motorista (use /hud mapa sempre)" : "[HUD] mapa: sempre (use /hud mapa carro)";
		}

		if (a[0] == "luzes")
		{
			if (a.size() >= 2 && a[1] == "off") { g.lights.store(false); return "[HUD] luzes de natal desligadas"; }
			if (a.size() >= 2 && a[1] == "on")  { g.lights.store(true);  return "[HUD] luzes de natal ligadas"; }
			return "[HUD] uso: /hud luzes on|off";
		}

		if (a[0] == "escala" && a.size() >= 2)
		{
			float v = (float)atof(a[1].c_str());
			if (v < 0.5f) v = 0.5f;
			if (v > 2.5f) v = 2.5f;
			g.scale.store(v);
			snprintf(buf, sizeof(buf), "[HUD] escala = %.2f", v);
			return buf;
		}

		if (a[0] == "set" && a.size() >= 3)
		{
			const int v = atoi(a[2].c_str());
			if (a[1] == "fome")     { g.hunger.store(std::clamp(v, 0, 100)); return "[HUD] fome ok"; }
			if (a[1] == "sede")     { g.thirst.store(std::clamp(v, 0, 100)); return "[HUD] sede ok"; }
			if (a[1] == "energia")  { std::lock_guard<std::mutex> lk(g_stMutex); g_st.value = (float)std::clamp(v, 0, 100); g_st.tired = g_st.value <= 0.0f; g.energy.store(std::clamp(v, 0, 100)); return "[HUD] energia ok"; }
			if (a[1] == "dinheiro") { g.money.store(v); g.moneyFromServer.store(true); return "[HUD] dinheiro ok"; }
			if (a[1] == "extra")    { g.extra.store(v); return "[HUD] extra ok"; }
			if (a[1] == "pessoa")
			{
				g.slotA.store(v);
				g.slotB.store(a.size() >= 4 ? atoi(a[3].c_str()) : 0);
				g.peopleManual.store(true);
				return "[HUD] pessoa ok (valor fixo; /hud reset volta a mostrar os jogadores online)";
			}
		}

		return "[HUD] uso: /hud | on | off | teste | reset | escala <0.5-2.5> | luzes on|off | mapa carro|sempre | "
			   "set fome|sede|energia <0-100> | set dinheiro|extra <n> | set pessoa <a> [max]";
	}
}

// =====================================================================================
//  Textura (atlas embutido: logo + icones)
// =====================================================================================

namespace
{
	RwRaster* s_atlas      = nullptr;
	bool      s_atlasFailed = false;

	// Decodifica o PNG embutido e cria o raster (mesmo caminho do CustomSkin / radar disc).
	RwRaster* CreateRasterFromPng(const unsigned char* png, size_t pngSize)
	{
		int w = 0, h = 0, ch = 0;
		unsigned char* px = stbi_load_from_memory(png, (int)pngSize, &w, &h, &ch, 4);
		if (!px)
		{
			FLog("[HUD] stbi falhou: %s", stbi_failure_reason());
			return nullptr;
		}

		if (!RwImageCreate || !RwImageAllocatePixels || !RwImageFindRasterFormat ||
			!RwRasterCreate || !RwRasterSetFromImage)
		{
			FLog("[HUD] funcoes RenderWare indisponiveis");
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
		if (!raster)
		{
			RwImageDestroy(image);
			return nullptr;
		}

		if (!RwRasterSetFromImage(raster, image))
		{
			if (RwRasterDestroy) RwRasterDestroy(raster);
			RwImageDestroy(image);
			return nullptr;
		}
		RwImageDestroy(image);

		FLog("[HUD] atlas %dx%d criado (raster %dx%d fmt=0x%x)", w, h, rw, rh, rf);
		return raster;
	}

	RwRaster* CreateAtlasRaster() { return CreateRasterFromPng(HudAtlas::kPng, HudAtlas::kPngSize); }

	RwRaster* GetAtlas()
	{
		if (s_atlas) return s_atlas;
		if (s_atlasFailed) return nullptr;

		s_atlas = CreateAtlasRaster();
		if (!s_atlas) s_atlasFailed = true;   // nao fica tentando todo frame; a HUD segue sem icones
		return s_atlas;
	}
}

// =====================================================================================
//  Tema / proporcoes
//  Tudo em "pixels do print de referencia" (1672x941) e multiplicado por S = altura/941.
// =====================================================================================

namespace
{
	const ImColor kRed    (222,  22,  30, 255);   // vida
	const ImColor kCyan   ( 36, 160, 222, 255);   // colete
	const ImColor kYellow (252, 184,  22, 255);   // fome
	const ImColor kOrange (244, 122,  18, 255);   // energia
	const ImColor kBlue   ( 24, 104, 238, 255);   // sede
	const ImColor kGreen  ( 34, 170,  34, 255);   // dinheiro
	const ImColor kBlack  ( 14,  14,  14, 255);   // pessoa
	const ImColor kBurst  (205,  24,  32, 255);   // estrela
	const ImColor kWhite  (255, 255, 255, 255);

	constexpr float kMargin   = 30.0f;    // distancia da borda direita
	constexpr float kTop      = 8.0f;     // distancia do topo (logo)
	constexpr float kLogoH    = 66.0f;    // altura do logo
	constexpr bool  kChristmasLights = true;   // chave geral das luzes de natal sobre o logo
	constexpr float kLogoTop  = 8.0f;     // distancia do topo da tela ao logo (logo centralizado na horizontal)
	constexpr float kLogoGap  = 8.0f;     // logo -> linha 1
	constexpr float kRaise    = 22.0f;    // sobe os circulos/pilulas (ficam ao lado do logo, nao embaixo)
	constexpr float kRadius   = 20.0f;    // raio dos circulos
	constexpr float kCircGap  = 11.0f;    // espaco entre circulos da linha 1
	constexpr float kRowGap   = 9.0f;     // entre linhas (borda -> borda dos circulos)
	constexpr float kPillH    = 28.0f;    // altura da pilula
	constexpr float kPillPad  = 11.0f;    // espaco dentro da pilula (esq/dir)
	constexpr float kItemGap  = 10.0f;    // entre os blocos (circulo+pilula) da linha 2
	constexpr float kCircPill = 7.0f;     // circulo -> pilula
	constexpr float kFont     = 15.5f;    // tamanho do texto das pilulas
	constexpr float kRingGap  = 2.6f;     // folga entre circulo e anel de nivel
	constexpr float kRingW    = 2.2f;     // espessura do anel
	constexpr float kLowPct   = 20.0f;    // abaixo disso o circulo pisca
	constexpr bool  kShowRing = true;     // anel de nivel em volta dos 5 circulos de status

	constexpr float kPi = 3.14159265f;

	// Poligono convexo -> circulo
	void FillCircle(ImGuiRenderer* r, const ImVec2& c, float rad, const ImColor& col)
	{
		constexpr int N = 40;
		ImVec2 p[N];
		for (int i = 0; i < N; i++)
		{
			const float a = 2.0f * kPi * (float)i / (float)N;
			p[i] = ImVec2(c.x + std::cos(a) * rad, c.y + std::sin(a) * rad);
		}
		r->drawConvexPolyFilled(p, N, col);
	}

	// Arco no sentido horario a partir do topo. frac = 0..1
	void DrawArc(ImGuiRenderer* r, const ImVec2& c, float rad, float frac, const ImColor& col, float thick)
	{
		frac = std::clamp(frac, 0.0f, 1.0f);
		if (frac <= 0.004f) return;

		constexpr int MAXP = 64;
		ImVec2 p[MAXP];

		if (frac >= 0.996f)
		{
			constexpr int N = 48;
			for (int i = 0; i < N; i++)
			{
				const float a = -kPi * 0.5f + 2.0f * kPi * (float)i / (float)N;
				p[i] = ImVec2(c.x + std::cos(a) * rad, c.y + std::sin(a) * rad);
			}
			r->drawPolyline(p, N, col, true, thick);
			return;
		}

		int n = std::clamp((int)(48.0f * frac) + 2, 3, MAXP);
		for (int i = 0; i < n; i++)
		{
			const float a = -kPi * 0.5f + 2.0f * kPi * frac * (float)i / (float)(n - 1);
			p[i] = ImVec2(c.x + std::cos(a) * rad, c.y + std::sin(a) * rad);
		}
		r->drawPolyline(p, n, col, false, thick);
	}

	// Capsula (retangulo com as pontas totalmente arredondadas)
	void FillCapsule(ImGuiRenderer* r, float x0, float y0, float x1, float y1, const ImColor& col)
	{
		const float rad = (y1 - y0) * 0.5f;
		constexpr int SEG = 12;
		ImVec2 p[(SEG + 1) * 2];
		int n = 0;

		for (int i = 0; i <= SEG; i++)   // meia-lua direita (de cima p/ baixo)
		{
			const float a = -kPi * 0.5f + kPi * (float)i / (float)SEG;
			p[n++] = ImVec2(x1 - rad + std::cos(a) * rad, y0 + rad + std::sin(a) * rad);
		}
		for (int i = 0; i <= SEG; i++)   // meia-lua esquerda (de baixo p/ cima)
		{
			const float a = kPi * 0.5f + kPi * (float)i / (float)SEG;
			p[n++] = ImVec2(x0 + rad + std::cos(a) * rad, y0 + rad + std::sin(a) * rad);
		}
		r->drawConvexPolyFilled(p, n, col);
	}

	void DrawAtlasRect(ImGuiRenderer* r, RwRaster* tex, const HudAtlas::Rect& rc,
					   const ImVec2& a, const ImVec2& b, int alpha = 255)
	{
		const float iw = 1.0f / (float)HudAtlas::kWidth;
		const float ih = 1.0f / (float)HudAtlas::kHeight;
		r->drawImage(a, b, (ImTextureID)tex, ImColor(255, 255, 255, alpha),
					 ImVec2(rc.x * iw, rc.y * ih), ImVec2((rc.x + rc.w) * iw, (rc.y + rc.h) * ih));
	}

	// mesma figura do atlas, mas pintada de uma cor so (usada para o contorno escuro do logo)
	void DrawAtlasRectTint(ImGuiRenderer* r, RwRaster* tex, const HudAtlas::Rect& rc, const ImVec2& a, const ImVec2& b, const ImColor& tint)
	{
		const float iw = 1.0f / (float)HudAtlas::kWidth;
		const float ih = 1.0f / (float)HudAtlas::kHeight;
		r->drawImage(a, b, (ImTextureID)tex, tint, ImVec2(rc.x * iw, rc.y * ih), ImVec2((rc.x + rc.w) * iw, (rc.y + rc.h) * ih));
	}

	void DrawIcon(ImGuiRenderer* r, RwRaster* tex, const HudAtlas::Rect& rc, const ImVec2& c, float size)
	{
		if (!tex) return;
		const float h = size * 0.5f;
		DrawAtlasRect(r, tex, rc, ImVec2(c.x - h, c.y - h), ImVec2(c.x + h, c.y + h));
	}

	ImColor Mix(const ImColor& a, const ImColor& b, float t)
	{
		t = std::clamp(t, 0.0f, 1.0f);
		return ImColor((int)(a.Value.x * 255.0f * (1.0f - t) + b.Value.x * 255.0f * t),
					   (int)(a.Value.y * 255.0f * (1.0f - t) + b.Value.y * 255.0f * t),
					   (int)(a.Value.z * 255.0f * (1.0f - t) + b.Value.z * 255.0f * t), 255);
	}

	// Pisca-pisca de natal sobre o logo: 3 festoes de fio verde com lampadas coloridas que acendem em ondas.
	// a/b = caixa do logo; S = escala da HUD. So desenha primitivas (linha/poligono), sem textura.
	void DrawChristmasLights(ImGuiRenderer* r, const ImVec2& a, const ImVec2& b, float S)
	{
		using namespace std::chrono;
		const double t = (double)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count() / 1000.0;

		static const ImColor kBulb[5] = {
			ImColor(235,  40,  45, 255),   // vermelho
			ImColor( 60, 210,  80, 255),   // verde
			ImColor(255, 200,  40, 255),   // amarelo
			ImColor( 60, 150, 255, 255),   // azul
			ImColor(255, 255, 255, 255),   // branco
		};

		const float w      = b.x - a.x;
		const float lh     = b.y - a.y;
		const float ext    = w * 0.04f;                 // o fio passa um pouco das bordas do logo
		const float x0     = a.x - ext, x1 = b.x + ext;
		const float yAnch  = a.y + lh * 0.06f;          // onde o fio prende
		const float sag    = lh * 0.20f;                // quanto o festao cai
		constexpr int kSwags = 3;
		constexpr int kPerSwag = 6;                     // lampadas por festao
		constexpr int kSeg = 14;                        // segmentos do fio por festao

		const ImColor wire(18, 70, 30, 255);
		const float wireW = std::max(1.2f, 1.7f * S);
		const float swagW = (x1 - x0) / (float)kSwags;

		int idx = 0;
		for (int s = 0; s < kSwags; s++)
		{
			const float sx0 = x0 + swagW * (float)s;

			// fio (parabola: cai no meio de cada festao)
			ImVec2 pts[kSeg + 1];
			for (int i = 0; i <= kSeg; i++)
			{
				const float u = (float)i / (float)kSeg;
				pts[i] = ImVec2(sx0 + swagW * u, yAnch + sag * 4.0f * u * (1.0f - u));
			}
			r->drawPolyline(pts, kSeg + 1, wire, false, wireW);

			// lampadas penduradas no fio
			for (int k = 0; k < kPerSwag; k++, idx++)
			{
				const float u  = ((float)k + 0.5f) / (float)kPerSwag;
				const float px = sx0 + swagW * u;
				const float py = yAnch + sag * 4.0f * u * (1.0f - u);

				// brilho 0.30..1.0, onda que anda pela fileira
				const double ph = t * 1.6 + (double)idx * 0.55;
				const float  br = 0.30f + 0.70f * (0.5f + 0.5f * (float)std::sin(ph * 2.0 * kPi * 0.5));
				const ImColor base = kBulb[(idx + (int)(t * 0.5)) % 5];

				const float bw = 3.4f * S;      // meia largura da lampada
				const float bh = 5.2f * S;      // altura da lampada
				const float cy = py + 2.0f * S + bh * 0.5f;

				// soquete
				r->drawRect(ImVec2(px - 1.6f * S, py), ImVec2(px + 1.6f * S, py + 2.4f * S), ImColor(30, 30, 30, 255), true);

				// halo
				const ImColor glow((int)(base.Value.x * 255), (int)(base.Value.y * 255), (int)(base.Value.z * 255), (int)(70.0f * br));
				FillCircle(r, ImVec2(px, cy), bw * 2.3f, glow);

				// corpo da lampada (oval) + ponto de luz
				constexpr int N = 14;
				ImVec2 oval[N];
				for (int i = 0; i < N; i++)
				{
					const float ang = 2.0f * kPi * (float)i / (float)N;
					oval[i] = ImVec2(px + std::cos(ang) * bw, cy + std::sin(ang) * bh * 0.5f);
				}
				const ImColor body((int)(base.Value.x * 255 * (0.35f + 0.65f * br)),
								   (int)(base.Value.y * 255 * (0.35f + 0.65f * br)),
								   (int)(base.Value.z * 255 * (0.35f + 0.65f * br)), 255);
				r->drawConvexPolyFilled(oval, N, body);
				FillCircle(r, ImVec2(px - bw * 0.25f, cy - bh * 0.15f), bw * 0.32f,
						   ImColor(255, 255, 255, (int)(170.0f * br)));
			}
		}
	}

	// Circulo de status (vida / colete / fome / energia / sede): cor + icone + anel de nivel.
	void StatusCircle(ImGuiRenderer* r, RwRaster* tex, const HudAtlas::Rect& icon,
					  const ImVec2& c, float S, const ImColor& base, float pct, bool alert)
	{
		const float R = kRadius * S;

		// sombra
		FillCircle(r, ImVec2(c.x, c.y + R * 0.09f), R * 1.03f, ImColor(0, 0, 0, 70));

		// circulo (pisca quando o nivel esta baixo)
		float flash = 0.0f;
		if (alert && pct <= kLowPct)
			flash = (std::sin((float)GetTickCount() * 0.011f) * 0.5f + 0.5f) * 0.45f;
		FillCircle(r, c, R, Mix(base, kWhite, flash));

		// brilho suave no alto (efeito "botao")
		FillCircle(r, ImVec2(c.x - R * 0.22f, c.y - R * 0.38f), R * 0.46f, ImColor(255, 255, 255, 22));

		// anel de nivel
		if (kShowRing)
		{
			const float rr = R + kRingGap * S;
			const float th = std::max(1.4f, kRingW * S);
			DrawArc(r, c, rr, 1.0f, ImColor(0, 0, 0, 80), th);
			DrawArc(r, c, rr, pct / 100.0f, ImColor(255, 255, 255, 235), th);
		}

		DrawIcon(r, tex, icon, c, R * 1.12f);
	}

	// Circulo "simples" (linha 2): cor + icone (ou texto).
	void ValueCircle(ImGuiRenderer* r, RwRaster* tex, const HudAtlas::Rect* icon, const char* label,
					 const ImVec2& c, float S, const ImColor& base)
	{
		const float R = kRadius * S;
		FillCircle(r, ImVec2(c.x, c.y + R * 0.09f), R * 1.03f, ImColor(0, 0, 0, 70));
		FillCircle(r, c, R, base);
		FillCircle(r, ImVec2(c.x - R * 0.22f, c.y - R * 0.38f), R * 0.46f, ImColor(255, 255, 255, 22));

		if (icon && tex)
		{
			DrawIcon(r, tex, *icon, c, R * 1.12f);
		}
		else if (label)
		{
			const float fs = R * 0.95f;
			const ImVec2 ts = r->calculateTextSize(std::string(label), fs);
			r->drawText(ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), kWhite, std::string(label), false, fs);
		}
	}

	// "67214" -> "67.214"  (negativo: "-1.234")
	std::string FormatThousands(long long v)
	{
		const bool neg = v < 0;
		std::string s = std::to_string(neg ? -v : v), out;
		int c = 0;
		for (int i = (int)s.size() - 1; i >= 0; --i)
		{
			out.insert(out.begin(), s[i]);
			if (++c % 3 == 0 && i > 0) out.insert(out.begin(), '.');
		}
		return neg ? "-" + out : out;
	}

	bool GetLocalVitals(float& hp, float& armour)
	{
		if (!pNetGame || !pNetGame->GetPlayerPool()) return false;
		CLocalPlayer* lp = pNetGame->GetPlayerPool()->GetLocalPlayer();
		if (!lp) return false;
		CPlayerPed* pp = lp->GetPlayerPed();
		if (!pp || !pp->m_pPed) return false;

		hp = pp->GetHealth();
		armour = pp->GetArmour();
		return true;
	}

	// Efeito "TV ligando" (mesmo do mapa grande) quando o MINIMAPA aparece (motorista entra no veiculo):
	// uma linha branca cresce na horizontal e depois abre na vertical, com brilho e linhas de varredura, so dentro do disco do radar.
	// O radar e desenhado pelo jogo ANTES da UI; aqui a UI cobre de preto o que ainda "nao acendeu". Sem animacao ao apagar
	// (o jogo para de desenhar o radar de uma vez).
	void DrawMiniTv(ImGuiRenderer* r, const ImVec2& screen)
	{
		using namespace std::chrono;
		static float   s_vis = 1.0f;          // 0 = apagado, 1 = ligado
		static int64_t s_last = 0, s_seen = 0;
		const int64_t now = duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
		const float gw = RsGlobal ? (float)RsGlobal->maximumWidth : 0.0f, gh = RsGlobal ? (float)RsGlobal->maximumHeight : 0.0f;
		bool visible = Hud::MapAllowed() && !WorldMapWidget::IsOpen() && gw > 1.0f && gh > 1.0f
		            && (now - MiniMapTap::gStamp.load()) <= MiniMapTap::kFreshMs;
		MiniMapTap::Rect ru{ 0, 0, 0, 0 };
		if (visible)
		{
			ru = { MiniMapTap::gL.load(), MiniMapTap::gT.load(), MiniMapTap::gR.load(), MiniMapTap::gB.load() };
			visible = MiniMapTap::RectOk(ru, gw, gh);
		}
		if (!visible) { if (now - s_seen > 250) s_vis = 0.0f; s_last = now; return; }   // sumiu: a proxima aparicao anima de novo
		s_seen = now;
		const float dt = s_last ? std::min(0.1f, std::max(0.0f, (float)(now - s_last) / 1000.0f)) : 0.0f;
		s_last = now;
		constexpr float kOn = 0.42f, kLine = 0.40f;     // iguais ao mapa grande (WorldMapUI.h)
		s_vis = std::min(1.0f, s_vis + dt / kOn);
		if (s_vis >= 0.999f) return;

		const float sx = screen.x / gw, sy = screen.y / gh;
		const float cx = (ru.l + ru.r) * 0.5f * sx, cy = (ru.t + ru.b) * 0.5f * sy;
		const float R = std::max((ru.r - ru.l) * sx, (ru.b - ru.t) * sy) * 0.5f * 1.03f;
		const float k = R / 100.0f;
		const float pp = s_vis;
		float lw, lh, flash = 0.0f;
		const float lineH = std::max(2.0f, 3.0f * k);
		if (pp < kLine) { const float u = pp / kLine, e = 1.0f - (1.0f - u) * (1.0f - u); lw = std::max(6.0f * k, 2.0f * R * e); lh = lineH; flash = 1.0f; }
		else            { const float u = (pp - kLine) / (1.0f - kLine), e = 1.0f - (1.0f - u) * (1.0f - u); lw = 2.0f * R; lh = lineH + (2.0f * R - lineH) * e; flash = 1.0f - u; }
		const float litL = cx - lw * 0.5f, litR = cx + lw * 0.5f, litT = cy - lh * 0.5f, litB = cy + lh * 0.5f;

		const ImColor blk(0, 0, 0, 255);
		const float step = std::max(1.5f, 2.0f * k);
		const int fa = (int)std::lround(220.0f * std::clamp(flash, 0.0f, 1.0f));
		const int sa = (int)std::lround(90.0f * std::clamp(flash, 0.0f, 1.0f));
		int row = 0;
		for (float y = cy - R; y < cy + R; y += step, row++)
		{
			const float ym = y + step * 0.5f, dy = ym - cy;
			const float hw = std::sqrt(std::max(0.0f, R * R - dy * dy));
			if (hw < 1.0f) continue;
			const float x0 = cx - hw, x1 = cx + hw, y1 = y + step + 0.5f;
			if (ym < litT || ym > litB) { r->drawRect(ImVec2(x0, y), ImVec2(x1, y1), blk, true); continue; }
			if (litL > x0) r->drawRect(ImVec2(x0, y), ImVec2(std::min(litL, x1), y1), blk, true);
			if (litR < x1) r->drawRect(ImVec2(std::max(litR, x0), y), ImVec2(x1, y1), blk, true);
			const float a = std::max(x0, litL), b = std::min(x1, litR);
			if (b <= a) continue;
			if (fa > 0) r->drawRect(ImVec2(a, y), ImVec2(b, y1), ImColor(255, 255, 255, fa), true);     // brilho branco que some ao ligar
			if (sa > 2 && (row % 2) == 0) r->drawRect(ImVec2(a, y), ImVec2(b, y1), ImColor(0, 0, 0, sa), true);   // varredura
		}
	}

	// Coordenadas na tela (/xyz): painel escuro a direita do logo, 3 linhas grandes (legiveis em print): X Y / Z + chao (Z-1) / angulo e veiculo.
	void DrawCoords(ImGuiRenderer* r, const ImVec2& screen, float S)
	{
		if (!Hud::gCoords.load()) return;
		float x, y, z, a; bool veh;
		if (!Hud::CoordsGet(x, y, z, a, veh)) return;
		char l1[64], l2[64], l3[64];
		std::snprintf(l1, sizeof(l1), "X %.2f    Y %.2f", x, y);
		std::snprintf(l2, sizeof(l2), "Z %.2f    chao %.2f", z, z - 1.0f);
		std::snprintf(l3, sizeof(l3), "A %.1f  %s", a, veh ? "veiculo" : "a pe");
		const float fsz = 16.0f * S, lineH = 21.0f * S, pad = 10.0f * S;
		const std::string s1(l1), s2(l2), s3(l3);
		const ImVec2 t1 = r->calculateTextSize(s1, fsz), t2 = r->calculateTextSize(s2, fsz), t3 = r->calculateTextSize(s3, fsz);
		const float w = std::max(t1.x, std::max(t2.x, t3.x)) + pad * 2.0f, h = lineH * 3.0f + pad * 1.4f;
		const float lh = kLogoH * S, lw = lh * ((float)HudAtlas::kLogo.w / (float)HudAtlas::kLogo.h);
		float x0 = (screen.x + lw) * 0.5f + 12.0f * S;                       // logo centralizado: o painel fica logo a direita dele
		const float y0 = 6.0f * S;
		if (x0 + w > screen.x - 8.0f * S) x0 = std::max(4.0f * S, screen.x - 8.0f * S - w);
		r->drawRoundedRect(ImVec2(x0, y0), ImVec2(x0 + w, y0 + h), ImColor(8, 8, 12, 235), 7.0f * S, true);
		r->drawRoundedRect(ImVec2(x0, y0), ImVec2(x0 + w, y0 + h), ImColor(255, 220, 90, 150), 7.0f * S, false, 1.2f);
		const float tx = x0 + pad, ty = y0 + pad * 0.7f;
		r->drawText(ImVec2(tx, ty), ImColor(255, 240, 150, 255), s1, false, fsz);
		r->drawText(ImVec2(tx, ty + lineH), ImColor(150, 235, 255, 255), s2, false, fsz);
		r->drawText(ImVec2(tx, ty + lineH * 2.0f), ImColor(215, 215, 215, 255), s3, false, fsz);
	}

	// Nome do local (cidade | zona) numa etiqueta escura com icone de pino, logo ACIMA do minimapa e alinhada a esquerda dele.
	// So aparece enquanto o minimapa esta sendo desenhado (retangulo publicado por game/sprite2d.cpp) e a regra do motorista permite.
	void DrawLocationTag(ImGuiRenderer* r, const ImVec2& screen, float S)
	{
		const bool allowed = Hud::MapAllowed();                                       // true = minimapa visivel (motorista)
		if (!UiPrefs::LocOn()) return;                                                // jogador desligou a tarja do local (Configuracoes)
		const bool mapOpen = WorldMapWidget::IsOpen();                                // mapa aberto: so o local; fechado: local + bussola
		if (mapOpen) return;                                                          // mapa grande aberto: some a etiqueta (o minimapa tambem some)
		using namespace std::chrono;
		const int64_t now = duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
		// ancora: acima do minimapa (motorista). A pe nao ha minimapa: usa o ultimo canto conhecido dele (ou o canto da tela)
		static float s_anchL = -1.0f, s_anchT = -1.0f;
		if (allowed)
		{
			if (now - MiniMapTap::gStamp.load() > MiniMapTap::kFreshMs) return;       // radar nao foi desenhado agora
			const float gw = RsGlobal ? (float)RsGlobal->maximumWidth : 0.0f, gh = RsGlobal ? (float)RsGlobal->maximumHeight : 0.0f;
			if (gw < 1.0f || gh < 1.0f) return;
			const MiniMapTap::Rect ru{ MiniMapTap::gL.load(), MiniMapTap::gT.load(), MiniMapTap::gR.load(), MiniMapTap::gB.load() };
			if (!MiniMapTap::RectOk(ru, gw, gh)) return;
			s_anchL = ru.l * (screen.x / gw); s_anchT = ru.t * (screen.y / gh);
		}
		// a pe: posicao FIXA (sempre a mesma), um pouco para a direita; no carro: acima do minimapa
		const float mapL = allowed ? s_anchL : 22.0f * S;
		const float mapT = allowed ? s_anchT : 40.0f * S;

		// posicao do jogador -> texto (recalcula a cada 250 ms)
		static char s_txt[64] = "San Andreas";
		static float s_bearing = 0.0f;       // graus, sentido horario a partir do norte (0 = N, 90 = L)
		static int64_t s_last = 0;
		if (now - s_last >= 250)
		{
			s_last = now;
			if (pNetGame && pNetGame->GetPlayerPool())
			{
				CLocalPlayer* lp = pNetGame->GetPlayerPool()->GetLocalPlayer();
				CPlayerPed* pp = lp ? lp->GetPlayerPed() : nullptr;
				if (pp && pp->m_pPed)
				{
					const CVector p = pp->m_pPed->GetPosition();
					ZoneNames::Describe(p.x, p.y, p.z, s_txt, sizeof(s_txt));
					if (MiniCity::gInCity.load()) std::snprintf(s_txt, sizeof(s_txt), "Cidade Nova");   // dentro da cidade nova (fora do mapa original)
					// direcao do VEICULO (motorista): GetHeading = atan2(-frente.x, frente.y), anti-horario como no jogo -> rumo horario
					const float hd = (pp->m_pPed->pVehicle ? pp->m_pPed->pVehicle->GetHeading() : pp->m_pPed->m_fCurrentRotation) * 57.2957795f;
					s_bearing = std::fmod(std::fmod(-hd, 360.0f) + 360.0f, 360.0f);
				}
			}
		}

		const float fsz = 12.5f * S, h = 23.0f * S, padL = 8.0f * S, padR = 10.0f * S, pinW = 12.0f * S, gapPin = 6.0f * S;
		const ImVec2 ts = r->calculateTextSize(std::string(s_txt), fsz);
		const float w = padL + pinW + gapPin + ts.x + padR;
		const float x0 = std::max(2.0f * S, mapL), y1 = mapT - 4.0f * S, y0 = y1 - h;
		if (y0 < 0.0f) return;                                                        // sem espaco acima do minimapa
		r->drawRoundedRect(ImVec2(x0, y0), ImVec2(x0 + w, y1), ImColor(20, 20, 20, 215), 5.0f * S, true);
		r->drawRoundedRect(ImVec2(x0, y0), ImVec2(x0 + w, y1), ImColor(255, 255, 255, 40), 5.0f * S, false, 1.0f);

		// icone de pino (primitivas): cabeca redonda + ponta + furo
		const float pr = 4.6f * S, pcx = x0 + padL + pinW * 0.5f, pcy = y0 + h * 0.5f - 1.6f * S;
		const ImColor pc(225, 225, 225, 255);
		FillCircle(r, ImVec2(pcx, pcy), pr, pc);
		{
			ImVec2 t[3] = { ImVec2(pcx - pr * 0.86f, pcy + pr * 0.5f), ImVec2(pcx + pr * 0.86f, pcy + pr * 0.5f), ImVec2(pcx, pcy + pr * 2.15f) };
			r->drawConvexPolyFilled(t, 3, pc);
		}
		FillCircle(r, ImVec2(pcx, pcy), pr * 0.42f, ImColor(20, 20, 20, 255));

		r->drawText(ImVec2(x0 + padL + pinW + gapPin, y0 + (h - ts.y) * 0.5f), kWhite, std::string(s_txt), false, fsz);

		// ---- orientacao (bussola): mesma etiqueta, logo ao lado: "NE 045" + agulha que aponta para onde o carro vai ----
		if (!mapOpen && allowed)                                                     // bussola so para o motorista (minimapa fechado)
		{
			static const char* const kDir[8] = { "N", "NE", "L", "SE", "S", "SO", "O", "NO" };
			const int di = ((int)std::lround(s_bearing / 45.0f)) & 7;
			char dt[24];
			snprintf(dt, sizeof(dt), "%s %03d", kDir[di], ((int)std::lround(s_bearing)) % 360);
			const float cpad = 8.0f * S, cicon = 14.0f * S, cgap = 6.0f * S;
			const ImVec2 tsw = r->calculateTextSize(std::string("NO 000"), fsz);          // largura fixa (nao pula quando muda)
			const float cw = cpad + cicon + cgap + tsw.x + padR;
			const float cx0 = x0 + w + 4.0f * S;
			r->drawRoundedRect(ImVec2(cx0, y0), ImVec2(cx0 + cw, y1), ImColor(20, 20, 20, 215), 5.0f * S, true);
			r->drawRoundedRect(ImVec2(cx0, y0), ImVec2(cx0 + cw, y1), ImColor(255, 255, 255, 40), 5.0f * S, false, 1.0f);
			// icone: aro + agulha (ponta clara = rumo atual, relativo ao norte da tela = sobe; "N" fixo no topo do aro)
			const ImVec2 cc(cx0 + cpad + cicon * 0.5f, y0 + h * 0.5f);
			const float cr = cicon * 0.5f;
			{
				constexpr int N = 24;
				ImVec2 ring[N];
				for (int i = 0; i < N; i++) { const float a = 2.0f * kPi * (float)i / (float)N; ring[i] = ImVec2(cc.x + std::cos(a) * cr, cc.y + std::sin(a) * cr); }
				r->drawPolyline(ring, N, ImColor(225, 225, 225, 255), true, std::max(1.2f, 1.3f * S));
			}
			{
				const float a = -s_bearing * kPi / 180.0f;          // agulha gira ao contrario do rumo: o norte do mundo fica fixo em cima
				const float fx = std::sin(a), fy = -std::cos(a);    // ponta que aponta para o NORTE do mundo
				ImVec2 n1[3] = { ImVec2(cc.x + fx * cr * 0.85f, cc.y + fy * cr * 0.85f),
								 ImVec2(cc.x - fy * cr * 0.28f, cc.y + fx * cr * 0.28f),
								 ImVec2(cc.x + fy * cr * 0.28f, cc.y - fx * cr * 0.28f) };
				ImVec2 n2[3] = { ImVec2(cc.x - fx * cr * 0.85f, cc.y - fy * cr * 0.85f), n1[1], n1[2] };
				r->drawConvexPolyFilled(n1, 3, ImColor(235, 70, 70, 255));      // ponta norte (vermelha)
				r->drawConvexPolyFilled(n2, 3, ImColor(225, 225, 225, 255));
			}
			r->drawText(ImVec2(cx0 + cpad + cicon + cgap, y0 + (h - tsw.y) * 0.5f), kWhite, std::string(dt), false, fsz);
		}
	}

	// CGame::DisplayHUD(): o proprio jogo/servidor liga e desliga (selecao de classe, textdraw de tela cheia...)
	bool NativeHudEnabled()
	{
		return *(volatile uint8_t*)(g_libGTASA + 0x9FF3A8) != 0;
	}
}

// =====================================================================================
//  HudInfo
// =====================================================================================

ImVec2 HudInfo::SizeFor(const ImVec2& screen)
{
	return ImVec2(screen.x * 0.40f, screen.y * 0.26f);
}

ImVec2 HudInfo::PosFor(const ImVec2& screen)
{
	const ImVec2 sz = SizeFor(screen);
	return ImVec2(screen.x - sz.x, 0.0f);
}

HudInfo::HudInfo()
	: Widget()
{
}

// ---- toque no circulo "pessoas online" (icone de pessoa + contagem): abre a Central da Cidade (/central) ----
extern volatile bool g_bTouchCancel;     // game/multitouch.cpp
namespace
{
	std::atomic<float>   g_pplRect[4] = {{0.0f}, {0.0f}, {0.0f}, {0.0f}};   // x0,y0,x1,y1 do bloco (circulo + pilula), na ultima vez que a HUD foi desenhada
	std::atomic<int64_t> g_pplStamp{0};
	std::atomic<float>   g_cartRect[4] = {{0.0f}, {0.0f}, {0.0f}, {0.0f}};  // carrinho da loja (so aparece dentro da area de uma loja)
	std::atomic<int64_t> g_cartStamp{0};
	int64_t PplNowMs() { using namespace std::chrono; return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count(); }
	bool PplInside(float x, float y)
	{
		return x >= g_pplRect[0].load() && x <= g_pplRect[2].load() && y >= g_pplRect[1].load() && y <= g_pplRect[3].load();
	}
	bool CartInside(float x, float y)
	{
		return x >= g_cartRect[0].load() && x <= g_cartRect[2].load() && y >= g_cartRect[1].load() && y <= g_cartRect[3].load();
	}
}

bool HudHandleRawTouch(int type, int num, int x, int y, bool blockNew)
{
	static int   s_finger = -1;
	static float s_sx = 0.0f, s_sy = 0.0f;
	static bool  s_moved = false;
	static bool  s_cart = false;                     // o dedo caiu no carrinho (/comprar) e nao no circulo de pessoas (/central)
	const int n = num & 3;
	if (type == 2)                                   // dedo desceu
	{
		if (s_finger >= 0 || blockNew) return false;
		if (!Hud::g.visible.load() || JobsUI::IsOpen()) return false;
		if (PplNowMs() - g_pplStamp.load() > 250) return false;                  // HUD nao foi desenhada agora (oculta / outra tela por cima)
		const bool inCart = Hud::g.shop.load() && PplNowMs() - g_cartStamp.load() <= 250 && CartInside((float)x, (float)y);
		if (!inCart && !PplInside((float)x, (float)y)) return false;
		s_cart = inCart;
		s_finger = n; s_sx = (float)x; s_sy = (float)y; s_moved = false;
		return true;
	}
	if (s_finger != n) return false;
	if (type == 3)                                   // moveu: se passou de ~14 px deixa de ser toque
	{
		if (std::fabs((float)x - s_sx) > 14.0f || std::fabs((float)y - s_sy) > 14.0f) s_moved = true;
		return true;
	}
	if (type == 1)                                   // soltou: toque valido = abre a Central (mesmo caminho do /central digitado)
	{
		const bool ok = !s_moved && !g_bTouchCancel && (s_cart ? CartInside((float)x, (float)y) : PplInside((float)x, (float)y));
		s_finger = -1; s_moved = false;
		if (ok && pNetGame) pNetGame->SendChatCommand(s_cart ? "/comprar" : "/central");   // carrinho = mesmo caminho do /comprar digitado
		return true;
	}
	return true;
}


// PNG embutido -> raster (usado pelo brasao animado do Crie seu personagem)
RwRaster* HudRasterFromPng(const unsigned char* png, size_t pngSize) { return CreateRasterFromPng(png, pngSize); }

// =====================================================================================
//  Intro "cinema" (Maximo RP): da conexao aceita ate a cutscene do servidor. Tela preta com a logo pulsando + barras pretas (cima/baixo)
//  que recuam ate sumir. Cobre o retangulo nativo "Loading..." do jogo. Some sozinha (fail-safe) ou quando a cutscene comeca (Hud::g.hideAll).
// =====================================================================================
void HudDrawCinemaIntro(ImGuiRenderer* r)
{
	using namespace std::chrono;
	static int64_t t0 = 0, tEnd = 0; static bool done = false;
	const int64_t now = duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
	extern CNetGame* pNetGame;
	const bool conn = pNetGame && pNetGame->GetGameState() == GAMESTATE_CONNECTED;
	if (!conn) { t0 = 0; tEnd = 0; done = false; return; }
	if (done) return;
	if (!t0) t0 = now;
	if (!tEnd && Hud::g.hideAll.load()) tEnd = now;     // cutscene comecou: some rapido
	const float T = (float)(now - t0) / 1000.0f;
	float fade = 1.0f;                                   // fade global (so no fim)
	if (tEnd) fade = 1.0f - (float)(now - tEnd) / 450.0f;
	else if (T > 6.5f) fade = 1.0f - (T - 6.5f) / 0.5f;
	if (fade <= 0.0f || T > 7.2f) { done = true; return; }

	const ImVec2 scr = ImGui::GetIO().DisplaySize;
	auto sm = [](float x) { x = x < 0 ? 0 : (x > 1 ? 1 : x); return x * x * (3.0f - 2.0f * x); };
	// fundo: preto total ate 1.4 s, depois clareia ate 4.2 s (so as barras ficam)
	const float bgA = 1.0f - sm((T - 1.4f) / 2.8f);
	// barras: cobrem 50% cada (tela toda) ate 1.4 s e recuam ate 0 em 5.8 s
	const float barH = (scr.y * 0.5f) * (1.0f - sm((T - 1.4f) / 4.4f));
	const int a255 = (int)(255.0f * fade);
	if (bgA > 0.01f) r->drawRect(ImVec2(0, 0), scr, ImColor(0, 0, 0, (int)(255.0f * bgA * fade)), true);
	if (barH > 0.5f)
	{
		r->drawRect(ImVec2(0, 0), ImVec2(scr.x, barH), ImColor(0, 0, 0, a255), true);
		r->drawRect(ImVec2(0, scr.y - barH), scr, ImColor(0, 0, 0, a255), true);
		// filete fino de luz na borda das barras (acabamento de cinema)
		const int ea = (int)(70.0f * fade);
		r->drawRect(ImVec2(0, barH - 1.0f), ImVec2(scr.x, barH), ImColor(255, 190, 80, ea), true);
		r->drawRect(ImVec2(0, scr.y - barH), ImVec2(scr.x, scr.y - barH + 1.0f), ImColor(255, 190, 80, ea), true);
	}
	// logo: surge (0-0.9 s), pulsa suave, sobe um pouco e some perto do fim das barras
	RwRaster* tex = GetAtlas();
	if (!tex) return;
	const float appear = sm(T / 0.9f), gone = 1.0f - sm((T - 5.0f) / 1.3f);
	const float la = appear * gone * fade;
	if (la <= 0.01f) return;
	const float pulse = 1.0f + 0.035f * std::sin(T * 3.2f);
	const float lw = scr.x * 0.34f * pulse * (0.92f + 0.08f * appear);
	const float lh = lw * ((float)HudAtlas::kLogo.h / (float)HudAtlas::kLogo.w);
	const float cx = scr.x * 0.5f, cy = scr.y * 0.5f - scr.y * 0.03f * sm((T - 1.4f) / 3.0f);
	// brilho atras da logo
	const int ga = (int)(60.0f * la * (0.6f + 0.4f * std::sin(T * 3.2f)));
	for (int i = 0; i < 3; i++)
	{
		const float k = 1.0f + 0.12f * (float)(i + 1);
		r->drawImage(ImVec2(cx - lw * 0.5f * k, cy - lh * 0.5f * k), ImVec2(cx + lw * 0.5f * k, cy + lh * 0.5f * k), (ImTextureID)tex,
					 ImColor(255, 170, 40, ga / (i + 2)),
					 ImVec2(HudAtlas::kLogo.x / (float)HudAtlas::kWidth, HudAtlas::kLogo.y / (float)HudAtlas::kHeight),
					 ImVec2((HudAtlas::kLogo.x + HudAtlas::kLogo.w) / (float)HudAtlas::kWidth, (HudAtlas::kLogo.y + HudAtlas::kLogo.h) / (float)HudAtlas::kHeight));
	}
	DrawAtlasRect(r, tex, HudAtlas::kLogo, ImVec2(cx - lw * 0.5f, cy - lh * 0.5f), ImVec2(cx + lw * 0.5f, cy + lh * 0.5f), (int)(255.0f * la));
}

void HudInfo::draw(ImGuiRenderer* r)
{
	if (!Hud::g.visible.load()) return;
	if (!NativeHudEnabled()) return;
	if (pUI && pUI->spawn() && pUI->spawn()->visible()) return;

	float hp = 0.0f, armour = 0.0f;
	if (!GetLocalVitals(hp, armour)) return;

	RwRaster* tex = GetAtlas();   // pode ser nullptr: a HUD segue sem icones/logo

	const ImVec2 screen = ImGui::GetIO().DisplaySize;
	const float S = (screen.y / 941.0f) * Hud::g.scale.load();

	// Safe Zone (menu > Graficos): afasta a HUD das bordas direita/superior (so desenho; a HUD nao recebe toque)
	const GfxSettings::Insets sz = GfxSettings::SafeInsets(screen.x, screen.y);
	const float right = screen.x - sz.x - kMargin * S;
	const float fs = kFont * S;

	DrawMiniTv(r, screen);           // efeito de TV ligando quando o minimapa aparece (entrar no veiculo)
	// nome do local acima do minimapa (so motorista no veiculo). Dentro da CIDADE NOVA a imagem do minimapa e' desenhada pelo mapa (WorldMapUI,
	// que vem DEPOIS da HUD na ordem dos widgets) e cobriria a etiqueta e a bussola: la ela e' redesenhada por HudDrawLocationTagOver(), por cima.
	if (!(MiniCity::gInCity.load() && !MiniCity::gMapOpen.load())) DrawLocationTag(r, screen, S);
	DrawCoords(r, screen, S);        // /xyz: X Y Z A na tela

	// ------------------------------------------------------------------ logo
	{
		const float lh = kLogoH * S;
		const float lw = lh * ((float)HudAtlas::kLogo.w / (float)HudAtlas::kLogo.h);
		// logo SEPARADO da HUD: centralizado no topo da tela (os circulos continuam na direita, no mesmo lugar)
		const float lx = (screen.x - lw) * 0.5f;
		const ImVec2 a(lx, sz.y + kLogoTop * S);
		const ImVec2 b(lx + lw, sz.y + kLogoTop * S + lh);

		if (tex)
		{
			// contorno/sombra escuro em volta do logo: de dia (ceu claro) o logo branco sumia; assim ele fica legivel a qualquer hora
			const float o = std::max(1.5f, 2.2f * S);
			static const float kDx[8] = { 1, -1, 0, 0, 0.7f, -0.7f, 0.7f, -0.7f }, kDy[8] = { 0, 0, 1, -1, 0.7f, 0.7f, -0.7f, -0.7f };
			for (int i = 0; i < 8; i++)
				DrawAtlasRectTint(r, tex, HudAtlas::kLogo, ImVec2(a.x + kDx[i] * o, a.y + kDy[i] * o), ImVec2(b.x + kDx[i] * o, b.y + kDy[i] * o), ImColor(8, 14, 30, 235));
			DrawAtlasRectTint(r, tex, HudAtlas::kLogo, ImVec2(a.x, a.y + o * 1.4f), ImVec2(b.x, b.y + o * 1.4f), ImColor(0, 0, 0, 120));
			DrawAtlasRect(r, tex, HudAtlas::kLogo, a, b);
		}
		else
		{
			const float lfs = lh * 0.55f;
			const ImVec2 ts = r->calculateTextSize(std::string("MAXIMO RP"), lfs);
			r->drawText(ImVec2((screen.x - ts.x) * 0.5f, a.y + (lh - ts.y) * 0.5f), kWhite, std::string("MAXIMO RP"), true, lfs);
		}
	}

	// luzes de natal sobre o logo (desligar: /hud luzes off ou kChristmasLights = false)
	if (kChristmasLights && Hud::g.lights.load())
	{
		const float lh = kLogoH * S;
		const float lw = lh * ((float)HudAtlas::kLogo.w / (float)HudAtlas::kLogo.h);
		const float lx = (screen.x - lw) * 0.5f;
		DrawChristmasLights(r, ImVec2(lx, sz.y + kLogoTop * S), ImVec2(lx + lw, sz.y + kLogoTop * S + lh), S);
	}

	if (Hud::g.photoCam.load()) return;   // Camera do celular aberta: so o logo (acima); circulos, pilulas, minimapa... ficam escondidos

	// ------------------------------------------------------------------ linha 1: status
	const float R  = kRadius * S;
	const float y1 = sz.y + (kTop + kLogoH + kLogoGap - kRaise + kRadius) * S;
	{
		struct Item { const HudAtlas::Rect* icon; ImColor col; float pct; bool alert; };
		const Item items[5] = {
			{ &HudAtlas::kIcon_heart,  kRed,    std::clamp(hp,     0.0f, 100.0f), true  },
			{ &HudAtlas::kIcon_shield, kCyan,   std::clamp(armour, 0.0f, 100.0f), false },   // sem colete nao e alerta
			{ &HudAtlas::kIcon_burger, kYellow, (float)Hud::g.hunger.load(),      true  },
			{ &HudAtlas::kIcon_bolt,   kOrange, (float)Hud::g.energy.load(),      true  },
			{ &HudAtlas::kIcon_drop,   kBlue,   (float)Hud::g.thirst.load(),      true  },
		};

		// alinhado a direita: o ultimo circulo (sede) encosta na margem
		const float step = R * 2.0f + kCircGap * S;
		float cx = right - R - (kRingGap + kRingW) * S * 0.5f;
		for (int i = 4; i >= 0; i--, cx -= step)
			StatusCircle(r, tex, *items[i].icon, ImVec2(cx, y1), S, items[i].col, items[i].pct, items[i].alert);
	}

	// ------------------------------------------------------------------ tarja de ID + hora + data (topo direito, acima dos circulos)
	if (UiPrefs::IdOn() && Hud::g.accountId.load() > 0)
	{
		char t1[64], t2[32];
		snprintf(t1, sizeof t1, "ID: %d", Hud::g.accountId.load());
		const time_t tt = time(nullptr); struct tm lt; localtime_r(&tt, &lt);
		snprintf(t2, sizeof t2, "%02d:%02d  %02d/%02d/%04d", lt.tm_hour, lt.tm_min, lt.tm_mday, lt.tm_mon + 1, lt.tm_year + 1900);
		const float fs = 13.0f * S, padX = 9.0f * S, padY = 4.0f * S;
		const ImVec2 s1 = r->calculateTextSize(std::string(t1), fs), s2 = r->calculateTextSize(std::string(t2), fs * 0.88f);
		const float w = std::max(s1.x, s2.x) + padX * 2.0f, h = s1.y + s2.y + padY * 2.0f + 2.0f * S;
		const float bx1 = right, bx0 = bx1 - w, by1 = y1 - R - 8.0f * S, by0 = by1 - h;
		r->drawRoundedRect(ImVec2(bx0, by0), ImVec2(bx1, by1), ImColor(10, 14, 22, 190), 7.0f * S, true);
		r->drawRoundedRect(ImVec2(bx0, by0), ImVec2(bx1, by1), ImColor(98, 232, 250, 150), 7.0f * S, false, std::max(1.0f, 1.2f * S));
		r->drawText(ImVec2(bx1 - padX - s1.x, by0 + padY), ImColor(98, 232, 250, 255), std::string(t1), true, fs);
		r->drawText(ImVec2(bx1 - padX - s2.x, by0 + padY + s1.y + 2.0f * S), ImColor(225, 232, 244, 235), std::string(t2), false, fs * 0.88f);
	}

	// ------------------------------------------------------------------ linha 2: valores
	{
		const float y2 = y1 + (kRadius * 2.0f + kRowGap) * S;
		const float ph = kPillH * S;
		const float padX = kPillPad * S;

		char moneyTxt[48], personTxt[48], extraTxt[48];
		snprintf(moneyTxt, sizeof(moneyTxt), "R$ %s", FormatThousands(Hud::g.money.load()).c_str());

		int a = Hud::g.slotA.load(), b = Hud::g.slotB.load();
		if (!Hud::g.peopleManual.load())        // padrao: jogadores online (voce + os do pool, sem NPC)
		{
			a = 0; b = 0;
			if (pNetGame && pNetGame->GetPlayerPool())
			{
				CPlayerPool* pool = pNetGame->GetPlayerPool();
				a = 1;
				for (int i = 0; i < MAX_PLAYERS; i++) if (pool->GetAt(i) && !pool->IsPlayerNPC(i)) a++;
			}
		}
		if (b > 0) snprintf(personTxt, sizeof(personTxt), "%d/%d", a, b);
		else       snprintf(personTxt, sizeof(personTxt), "%d", a);

		snprintf(extraTxt, sizeof(extraTxt), "%s", FormatThousands(Hud::g.extra.load()).c_str());

		struct Block
		{
			const HudAtlas::Rect* icon;
			const char* label;      // usado quando nao ha icone
			ImColor     col;
			const char* text;
			float       textW;
			float       pillW;
		};
		Block blocks[3] = {
			{ nullptr,                   "R$", kGreen, moneyTxt,  0, 0 },
			{ &HudAtlas::kIcon_person,   nullptr, kBlack, personTxt, 0, 0 },
			{ &HudAtlas::kIcon_burst,    nullptr, kBurst, extraTxt,  0, 0 },
		};

		float total = 0.0f;
		for (int i = 0; i < 3; i++)
		{
			blocks[i].textW = r->calculateTextSize(std::string(blocks[i].text), fs).x;
			blocks[i].pillW = std::max(blocks[i].textW + padX * 2.0f, ph * 1.4f);
			total += R * 2.0f + kCircPill * S + blocks[i].pillW;
			if (i < 2) total += kItemGap * S;
		}

		float x = right - total;
		for (int i = 0; i < 3; i++)
		{
			const Block& bl = blocks[i];

			const float blockX0 = x;
			ValueCircle(r, tex, bl.icon, bl.label, ImVec2(x + R, y2), S, bl.col);
			x += R * 2.0f + kCircPill * S;
			if (i == 1)                                 // bloco "pessoas online": area de toque (com folga para o dedo)
			{
				const float pad = 8.0f * S, hh = std::max(R, ph * 0.5f);
				g_pplRect[0].store(blockX0 - pad); g_pplRect[1].store(y2 - hh - pad);
				g_pplRect[2].store(x + bl.pillW + pad); g_pplRect[3].store(y2 + hh + pad);
				g_pplStamp.store(PplNowMs());
			}

			// pilula translucida (azul-petroleo escuro, como na referencia)
			FillCapsule(r, x, y2 - ph * 0.5f, x + bl.pillW, y2 + ph * 0.5f, ImColor(22, 34, 50, 205));

			const ImVec2 ts = r->calculateTextSize(std::string(bl.text), fs);
			r->drawText(ImVec2(x + (bl.pillW - ts.x) * 0.5f, y2 - ts.y * 0.5f), kWhite, std::string(bl.text), true, fs);

			x += bl.pillW + kItemGap * S;
		}

		// ---- linha 3: carrinho (so dentro da area de uma loja): toque = /comprar ----
		if (Hud::g.shop.load())
		{
			const float cy = y2 + (kRadius * 2.0f + kRowGap) * S;
			const float cx = right - R - (kRingGap + kRingW) * S * 0.5f;                 // alinhado ao ultimo circulo da linha 1
			FillCircle(r, ImVec2(cx, cy), R, ImColor(34, 150, 90, 255));
			DrawArc(r, ImVec2(cx, cy), R - 1.5f * S, 1.0f, ImColor(255, 255, 255, 200), 2.0f * S);
			const ImColor wh(255, 255, 255, 255);
			// carrinho de compras desenhado por codigo: alca, cesto e duas rodas
			r->drawLine(ImVec2(cx - 11.0f * S, cy - 9.0f * S), ImVec2(cx - 7.0f * S, cy - 9.0f * S), wh, 2.2f * S);
			r->drawLine(ImVec2(cx - 7.0f * S, cy - 9.0f * S), ImVec2(cx - 4.0f * S, cy + 3.0f * S), wh, 2.2f * S);
			r->drawLine(ImVec2(cx - 4.0f * S, cy + 3.0f * S), ImVec2(cx + 8.0f * S, cy + 3.0f * S), wh, 2.2f * S);
			r->drawLine(ImVec2(cx - 6.0f * S, cy - 4.0f * S), ImVec2(cx + 10.0f * S, cy - 4.0f * S), wh, 2.2f * S);
			r->drawLine(ImVec2(cx + 10.0f * S, cy - 4.0f * S), ImVec2(cx + 8.0f * S, cy + 3.0f * S), wh, 2.2f * S);
			FillCircle(r, ImVec2(cx - 2.0f * S, cy + 8.0f * S), 2.2f * S, wh);
			FillCircle(r, ImVec2(cx + 7.0f * S, cy + 8.0f * S), 2.2f * S, wh);
			const float pad = 8.0f * S;
			g_cartRect[0].store(cx - R - pad); g_cartRect[1].store(cy - R - pad);
			g_cartRect[2].store(cx + R + pad); g_cartRect[3].store(cy + R + pad);
			g_cartStamp.store(PplNowMs());
		}
	}
}

// Chamada por Ui::Draw (WorldMapUI.h) logo depois de desenhar a imagem da cidade no minimapa: mesma etiqueta (local + bussola) e mesmas regras de
// visibilidade de HudInfo::draw, so que POR CIMA da imagem. Fora da cidade nao e' chamada (a HUD desenha a etiqueta normalmente).
void HudDrawLocationTagOver(ImGuiRenderer* r)
{
	if (!Hud::g.visible.load()) return;
	if (!NativeHudEnabled()) return;
	if (pUI && pUI->spawn() && pUI->spawn()->visible()) return;
	float hp = 0.0f, armour = 0.0f;
	if (!GetLocalVitals(hp, armour)) return;
	const ImVec2 screen = ImGui::GetIO().DisplaySize;
	const float S = (screen.y / 941.0f) * Hud::g.scale.load();
	DrawLocationTag(r, screen, S);
}
