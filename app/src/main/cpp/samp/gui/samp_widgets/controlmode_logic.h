#pragma once
// =====================================================================================
//  Logica PURA dos controles CLASSICO x MODERNO (sem ImGui/Android/jogo): testada no PC
//  (testes/controles/test_controles.cpp).
//
//  - Mode            : qual sistema esta ativo (padrao CLASSICO).
//  - ModernConfig    : ajustes do sistema moderno (independentes dos do classico).
//  - ModernLayout    : geometria (joystick + botoes) para uma tela sw x sh.
//  - ModernController: multitouch por ID do dedo (um dedo = um controle ate soltar).
//  - SelectorLayout/SelectorState: tela de escolha (2 cartoes + SELECIONAR + CONFIRMAR/VOLTAR).
// =====================================================================================
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

namespace CtlLogic
{
	// ---------------------------------------------------------------- modo ----------
	enum class Mode : int { Classic = 0, Modern = 1 };

	inline Mode ModeFromString(const char* s)
	{
		if (!s) return Mode::Classic;                                   // padrao: CLASSICO
		std::string v(s);
		for (auto& c : v) c = (char)std::tolower((unsigned char)c);
		if (v == "moderno" || v == "modern" || v == "1") return Mode::Modern;
		return Mode::Classic;                                           // qualquer outra coisa -> classico
	}
	inline const char* ModeToString(Mode m) { return m == Mode::Modern ? "moderno" : "classico"; }

	// Secao do settings.ini POR JOGADOR (nick): so letras/numeros/_ ; vazio -> "jogador".
	inline std::string SectionFor(const char* nick)
	{
		std::string s = "controles_";
		std::string n;
		if (nick) for (const char* p = nick; *p && n.size() < 24; ++p)
			n += (std::isalnum((unsigned char)*p) || *p == '_') ? *p : '_';
		s += n.empty() ? "jogador" : n;
		return s;
	}

	// ---------------------------------------------------------------- botoes ---------
	// P1 / P2 (entrar no veiculo como MOTORISTA / PASSAGEIRO): aparecem a pe, perto de um veiculo do SA-MP, e a acao e feita pelo
	// cliente (CLocalPlayer::HandleDriverEntry / HandlePassengerEntry). No moderno o widget nativo "hud_car" fica oculto a pe.
	enum Btn : int { B_RUN = 0, B_JUMP, B_CROUCH, B_ATTACK, B_MIC, B_RUNLOCK, B_AIM, B_P1, B_P2, B_INV, B_SLOTS, B_COUNT };
	// B_INV  = botao INVENTARIO (mochila), padrao logo abaixo do minimapa; toque = pede ao servidor para abrir o inventario (/inv).
	// B_SLOTS = barra de SLOTS RAPIDOS (kHotbarN casas) embaixo da tela; mostra os kHotbarN primeiros slots do inventario do servidor,
	//           toque numa casa = usar o item (/invx use N). So A PE (some em veiculo / UI aberta). Editaveis como os outros (arrastar/tamanho/opacidade).
	inline const char* BtnKey(int b)
	{
		static const char* k[B_COUNT] = { "corrida", "pulo", "agachar", "ataque", "microfone", "corrida_lock", "mirar", "p1", "p2", "inventario", "slots" };
		return (b >= 0 && b < B_COUNT) ? k[b] : "";
	}

	struct CtlVec { float x, y; };
	struct CtlCircle { float cx, cy, r; };
	struct CtlRect { float l, t, r, b; };
	inline bool CtlIn(const CtlRect& R, float x, float y) { return x >= R.l && x <= R.r && y >= R.t && y <= R.b; }

	inline float Clampf(float v, float lo, float hi) { return !(v >= lo) ? lo : (v > hi ? hi : v); }

	// ---------------------------------------------------------------- config ---------
	struct ModernConfig
	{
		CtlVec   joyPos   { 0.134f, 0.768f };   // centro da base do joystick (fracao da tela)
		float joyScale = 1.0f;             // 0.5 .. 1.6
		float btnScale = 1.0f;             // 0.5 .. 1.6
		float alpha    = 0.50f;            // 0.15 .. 1.0 (transparencia; padrao 50%)
		float sens     = 1.0f;             // 0.5 .. 2.0 (sensibilidade da intensidade)
		float deadzone = 0.12f;            // 0 .. 0.45
		float maxDist  = 1.0f;             // 0.6 .. 1.6 (distancia analogica maxima, x raio da base)
		bool  showInv   = true;            // mostra o botao INVENTARIO (abaixo do minimapa)
		bool  showSlots = true;            // mostra a barra de SLOTS RAPIDOS (embaixo)
		bool  aimDrag  = true;             // Ataque tambem arrasta a mira (estilo Free Fire); desligado = Ataque so atira
		float aimSens  = 1.0f;             // 0.3 .. 3.0 (sensibilidade do arrasto da mira)
		float aimSmooth = 0.0f;            // 0 .. 0.85 (suavizacao; 0 = desligada)
		CtlVec   btnPos[B_COUNT] = {     // padrao = HUD atual do jogo (medido no print 1600x720 do HUD final, 8/nov)
			{ 0.7875f, 0.824f }, // corrida
			{ 0.792f, 0.635f }, // pulo
			{ 0.898f, 0.879f }, // agachar
			{ 0.901f, 0.656f }, // ataque
			{ 0.929f, 0.417f }, // microfone
			{ 0.1375f, 0.458f }, // corrida lock (liga/desliga: corre reto sozinho)
			{ 0.719f, 0.563f }, // mirar (toque: trava a mira; atirar desativa)
			{ 0.652f, 0.711f }, // P1: entrar como motorista
			{ 0.5875f, 0.714f }, // P2: entrar como passageiro
			{ 0.050f, 0.333f }, // inventario (abaixo do minimapa)
			{ 0.500f, 0.914f }, // slots rapidos (embaixo, centro)
		};

