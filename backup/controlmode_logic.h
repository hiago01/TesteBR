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
	// Interacao (entrar no veiculo) NAO esta aqui: nao existe hook de pad comprovado para forca-la; o widget nativo
	// "hud_car" continua ativo no modo moderno (ver LEIA-ME_CONTROLES.md).
	enum Btn : int { B_RUN = 0, B_JUMP, B_CROUCH, B_ATTACK, B_MIC, B_RUNLOCK, B_COUNT };
	inline const char* BtnKey(int b)
	{
		static const char* k[B_COUNT] = { "corrida", "pulo", "agachar", "ataque", "microfone", "corrida_lock" };
		return (b >= 0 && b < B_COUNT) ? k[b] : "";
	}

	struct CtlVec { float x, y; };
	struct CtlCircle { float cx, cy, r; };

	inline float Clampf(float v, float lo, float hi) { return !(v >= lo) ? lo : (v > hi ? hi : v); }

	// ---------------------------------------------------------------- config ---------
	struct ModernConfig
	{
		CtlVec   joyPos   { 0.13f, 0.72f };   // centro da base do joystick (fracao da tela)
		float joyScale = 1.0f;             // 0.5 .. 1.6
		float btnScale = 1.0f;             // 0.5 .. 1.6
		float alpha    = 0.55f;            // 0.15 .. 1.0 (transparencia)
		float sens     = 1.0f;             // 0.5 .. 2.0 (sensibilidade da intensidade)
		float deadzone = 0.12f;            // 0 .. 0.45
		float maxDist  = 1.0f;             // 0.6 .. 1.6 (distancia analogica maxima, x raio da base)
		bool  aimDrag  = true;             // Ataque tambem arrasta a mira (estilo Free Fire); desligado = Ataque so atira
		float aimSens  = 1.0f;             // 0.3 .. 3.0 (sensibilidade do arrasto da mira)
		float aimSmooth = 0.0f;            // 0 .. 0.85 (suavizacao; 0 = desligada)
		CtlVec   btnPos[B_COUNT] = {
			{ 0.77f, 0.86f },   // corrida
			{ 0.78f, 0.64f },   // pulo
			{ 0.90f, 0.88f },   // agachar
			{ 0.90f, 0.68f },   // ataque
			{ 0.93f, 0.42f },   // microfone
			{ 0.66f, 0.80f },   // corrida lock (liga/desliga: corre reto sozinho)
		};

		void Clamp()
		{
			auto fin = [](float v, float d) { return std::isfinite(v) ? v : d; };
			joyScale = Clampf(fin(joyScale, 1.0f), 0.5f, 1.6f);
			btnScale = Clampf(fin(btnScale, 1.0f), 0.5f, 1.6f);
			alpha    = Clampf(fin(alpha, 0.55f),   0.15f, 1.0f);
			sens     = Clampf(fin(sens, 1.0f),     0.5f, 2.0f);
			deadzone = Clampf(fin(deadzone, 0.12f), 0.0f, 0.45f);
			maxDist  = Clampf(fin(maxDist, 1.0f),  0.6f, 1.6f);
			aimSens  = Clampf(fin(aimSens, 1.0f),  0.3f, 3.0f);
			aimSmooth = Clampf(fin(aimSmooth, 0.0f), 0.0f, 0.85f);
			joyPos.x = Clampf(fin(joyPos.x, 0.13f), 0.05f, 0.95f);
			joyPos.y = Clampf(fin(joyPos.y, 0.72f), 0.10f, 0.95f);
			for (auto& p : btnPos) { p.x = Clampf(fin(p.x, 0.5f), 0.03f, 0.97f); p.y = Clampf(fin(p.y, 0.5f), 0.05f, 0.97f); }
		}
	};

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
		const float baseR = sh * 0.15f * c.joyScale;
		L.base      = CtlCircle{ c.joyPos.x * sw, c.joyPos.y * sh, baseR };
		L.knobR     = baseR * 0.42f;
		L.maxDistPx = baseR * c.maxDist;
		L.zoneR     = baseR * 1.55f;
		const float rb = sh * 0.058f * c.btnScale;
		for (int i = 0; i < B_COUNT; i++)
			L.btn[i] = CtlCircle{ c.btnPos[i].x * sw, c.btnPos[i].y * sh, rb * (i == B_ATTACK ? 1.45f : 1.0f) };
		return L;
	}

	inline bool Hit(const CtlCircle& c, float x, float y, float padK = 1.2f)
	{
		const float dx = x - c.cx, dy = y - c.cy, R = c.r * padK;
		return dx * dx + dy * dy <= R * R;
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
		uint32_t disabledMask = 0;      // bit i = botao i ausente (ex.: microfone com voz desligada): nao desenha nem recebe toque

		ModernController() { Reset(); }

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
				if (btnFinger[i] >= 0 || (disabledMask & (1u << i)) || !Hit(lay.btn[i], x, y)) continue;
				const float dx = x - lay.btn[i].cx, dy = y - lay.btn[i].cy, d = dx * dx + dy * dy;
				if (d < bestD) { bestD = d; best = i; }
			}
			if (best >= 0) { btnFinger[best] = num; pendingPressed |= (1u << best); return true; }
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

		// ---- edicao de posicoes: arrastar o controle; nada de gameplay ----
		bool OnEditTouch(int type, int num, float x, float y)
		{
			if (type == 2)
			{
				if (dragFinger >= 0) return true;
				int best = -1; float bestD = 1e30f;
				for (int i = 0; i < B_COUNT; i++)
				{
					if (!Hit(lay.btn[i], x, y)) continue;
					const float dx = x - lay.btn[i].cx, dy = y - lay.btn[i].cy, d = dx * dx + dy * dy;
					if (d < bestD) { bestD = d; best = i; }
				}
				if (best >= 0) { dragFinger = num; dragWhat = best; return true; }
				const float dx = x - lay.base.cx, dy = y - lay.base.cy;
				if (dx * dx + dy * dy <= lay.zoneR * lay.zoneR) { dragFinger = num; dragWhat = -2; return true; }
				return false;
			}
			if (dragFinger != num) return false;
			if (type == 3)
			{
				const float fx = Clampf(x / sw, 0.0f, 1.0f), fy = Clampf(y / sh, 0.0f, 1.0f);
				if (dragWhat == -2) { cfg.joyPos = CtlVec{ fx, fy }; }
				else if (dragWhat >= 0) { cfg.btnPos[dragWhat] = CtlVec{ fx, fy }; }
				cfg.Clamp(); lay = MakeLayout(cfg, sw, sh);
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
	struct CtlRect { float l, t, r, b; };
	inline bool CtlIn(const CtlRect& R, float x, float y) { return x >= R.l && x <= R.r && y >= R.t && y <= R.b; }

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
