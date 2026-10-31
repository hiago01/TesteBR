#include "../../main.h"
#include "../gui.h"
#include "../../game/game.h"
#include "../../game/RW/RenderWare.h"
#include "../../net/netgame.h"
#include "../../net/localplayer.h"
#include "../../vendor/stb/stb_image.h"   // implementacao (STB_IMAGE_IMPLEMENTATION) esta em game/sprite2d.cpp
#include "hudatlas.h"
#include "../../game/stamina.h"

#include <chrono>
#include <mutex>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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

	void StaminaTick(bool sprinting)
	{
		using namespace std::chrono;
		const long long now = duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
		std::lock_guard<std::mutex> lk(g_stMutex);
		const float dt = g_stLastMs == 0 ? 0.0f : (float)(now - g_stLastMs) / 1000.0f;
		g_stLastMs = now;
		if (g_stServerPending >= 0) { g_st.OnServer(g_stServerPending); g_stServerPending = -1; }
		g_st.Tick(sprinting, dt);
		g.energy.store(g_st.Percent());
	}

	bool CanSprint()
	{
		std::lock_guard<std::mutex> lk(g_stMutex);
		return g_st.CanSprint();
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
		g.visible.store(true);
		g.hunger.store(100);
		g.thirst.store(100);
		g.energy.store(100);
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
		g.extra.store(0);
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
			g.extra.store(24600);
			return "[HUD] valores de teste aplicados (use /hud reset para voltar)";
		}

		if (a[0] == "reset") { Reset(); return "[HUD] resetada"; }

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
				return "[HUD] pessoa ok";
			}
		}

		return "[HUD] uso: /hud | on | off | teste | reset | escala <0.5-2.5> | "
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
	RwRaster* CreateAtlasRaster()
	{
		int w = 0, h = 0, ch = 0;
		unsigned char* px = stbi_load_from_memory(HudAtlas::kPng, (int)HudAtlas::kPngSize, &w, &h, &ch, 4);
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
	constexpr float kLogoGap  = 8.0f;     // logo -> linha 1
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

	const float right = screen.x - kMargin * S;
	const float fs = kFont * S;

	// ------------------------------------------------------------------ logo
	{
		const float lh = kLogoH * S;
		const float lw = lh * ((float)HudAtlas::kLogo.w / (float)HudAtlas::kLogo.h);
		const ImVec2 a(right - lw, kTop * S);
		const ImVec2 b(right, kTop * S + lh);

		if (tex)
		{
			DrawAtlasRect(r, tex, HudAtlas::kLogo, a, b);
		}
		else
		{
			const float lfs = lh * 0.55f;
			const ImVec2 ts = r->calculateTextSize(std::string("MAXIMO RP"), lfs);
			r->drawText(ImVec2(right - ts.x, a.y + (lh - ts.y) * 0.5f), kWhite, std::string("MAXIMO RP"), true, lfs);
		}
	}

	// ------------------------------------------------------------------ linha 1: status
	const float R  = kRadius * S;
	const float y1 = (kTop + kLogoH + kLogoGap + kRadius) * S;
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

	// ------------------------------------------------------------------ linha 2: valores
	{
		const float y2 = y1 + (kRadius * 2.0f + kRowGap) * S;
		const float ph = kPillH * S;
		const float padX = kPillPad * S;

		char moneyTxt[48], personTxt[48], extraTxt[48];
		snprintf(moneyTxt, sizeof(moneyTxt), "R$ %s", FormatThousands(Hud::g.money.load()).c_str());

		const int a = Hud::g.slotA.load(), b = Hud::g.slotB.load();
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

			ValueCircle(r, tex, bl.icon, bl.label, ImVec2(x + R, y2), S, bl.col);
			x += R * 2.0f + kCircPill * S;

			// pilula translucida (azul-petroleo escuro, como na referencia)
			FillCapsule(r, x, y2 - ph * 0.5f, x + bl.pillW, y2 + ph * 0.5f, ImColor(22, 34, 50, 205));

			const ImVec2 ts = r->calculateTextSize(std::string(bl.text), fs);
			r->drawText(ImVec2(x + (bl.pillW - ts.x) * 0.5f, y2 - ts.y * 0.5f), kWhite, std::string(bl.text), true, fs);

			x += bl.pillW + kItemGap * S;
		}
	}
}