		// Por controle (0..B_COUNT-1 = botoes, B_COUNT = joystick): multiplicador de tamanho e opacidade propria.
		float ctlScale[B_COUNT + 1];       // 0.5 .. 2.0 (x joy_escala / botoes_escala)
		float ctlAlpha[B_COUNT + 1];       // 0.15 .. 1.0 (opacidade absoluta do controle)
		ModernConfig() { for (int i = 0; i <= B_COUNT; i++) { ctlScale[i] = 1.0f; ctlAlpha[i] = 0.50f; } ctlScale[B_RUNLOCK] = 1.32f; ctlScale[B_SLOTS] = 1.22f; }      // todas as opacidades em 50%

		void Clamp()
		{
			auto fin = [](float v, float d) { return std::isfinite(v) ? v : d; };
			joyScale = Clampf(fin(joyScale, 1.0f), 0.5f, 1.6f);
			btnScale = Clampf(fin(btnScale, 1.0f), 0.5f, 1.6f);
			alpha    = Clampf(fin(alpha, 0.15f),   0.15f, 1.0f);
			sens     = Clampf(fin(sens, 1.0f),     0.5f, 2.0f);
			deadzone = Clampf(fin(deadzone, 0.12f), 0.0f, 0.45f);
			maxDist  = Clampf(fin(maxDist, 1.0f),  0.6f, 1.6f);
			aimSens  = Clampf(fin(aimSens, 1.0f),  0.3f, 3.0f);
			aimSmooth = Clampf(fin(aimSmooth, 0.0f), 0.0f, 0.85f);
			for (int i = 0; i <= B_COUNT; i++) { ctlScale[i] = Clampf(fin(ctlScale[i], 1.0f), 0.5f, 2.0f); ctlAlpha[i] = Clampf(fin(ctlAlpha[i], 0.15f), 0.15f, 1.0f); }
			joyPos.x = Clampf(fin(joyPos.x, 0.13f), 0.05f, 0.95f);
			joyPos.y = Clampf(fin(joyPos.y, 0.72f), 0.10f, 0.95f);
			for (auto& p : btnPos) { p.x = Clampf(fin(p.x, 0.5f), 0.03f, 0.97f); p.y = Clampf(fin(p.y, 0.5f), 0.05f, 0.97f); }
		}
	};

	// ---------------------------------------------------------------- HUD 1..4 ---------
	// Cada HUD guarda SO o que e "do layout": posicoes, tamanho e opacidade de cada controle. Os ajustes globais
	// (sensibilidade, zona morta, mira...) ficam no ModernConfig e valem para os 4.
	constexpr int kHudCount = 4;
	struct HudLayout
	{
		CtlVec joyPos{ 0.13f, 0.72f };
		CtlVec btnPos[B_COUNT];
		float  ctlScale[B_COUNT + 1];
		float  ctlAlpha[B_COUNT + 1];
		HudLayout() { const ModernConfig d; joyPos = d.joyPos; for (int i = 0; i < B_COUNT; i++) btnPos[i] = d.btnPos[i]; for (int i = 0; i <= B_COUNT; i++) { ctlScale[i] = d.ctlScale[i]; ctlAlpha[i] = d.ctlAlpha[i]; } }
	};
	inline HudLayout ExtractLayout(const ModernConfig& c)
	{
		HudLayout h; h.joyPos = c.joyPos;
		for (int i = 0; i < B_COUNT; i++) h.btnPos[i] = c.btnPos[i];
		for (int i = 0; i <= B_COUNT; i++) { h.ctlScale[i] = c.ctlScale[i]; h.ctlAlpha[i] = c.ctlAlpha[i]; }
		return h;
	}
	inline void ApplyLayout(ModernConfig& c, const HudLayout& h)
	{
		c.joyPos = h.joyPos;
		for (int i = 0; i < B_COUNT; i++) c.btnPos[i] = h.btnPos[i];
		for (int i = 0; i <= B_COUNT; i++) { c.ctlScale[i] = h.ctlScale[i]; c.ctlAlpha[i] = h.ctlAlpha[i]; }
		c.Clamp();
	}
	inline int ClampHud(int n) { return n < 0 ? 0 : (n >= kHudCount ? kHudCount - 1 : n); }

	// ---------------------------------------------------------------- layout ---------
	struct ModernLayout
	{
		CtlCircle base{};            // base do joystick
		float  knobR = 0;         // raio do botao central
		float  maxDistPx = 0;     // curso maximo do centro (px)
		float  zoneR = 0;         // area de toque que "pega" o joystick
		CtlCircle btn[B_COUNT]{};
	};

	inline ModernLayout MakeLayout(const ModernConfig& c, float sw, float sh)
	{
		ModernLayout L;
		const float baseR = sh * 0.15f * c.joyScale * c.ctlScale[B_COUNT];
		L.base      = CtlCircle{ c.joyPos.x * sw, c.joyPos.y * sh, baseR };
		L.knobR     = baseR * 0.42f;
		L.maxDistPx = baseR * c.maxDist;
		L.zoneR     = baseR * 1.55f;
		const float rb = sh * 0.058f * c.btnScale;
		for (int i = 0; i < B_COUNT; i++)
			L.btn[i] = CtlCircle{ c.btnPos[i].x * sw, c.btnPos[i].y * sh, rb * c.ctlScale[i] * (i == B_ATTACK ? 1.45f : 1.0f) };
		return L;
	}

	inline bool Hit(const CtlCircle& c, float x, float y, float padK = 1.2f)
	{
		const float dx = x - c.cx, dy = y - c.cy, R = c.r * padK;
		return dx * dx + dy * dy <= R * R;
	}

	// ---- barra de slots rapidos (B_SLOTS): o "circulo" do layout guarda o centro e METADE do lado de cada casa (r) ----
	constexpr int   kHotbarN   = 5;
	constexpr float kHotbarGap = 0.16f;                       // espaco entre casas (fracao do lado da casa)
	inline float HotbarSide(const CtlCircle& c)  { return 2.0f * c.r; }
	inline float HotbarPitch(const CtlCircle& c) { return HotbarSide(c) * (1.0f + kHotbarGap); }
	inline float HotbarWidth(const CtlCircle& c) { return HotbarSide(c) * kHotbarN + HotbarSide(c) * kHotbarGap * (kHotbarN - 1); }
	inline float HotbarLeft(const CtlCircle& c)  { return c.cx - HotbarWidth(c) * 0.5f; }
	inline int   HotbarIndexAt(const CtlCircle& c, float x)   // casa mais proxima do x (toque no espaco entre casas escolhe a vizinha)
	{
		int i = (int)std::floor((x - HotbarLeft(c)) / HotbarPitch(c));
		return i < 0 ? 0 : (i >= kHotbarN ? kHotbarN - 1 : i);
	}
	// Acerto de um controle: circulo, exceto a barra de slots (retangulo da barra inteira).
	inline bool BtnHit(const ModernLayout& L, int i, float x, float y, float padK = 1.2f)
	{
		const CtlCircle& c = L.btn[i];
		if (i == B_SLOTS)
		{
			const float hw = HotbarWidth(c) * 0.5f + c.r * (padK - 1.0f), hh = c.r * padK;
			return std::fabs(x - c.cx) <= hw && std::fabs(y - c.cy) <= hh;
		}
		return Hit(c, x, y, padK);
	}

	// ---------------------------------------------------------------- analogico -------
	// (dx,dy) = deslocamento do dedo em relacao ao centro da base (px, y para baixo).
	// Saida em [-1,1]: direcao 360 graus; intensidade proporcional (zona morta -> 0; depois cresce ate 1).
	inline void StickAxes(float dx, float dy, float maxDistPx, float deadzone, float sens, float& ox, float& oy)
	{
		ox = oy = 0.0f;
		const float len = std::sqrt(dx * dx + dy * dy);
		if (!(len > 0.0f) || !(maxDistPx > 0.0f)) return;
		const float m = std::min(len / maxDistPx, 1.0f);
		if (m <= deadzone) return;
		float mag = (m - deadzone) / std::max(1.0f - deadzone, 0.001f);
		mag = Clampf(mag * sens, 0.0f, 1.0f);
		ox = dx / len * mag;
		oy = dy / len * mag;
	}

	// Escala do stick do motor: +-127 (movemode.cpp usa kMax 128). Y da tela para baixo = ud positivo (tras);
	// para cima = negativo (frente) - convencao do GTA SA, NAO verificada no aparelho.
	inline int ToPad(float v) { return (int)std::lround(Clampf(v, -1.0f, 1.0f) * 127.0f); }

	// ---------------------------------------------------------------- painel do editor ----
	// Painel do editor de HUD (estilo do video de referencia): linha 1 [sair][<  HUD n  >][nuvem], linha 2 [compartilhar][restaurar][salvar],
	// "Ajuste Inteligente", Opacidade/Tamanho (barras) + setas de posicao, e uma aba (v/^) que recolhe o painel.
	// O painel pode ser ARRASTADO (ox/oy em fracao da tela). Tudo em fracao da tela (retrato/paisagem).
	enum PanelItem : int { PI_NONE = 0, PI_EXIT, PI_HUDPREV, PI_HUDNEXT, PI_CLOUD, PI_SHARE, PI_RESET, PI_SAVE,
	                       PI_SMART, PI_OPA_TRK, PI_SIZE_TRK, PI_DP_L, PI_DP_R, PI_DP_U, PI_DP_D, PI_TAB, PI_DRAG };
	struct EditPanel
	{
		CtlRect box, tab, exit, hudPrev, hudLbl, hudNext, cloud, share, reset, save;
		CtlRect chk, chkRow, hint, opaLbl, opaTrk, sizeLbl, sizeTrk, dpL, dpR, dpU, dpD;
		bool collapsed = false;
	};
	constexpr float kScaleMin = 0.5f, kScaleMax = 2.0f, kAlphaMin = 0.15f, kAlphaMax = 1.0f;
	constexpr float kNudge = 0.004f;       // passo das setas de posicao (fracao da tela)
	constexpr float kSnap  = 0.012f;       // "Ajuste Inteligente": distancia para alinhar com outro controle
	inline EditPanel MakeEditPanel(float sw, float sh, float ox = 0.0f, float oy = 0.0f, bool collapsed = false)
	{
		EditPanel p; p.collapsed = collapsed;
		const float w = std::min(sw * 0.30f, sh * 0.80f), Hf = sh * 0.44f;
		const float H = collapsed ? Hf * 0.215f : Hf, tabH = Hf * 0.075f;
		float l = (sw - w) * 0.5f + ox * sw, t = oy * sh;
		l = Clampf(l, 0.0f, std::max(sw - w, 0.0f)); t = Clampf(t, 0.0f, std::max(sh - H - tabH, 0.0f));
		// frações relativas ao painel EXPANDIDO (Hf); no recolhido so a linha 1 existe e usa a altura H
		auto F = [&](float x0, float y0, float x1, float y1) { return CtlRect{ l + x0 * w, t + y0 * Hf, l + x1 * w, t + y1 * Hf }; };
		p.box = CtlRect{ l, t, l + w, t + H };
		p.tab = CtlRect{ l + w * 0.43f, t + H, l + w * 0.57f, t + H + tabH };
		p.exit    = F(0.033f, 0.055f, 0.200f, 0.175f);
		p.hudPrev = F(0.217f, 0.055f, 0.380f, 0.175f);
		p.hudLbl  = F(0.383f, 0.055f, 0.615f, 0.175f);
		p.hudNext = F(0.620f, 0.055f, 0.787f, 0.175f);
		p.cloud   = F(0.790f, 0.055f, 0.967f, 0.175f);
		p.share   = F(0.033f, 0.235f, 0.335f, 0.355f);
		p.reset   = F(0.350f, 0.235f, 0.650f, 0.355f);
		p.save    = F(0.665f, 0.235f, 0.967f, 0.355f);
		p.chk     = F(0.040f, 0.400f, 0.085f, 0.485f);
		p.chkRow  = F(0.033f, 0.385f, 0.600f, 0.500f);
		p.hint    = F(0.033f, 0.530f, 0.967f, 0.620f);
		p.opaLbl  = F(0.033f, 0.640f, 0.500f, 0.730f);
		p.opaTrk  = F(0.033f, 0.735f, 0.505f, 0.800f);
		p.sizeLbl = F(0.033f, 0.825f, 0.500f, 0.915f);
		p.sizeTrk = F(0.033f, 0.920f, 0.505f, 0.985f);
		p.dpL = F(0.745f, 0.700f, 0.805f, 0.965f);
		p.dpU = F(0.815f, 0.700f, 0.905f, 0.820f);
		p.dpD = F(0.815f, 0.845f, 0.905f, 0.965f);
		p.dpR = F(0.915f, 0.700f, 0.975f, 0.965f);
		return p;
	}
	// Item tocado (so os itens visiveis: recolhido = linha 1 + aba). PI_DRAG = fundo do painel (arrasta o painel).
	inline int PanelHit(const EditPanel& P, float x, float y)
	{
		if (CtlIn(P.tab, x, y)) return PI_TAB;
		if (!CtlIn(P.box, x, y)) return PI_NONE;
		if (CtlIn(P.exit, x, y)) return PI_EXIT;
		if (CtlIn(P.hudPrev, x, y)) return PI_HUDPREV;
		if (CtlIn(P.hudNext, x, y)) return PI_HUDNEXT;
		if (CtlIn(P.cloud, x, y)) return PI_CLOUD;
		if (P.collapsed) return PI_DRAG;
		if (CtlIn(P.share, x, y)) return PI_SHARE;
		if (CtlIn(P.reset, x, y)) return PI_RESET;
		if (CtlIn(P.save, x, y)) return PI_SAVE;
		if (CtlIn(P.chkRow, x, y)) return PI_SMART;
		if (CtlIn(P.opaTrk, x, y)) return PI_OPA_TRK;
		if (CtlIn(P.sizeTrk, x, y)) return PI_SIZE_TRK;
		if (CtlIn(P.dpL, x, y)) return PI_DP_L;
		if (CtlIn(P.dpR, x, y)) return PI_DP_R;
		if (CtlIn(P.dpU, x, y)) return PI_DP_U;
		if (CtlIn(P.dpD, x, y)) return PI_DP_D;
		return PI_DRAG;
	}
	inline float ScaleToT(float v) { return Clampf((v - kScaleMin) / (kScaleMax - kScaleMin), 0.0f, 1.0f); }
	inline float AlphaToT(float v) { return Clampf((v - kAlphaMin) / (kAlphaMax - kAlphaMin), 0.0f, 1.0f); }

	// ---------------------------------------------------------------- controlador ----
	struct Output
	{
		int      lr = 0, ud = 0;          // stick (-127..127)
		uint32_t held = 0;                // bit i = botao i segurado
		uint32_t pressedEdge = 0;         // bit i = acabou de ser tocado (consumido pelo chamador)
		uint32_t releasedEdge = 0;        // bit i = acabou de ser solto
		bool     stickActive = false;
	};

	class ModernController
	{
	public:
		ModernConfig cfg;
		ModernLayout lay;
		bool editing = false;
		uint32_t disabledMask = 0;      // bit i = botao i ausente (ex.: CORRER com o LOCK ligado): nao desenha nem recebe toque
		int slotPressed = -1;           // ultima casa da barra de slots tocada (0..kHotbarN-1), lida junto com a borda de B_SLOTS
		uint32_t blockedMask = 0;       // bit i = botao i BLOQUEADO (pulando/na parede): continua desenhado (esmaecido), mas nao recebe toque novo

		// ---- editor de HUD (so com editing=true) ----
		int sel = B_ATTACK;             // controle selecionado: 0..B_COUNT-1 botao, B_COUNT joystick, -1 nenhum
		int hud = 0;                    // HUD em edicao (0..3)
		int editAction = 0;             // 1 = salvar e sair, 2 = sair sem salvar, 3 = salvar e continuar (lido e zerado pelo widget)
		int soundReq = 0;               // 1 = toque de botao, 2 = voltar/recolher (lido e zerado pelo widget -> som)
		bool collapsed = false;         // painel recolhido (so a linha 1)
		bool smart = false;             // Ajuste Inteligente: alinha ao arrastar
		bool dirty = false;             // algo mudou desde a abertura / ultimo "salvar"
		float panelOx = 0, panelOy = 0; // deslocamento do painel (fracao da tela); o jogador pode arrastar o painel
		HudLayout hudStore[kHudCount];  // copia de trabalho dos 4 HUDs durante a edicao

		ModernController() { Reset(); }

		void BeginEdit(const HudLayout* h, int cur)
		{
			for (int i = 0; i < kHudCount; i++) hudStore[i] = h[i];
			hud = ClampHud(cur); sel = B_ATTACK; editAction = 0; soundReq = 0; dirty = false;
			ApplyLayout(cfg, hudStore[hud]); lay = MakeLayout(cfg, sw, sh);
			dragFinger = -1; dragWhat = -1; panelFinger = -1; panelWhat = 0;
		}
		void StoreHud() { hudStore[hud] = ExtractLayout(cfg); }
		void MarkSaved() { dirty = false; }
		void SwitchHud(int n)
		{
			n = ClampHud(n); if (n == hud) return;
			StoreHud(); hud = n; ApplyLayout(cfg, hudStore[hud]); lay = MakeLayout(cfg, sw, sh);
		}
		void ResetHud() { ApplyLayout(cfg, HudLayout()); lay = MakeLayout(cfg, sw, sh); dirty = true; }    // so o HUD atual
		void CopyHudToAll() { StoreHud(); const HudLayout cur = hudStore[hud]; for (int i = 0; i < kHudCount; i++) hudStore[i] = cur; dirty = true; }
		bool  HasSel() const { return sel >= 0 && sel <= B_COUNT; }
		float SelScale() const { return HasSel() ? cfg.ctlScale[sel] : 1.0f; }
		float SelAlpha() const { return HasSel() ? cfg.ctlAlpha[sel] : cfg.alpha; }
		void  SetSelScale(float v) { if (!HasSel()) return; cfg.ctlScale[sel] = Clampf(v, kScaleMin, kScaleMax); lay = MakeLayout(cfg, sw, sh); dirty = true; }
		void  SetSelAlpha(float v) { if (!HasSel()) return; cfg.ctlAlpha[sel] = Clampf(v, kAlphaMin, kAlphaMax); dirty = true; }
		void  Nudge(float dx, float dy)      // setas de posicao
		{
			if (!HasSel()) return;
			CtlVec& p = (sel == B_COUNT) ? cfg.joyPos : cfg.btnPos[sel];
			p.x += dx; p.y += dy; cfg.Clamp(); lay = MakeLayout(cfg, sw, sh); dirty = true;
		}
		EditPanel Panel() const { return MakeEditPanel(sw, sh, panelOx, panelOy, collapsed); }

		void Reset()
		{
			stickFinger = -1; kx = ky = 0; dragFinger = -1; dragWhat = -1;
			for (auto& f : btnFinger) f = -1;
			pendingPressed = pendingReleased = 0;
		}

		void SetScreen(float w, float h) { sw = w; sh = h; lay = MakeLayout(cfg, w, h); }
		void ApplyConfig(const ModernConfig& c) { cfg = c; cfg.Clamp(); lay = MakeLayout(cfg, sw, sh); }

		// type: 2 push, 3 move, 1 pop (igual a AND_TouchEvent). true = o evento e nosso.
		bool OnTouch(int type, int num, float x, float y)
		{
			if (editing) return OnEditTouch(type, num, x, y);
			if (type == 2) return Push(num, x, y);
			if (type == 3) return Move(num, x, y);
			if (type == 1) return Pop(num);
			return false;
		}

		// CANCEL / perda de foco: solta tudo (nada fica "preso").
		void CancelAll()
		{
			uint32_t rel = 0;
			for (int i = 0; i < B_COUNT; i++) if (btnFinger[i] >= 0) rel |= (1u << i);
			Reset();
			pendingReleased |= rel;                // Reset() zera as bordas: registra as solturas depois
		}

		Output Read(bool consumeEdges)
		{
			Output o;
			if (stickFinger >= 0)
			{
				float ax, ay;
				StickAxes(kx, ky, lay.maxDistPx, cfg.deadzone, cfg.sens, ax, ay);
				o.lr = ToPad(ax); o.ud = ToPad(ay);
				o.stickActive = (o.lr != 0 || o.ud != 0);
			}
			for (int i = 0; i < B_COUNT; i++) if (btnFinger[i] >= 0) o.held |= (1u << i);
			o.pressedEdge = pendingPressed; o.releasedEdge = pendingReleased;
			if (consumeEdges) { pendingPressed = 0; pendingReleased = 0; }
			return o;
		}

		// posicao do botao central (para desenhar), em px
		CtlVec Knob() const
		{
			if (stickFinger < 0) return CtlVec{ lay.base.cx, lay.base.cy };
			float dx = kx, dy = ky;
			const float len = std::sqrt(dx * dx + dy * dy);
			if (len > lay.maxDistPx && len > 0) { dx *= lay.maxDistPx / len; dy *= lay.maxDistPx / len; }
			return CtlVec{ lay.base.cx + dx, lay.base.cy + dy };
		}
		bool StickDown() const { return stickFinger >= 0; }
		bool BtnDown(int b) const { return b >= 0 && b < B_COUNT && btnFinger[b] >= 0; }
		int  StickFinger() const { return stickFinger; }
		int  BtnFinger(int b) const { return btnFinger[b]; }

	private:
		float sw = 1920, sh = 1080;
		int   stickFinger = -1;
		float kx = 0, ky = 0;                       // deslocamento do dedo em relacao a base
		int   btnFinger[B_COUNT];
		uint32_t pendingPressed = 0, pendingReleased = 0;
		int   dragFinger = -1, dragWhat = -1;       // modo edicao: -2 joystick, 0..B_COUNT-1 botao

		bool Push(int num, float x, float y)
		{
			if (num < 0) return false;
			if (OwnsFinger(num)) return true;
			// 1) botoes (o mais proximo entre os acertados)
			int best = -1; float bestD = 1e30f;
			for (int i = 0; i < B_COUNT; i++)
			{
				if (btnFinger[i] >= 0 || (disabledMask & (1u << i)) || (blockedMask & (1u << i)) || !BtnHit(lay, i, x, y)) continue;
				const float dx = x - lay.btn[i].cx, dy = y - lay.btn[i].cy, d = dx * dx + dy * dy;
				if (d < bestD) { bestD = d; best = i; }
			}
			if (best >= 0) { if (best == B_SLOTS) slotPressed = HotbarIndexAt(lay.btn[B_SLOTS], x); btnFinger[best] = num; pendingPressed |= (1u << best); return true; }
			// 2) joystick
			if (stickFinger < 0)
			{
				const float dx = x - lay.base.cx, dy = y - lay.base.cy;
				if (dx * dx + dy * dy <= lay.zoneR * lay.zoneR) { stickFinger = num; kx = dx; ky = dy; return true; }
			}
			return false;
		}
		bool OwnsFinger(int num) const
		{
			if (stickFinger == num) return true;
			for (int i = 0; i < B_COUNT; i++) if (btnFinger[i] == num) return true;
			return false;
		}
		bool Move(int num, float x, float y)
		{
			if (stickFinger == num) { kx = x - lay.base.cx; ky = y - lay.base.cy; return true; }
			return OwnsFinger(num);      // dedo de botao: segue dono, mesmo fora do circulo
		}
		bool Pop(int num)
		{
			if (stickFinger == num) { stickFinger = -1; kx = ky = 0; return true; }       // volta ao centro
			for (int i = 0; i < B_COUNT; i++)
				if (btnFinger[i] == num) { btnFinger[i] = -1; pendingReleased |= (1u << i); return true; }
			return false;
		}

		// ---- editor de HUD: selecionar/arrastar o controle, painel (estilo referencia); nada de gameplay ----
		int   panelFinger = -1, panelWhat = 0;
		float grabDx = 0, grabDy = 0;

		// "Ajuste Inteligente": alinha x/y ao de outro controle proximo (fracao da tela)
		void SnapPos(float& fx, float& fy, int self) const
		{
			if (!smart) return;
			float bx = kSnap, by = kSnap;
			auto test = [&](const CtlVec& o) {
				if (std::fabs(fx - o.x) < bx) { bx = std::fabs(fx - o.x); fx = o.x; }
				if (std::fabs(fy - o.y) < by) { by = std::fabs(fy - o.y); fy = o.y; }
			};
			for (int i = 0; i < B_COUNT; i++) if (i != self && !(disabledMask & (1u << i))) test(cfg.btnPos[i]);
			if (self != -2) test(cfg.joyPos);
		}

		void PanelPush(const EditPanel& P, int what, float x, float y)
		{
			if (what != PI_NONE && what != PI_DRAG && what != PI_OPA_TRK && what != PI_SIZE_TRK) soundReq = (what == PI_EXIT || what == PI_TAB) ? 2 : 1;
			switch (what)
			{
				case PI_HUDPREV: SwitchHud(hud - 1 < 0 ? kHudCount - 1 : hud - 1); break;
				case PI_HUDNEXT: SwitchHud(hud + 1 >= kHudCount ? 0 : hud + 1); break;
				case PI_SMART:   smart = !smart; break;
				case PI_RESET:   ResetHud(); break;
				case PI_SHARE:   CopyHudToAll(); break;
				case PI_DP_L: Nudge(-kNudge, 0); break;
				case PI_DP_R: Nudge(+kNudge, 0); break;
				case PI_DP_U: Nudge(0, -kNudge); break;
				case PI_DP_D: Nudge(0, +kNudge); break;
				case PI_DRAG: grabDx = P.box.l - x; grabDy = P.box.t - y; break;      // pega o painel pelo fundo
				default: PanelMove(P, what, x, y); break;
			}
		}
		void PanelMove(const EditPanel& P, int what, float x, float y)
		{
			if (what == PI_SIZE_TRK)
			{
				const float t = Clampf((x - P.sizeTrk.l) / std::max(P.sizeTrk.r - P.sizeTrk.l, 1.0f), 0.0f, 1.0f);
				SetSelScale(kScaleMin + t * (kScaleMax - kScaleMin));
			}
			else if (what == PI_OPA_TRK)
			{
				const float t = Clampf((x - P.opaTrk.l) / std::max(P.opaTrk.r - P.opaTrk.l, 1.0f), 0.0f, 1.0f);
				SetSelAlpha(kAlphaMin + t * (kAlphaMax - kAlphaMin));
			}
			else if (what == PI_DRAG)               // arrastar o painel (fica sempre inteiro na tela: MakeEditPanel limita)
			{
				const float defL = (sw - (P.box.r - P.box.l)) * 0.5f;
				panelOx = (x + grabDx - defL) / sw;
				panelOy = (y + grabDy) / sh;
			}
		}
		void PanelPop(const EditPanel& P, int what, float x, float y)
		{
			(void)P;
			const EditPanel Q = Panel();
			if (what == PI_TAB && CtlIn(Q.tab, x, y)) collapsed = !collapsed;
			else if (what == PI_EXIT && CtlIn(Q.exit, x, y)) editAction = 2;
			else if (what == PI_SAVE && CtlIn(Q.save, x, y) && dirty) editAction = 1;
			else if (what == PI_CLOUD && CtlIn(Q.cloud, x, y)) editAction = 3;
		}

		bool OnEditTouch(int type, int num, float x, float y)
		{
			const EditPanel P = Panel();
			if (type == 2)
			{
				if (dragFinger >= 0 || panelFinger >= 0) return true;        // um dedo por vez no editor (os outros so sao consumidos)
				const int hit = PanelHit(P, x, y);
				if (hit != PI_NONE)                                           // painel: nunca "vaza" para o controle de tras
				{
					panelFinger = num; panelWhat = hit;
					PanelPush(P, hit, x, y);
					return true;
				}
				int best = -1; float bestD = 1e30f;
				for (int i = 0; i < B_COUNT; i++)
				{
					if ((disabledMask & (1u << i)) || !BtnHit(lay, i, x, y)) continue;
					const float dx = x - lay.btn[i].cx, dy = y - lay.btn[i].cy, d = dx * dx + dy * dy;
					if (d < bestD) { bestD = d; best = i; }
				}
				if (best >= 0)
				{
					dragFinger = num; dragWhat = best; sel = best; soundReq = 1;
					grabDx = lay.btn[best].cx - x; grabDy = lay.btn[best].cy - y;
					return true;
				}
				const float dx = x - lay.base.cx, dy = y - lay.base.cy;
				if (dx * dx + dy * dy <= lay.zoneR * lay.zoneR)
				{
					dragFinger = num; dragWhat = -2; sel = B_COUNT; soundReq = 1;
					grabDx = lay.base.cx - x; grabDy = lay.base.cy - y;
					return true;
				}
				return false;
			}
			if (panelFinger == num)
			{
				if (type == 3) PanelMove(P, panelWhat, x, y);
				else if (type == 1) { PanelPop(P, panelWhat, x, y); panelFinger = -1; panelWhat = 0; }
				return true;
			}
			if (dragFinger != num) return false;
			if (type == 3)
			{
				float fx = Clampf((x + grabDx) / sw, 0.0f, 1.0f), fy = Clampf((y + grabDy) / sh, 0.0f, 1.0f);
				SnapPos(fx, fy, dragWhat);
				if (dragWhat == -2) { cfg.joyPos = CtlVec{ fx, fy }; }
				else if (dragWhat >= 0) { cfg.btnPos[dragWhat] = CtlVec{ fx, fy }; }
				cfg.Clamp(); lay = MakeLayout(cfg, sw, sh); dirty = true;
				return true;
			}
			if (type == 1) { dragFinger = -1; dragWhat = -1; return true; }
			return false;
		}
	};

	// ---------------------------------------------------------------- mira arrastavel ----
	// O dedo do Ataque tambem "olha": cada movimento RELATIVO do dedo (nao a posicao absoluta) vira um arrasto num dedo
	// SINTETICO que o jogo recebe como um toque normal na regiao de camera/mira. Assim o proprio motor aplica a
	// camera/mira (sensibilidade nativa, limites verticais, recuo, animacoes, municao). O dedo sintetico usa o MESMO indice
	// do dedo do Ataque (o jogo nunca ve o dedo real, que o moderno consome). Quando o dedo sintetico chega perto da borda,
	// ele e solto e reposto no ancora (o jogo so usa o delta; nada "pula").
	struct AimEv { int type; float x, y; };      // 2 push, 3 move, 1 pop (igual a AND_TouchEvent)

	class AimDrag
	{
	public:
		float sens = 1.0f, smooth = 0.0f;
		bool  active() const { return act; }

		void SetScreen(float w, float h)
		{
			sw = w; sh = h;
			ax = w * 0.58f; ay = h * 0.38f;                 // ancora: livre de botoes (joystick a esquerda, botoes a direita)
			const float m = std::min(w, h) * 0.06f;
			minX = m; maxX = w - m; minY = m; maxY = h - m;
		}

		int Begin(float x, float y, AimEv* out)
		{
			act = true; lx = x; ly = y; fx = fy = 0; px = ax; py = ay;
			out[0] = AimEv{ 2, px, py };
			return 1;
		}

		int Move(float x, float y, AimEv* out)
		{
			if (!act) return 0;
			const float dx = x - lx, dy = y - ly;
			lx = x; ly = y;
			const float s = Clampf(smooth, 0.0f, 0.85f);
			fx = fx * s + dx * (1.0f - s);                    // suavizacao (s = 0: passa direto)
			fy = fy * s + dy * (1.0f - s);
			if (std::fabs(fx) < 1e-3f && std::fabs(fy) < 1e-3f) return 0;
			int n = 0;
			float nx = px + fx * sens, ny = py + fy * sens;
			if (nx < minX || nx > maxX || ny < minY || ny > maxY)       // vai sair da tela: solta e repoe no ancora
			{
				out[n++] = AimEv{ 1, px, py };
				px = ax; py = ay;
				out[n++] = AimEv{ 2, px, py };
				nx = px + fx * sens; ny = py + fy * sens;
			}
			px = nx; py = ny;
			out[n++] = AimEv{ 3, px, py };
			return n;
		}

		int End(AimEv* out)
		{
			if (!act) return 0;
			act = false; fx = fy = 0;
			out[0] = AimEv{ 1, px, py };
			return 1;
		}

	private:
		bool  act = false;
		float sw = 1920, sh = 1080, ax = 1100, ay = 410, minX = 0, maxX = 1920, minY = 0, maxY = 1080;
		float lx = 0, ly = 0, fx = 0, fy = 0, px = 0, py = 0;
	};

	// ---------------------------------------------------------------- seletor ---------

	struct SelectorLayout
	{
		CtlRect card[2];        // 0 classico, 1 moderno
		CtlRect selectBtn[2];   // SELECIONAR de cada cartao
		CtlRect confirm, back;
		CtlRect panel;
	};

	// Responsivo: tudo proporcional a tela (paisagem ou retrato).
	inline SelectorLayout MakeSelectorLayout(float sw, float sh)
	{
		SelectorLayout S;
		const bool land = sw >= sh;
		const float m = std::min(sw, sh) * 0.04f;
		S.panel = CtlRect{ m, m, sw - m, sh - m };
		const float pw = S.panel.r - S.panel.l, ph = S.panel.b - S.panel.t;
		const float titleH = ph * 0.14f, footH = ph * 0.15f, gap = m;
		const float top = S.panel.t + titleH, bot = S.panel.b - footH;
		if (land)
		{
			const float cw = (pw - gap * 3) * 0.5f;
			for (int i = 0; i < 2; i++) S.card[i] = CtlRect{ S.panel.l + gap + i * (cw + gap), top, S.panel.l + gap + i * (cw + gap) + cw, bot };
		}
		else
		{
			const float ch = (bot - top - gap * 3) * 0.5f;
			for (int i = 0; i < 2; i++) S.card[i] = CtlRect{ S.panel.l + gap, top + gap + i * (ch + gap), S.panel.r - gap, top + gap + i * (ch + gap) + ch };
		}
		for (int i = 0; i < 2; i++)
		{
			const CtlRect& c = S.card[i];
			const float bh = (c.b - c.t) * 0.16f, bw = (c.r - c.l) * 0.70f, cx = (c.l + c.r) * 0.5f;
			S.selectBtn[i] = CtlRect{ cx - bw * 0.5f, c.b - bh - gap * 0.6f, cx + bw * 0.5f, c.b - gap * 0.6f };
		}
		const float bw = pw * 0.22f, bh = footH * 0.62f, by = S.panel.b - footH * 0.5f - bh * 0.5f;
		S.confirm = CtlRect{ S.panel.r - gap - bw,           by, S.panel.r - gap,           by + bh };
		S.back    = CtlRect{ S.panel.r - gap * 2 - bw * 2,   by, S.panel.r - gap * 2 - bw,  by + bh };
		return S;
	}

	enum class SelAction { None, Select0, Select1, Confirm, Back };

	inline SelAction SelectorHit(const SelectorLayout& S, float x, float y)
	{
		if (CtlIn(S.confirm, x, y)) return SelAction::Confirm;
		if (CtlIn(S.back, x, y))    return SelAction::Back;
		for (int i = 0; i < 2; i++)
			if (CtlIn(S.selectBtn[i], x, y) || CtlIn(S.card[i], x, y)) return i == 0 ? SelAction::Select0 : SelAction::Select1;
		return SelAction::None;
	}

	struct SelectorState
	{
		Mode current  = Mode::Classic;   // modo gravado
		Mode selected = Mode::Classic;   // cartao marcado
		bool done = false;               // fechar a tela
		bool apply = false;              // gravar/aplicar `selected`

		void Open(Mode saved) { current = saved; selected = saved; done = false; apply = false; }
		void Act(SelAction a)
		{
			switch (a)
			{
				case SelAction::Select0: selected = Mode::Classic; break;
				case SelAction::Select1: selected = Mode::Modern;  break;
				case SelAction::Confirm: apply = true; done = true; break;
				case SelAction::Back:    apply = false; done = true; break;   // VOLTAR: nao muda nada
				default: break;
			}
		}
	};

	// A tela de escolha so aparece se o jogador ainda nao escolheu, e nunca sobre login/spawn/dialogo.
	inline bool ShouldAutoShowSelector(bool alreadyChosen, bool inGame, bool uiBlocked) { return !alreadyChosen && inGame && !uiBlocked; }

	// Modo EFETIVO: so o moderno quando escolhido E a pe e vivo; em veiculo/jetpack volta o HUD nativo (veiculo).
	inline bool ModernActiveOnFoot(Mode m, bool inVehicle, bool jetpack, bool alive) { return m == Mode::Modern && !inVehicle && !jetpack && alive; }
}
