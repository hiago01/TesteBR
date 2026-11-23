#include "../../main.h"
#include "../gui.h"
#include "../../game/game.h"
#include "../../game/CustomSkin.h"
#include "../../game/SkinSlots.h"
#include "../../game/ProvadorModel.h"
#include "../../game/CorpTex.h"
#include "../../net/netgame.h"
#include "provador.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

extern UI*       pUI;
extern CNetGame* pNetGame;

// definido em game/multitouch.cpp: true enquanto o evento em despacho veio de ACTION_CANCEL
extern volatile bool g_bTouchCancel;

// =====================================================================================
//  Estado global / fila de toques (thread do jogo -> thread de desenho)
// =====================================================================================
namespace
{
	struct TouchEv { int type; int num; float x; float y; bool cancel; };

	std::atomic<bool> g_open{false};
	std::atomic<bool> g_flush{false};
	std::atomic<bool> g_resetView{false};
	std::atomic<bool> g_pendingMirror{false};    // espelho (thread de desenho) de "ha previa nao salva": lido por Voltar/chat
	std::atomic<bool> g_awaitMirror{false};      // espelho de "esperando o servidor confirmar"
	std::atomic<bool> g_confirmExit{false};      // pergunta "salvar ou sair sem salvar?" na tela
	bool              g_exitAfterSave = false;   // SALVAR E SAIR: fecha quando o servidor confirmar (so thread de desenho)
	std::atomic<bool> g_claimed[4] = {{false}, {false}, {false}, {false}};
	std::mutex        g_evMutex;
	std::vector<TouchEv> g_events;

	int64_t NowMs()
	{
		using namespace std::chrono;
		return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
	}

	// ---- estado da tela (so a thread de desenho usa) ----
	typedef CCustomSkin::UiSlot  SlotState;
	using ProvadorModel::kUnavailable; using ProvadorModel::kDefault; using ProvadorModel::kEquipped;
	using ProvadorModel::kDisabled; using ProvadorModel::kCovered;

	int                  g_cat = 0;                 // categoria (SkinSlots::kUiCategories)
	int                  g_sel = -1;                // slot selecionado (indice em SkinSlots::kSlots)
	float                g_scrollList = 0.0f;
	float                g_scrollVar  = 0.0f;
	bool                 g_scrollToUsed = false;    // ao (re)entrar na pagina / trocar de item: rola lista e grade ate o item USADO e sua variante (id do <n>.png)
	std::vector<int>     g_catSlots;                // slots da categoria atual (indices em kSlots)
	CCustomSkin::UiState g_st;                      // ultimo estado REAL publicado pelo CCustomSkin
	bool                 g_haveSt = false;
	uint32_t             g_stVer  = 0;
	bool                 g_noticeInit = false;
	uint32_t             g_noticeSeq = 0;           // último aviso assíncrono já mostrado (confirmado/recusado pelo servidor, queda...)
	uint32_t             g_inflight   = 0;          // id do pedido aguardando resposta (0 = nenhum)
	int64_t              g_inflightAt = 0;
	std::string          g_okToast;                 // mostrada se o pedido em andamento der certo
	bool                 g_afterPrepare = false;    // o "refresh" em andamento veio do botao PREPARAR (/skin7 apply)
	std::string          g_toast;
	bool                 g_toastErr   = false;
	int64_t              g_toastUntil = 0;

	enum Kind { K_NONE = 0, K_CAT, K_ROW, K_VAR, K_PREV, K_NEXT, K_TOGGLE, K_APPLY, K_CANCEL, K_BACK, K_PREPARE, K_EXIT_SAVE, K_EXIT_DISCARD, K_EXIT_STAY };

	struct Hit { int kind = K_NONE; int idx = -1; };

	struct Drag
	{
		int   finger = -1;
		Hit   hit;
		int   region = 0;        // 0 nada, 1 lista de opcoes, 2 grade de variantes (rolagem)
		float sy = 0.0f, scroll0 = 0.0f;
		bool  scrolling = false;
	} g_drag;

	const ImColor cPanel (13, 16, 21, 228);
	const ImColor cPanel2(20, 24, 31, 232);
	const ImColor cLine  (44, 52, 64, 255);
	const ImColor cText  (226, 231, 237, 255);
	const ImColor cDim   (124, 133, 145, 255);
	const ImColor cBlue  (52, 140, 235, 255);
	const ImColor cBlueBg(52, 140, 235, 46);
	const ImColor cRed   (232, 92, 84, 255);
	const ImColor cOff   (30, 35, 43, 235);
	const ImColor cAmber (240, 176, 56, 255);       // prévia (ainda não confirmada)
	const ImColor cAmberBg(240, 176, 56, 46);

	void Toast(const std::string& msg, bool err)
	{
		g_toast = msg; g_toastErr = err; g_toastUntil = NowMs() + 3200;
	}

	const SkinSlots::Slot& SlotAt(int k) { return SkinSlots::kSlots[k]; }

	const SlotState* StateOf(int k)
	{
		return (g_haveSt && k >= 0 && k < (int)g_st.slots.size()) ? &g_st.slots[(size_t)k] : nullptr;
	}

	// "hair2" -> "Cabelo 2". O nome real do slot aparece no rodape; slot desconhecido mostra o proprio nome.
	std::string SlotLabel(const char* name)
	{
		struct Lbl { const char* prefix; const char* label; };
		static const Lbl kLbl[] = {
			{ "torso", "Camisa" }, { "pants", "Calça" }, { "short", "Bermuda" }, { "underwear", "Roupa íntima" },
			{ "hair", "Cabelo" }, { "head", "Rosto" }, { "glasse", "Óculos" }, { "ear", "Brinco" },
			{ "clock", "Relógio" }, { "mask", "Máscara" }, { "necklace", "Colar" }, { "hat", "Chapéu" },
			{ "boot", "Bota" }, { "slipper", "Chinelo" }, { "backpack", "Mochila" },
			{ "brow", "Sobrancelha" }, { "beard", "Barba" },
			{ "skin_torso", "Tom de pele" }, { "skin_leg", "Pele: pernas" }, { "skin_feet", "Pele: pés" },
			{ "tat_back", "Tatuagem: costas" }, { "tat_chest", "Tatuagem: peito" }, { "tat_arm", "Tatuagem: braços" },
			{ "tat_shoulder", "Tatuagem: ombros" }, { "tat_legs", "Tatuagem: pernas" },
		};
		const size_t n = std::strlen(name);
		for (const Lbl& l : kLbl)
		{
			const size_t pn = std::strlen(l.prefix);
			if (n < pn || std::strncmp(name, l.prefix, pn) != 0) continue;
			bool digits = true;
			for (size_t i = pn; i < n; ++i) if (name[i] < '0' || name[i] > '9') digits = false;
			if (!digits) continue;
			std::string s = l.label;
			if (n > pn) s += std::string(" ") + (name + pn);
			return s;
		}
		return name;
	}

	// skin_leg/skin_feet acompanham skin_torso (TOM DE PELE unico): nao aparecem como itens separados
	bool IsToneHelper(const char* name) { return std::strcmp(name, "skin_leg") == 0 || std::strcmp(name, "skin_feet") == 0; }
	bool IsToneSlotName(const char* name) { return std::strcmp(name, "skin_torso") == 0; }

	// rotulo da variante: rosto mostra so o rosto (o tom vem do corpo); demais = o proprio numero
	int VarLabel(const SlotState* u, int v) { return (u && u->varMod > 0) ? v % u->varMod : v; }

	void RebuildCategory()
	{
		g_catSlots.clear();
		for (int k = 0; k < SkinSlots::kCount; ++k)
			if (SkinSlots::UiCategoryOf(SlotAt(k).name) == g_cat && !IsToneHelper(SlotAt(k).name)) g_catSlots.push_back(k);
	}

	// Selecao inicial da categoria: o slot que ja esta visivel (estado real) ou o primeiro que existe no DFF.
	// Nao envia pedido nenhum: trocar de categoria nao muda a aparencia.
	void SelectDefault()
	{
		g_sel = -1;
		int firstOk = -1;
		for (int k : g_catSlots)
		{
			const SlotState* u = StateOf(k);
			if (!u || !u->hasMaterial) continue;
			if (firstOk < 0) firstOk = k;
			if (u->state == kEquipped) { g_sel = k; break; }
		}
		if (g_sel < 0) g_sel = firstOk;
		g_scrollList = 0.0f;
		g_scrollVar  = 0.0f;
		g_scrollToUsed = true;
	}

	void Request(const std::string& op, const char* okToast)
	{
		g_inflight   = CCustomSkin::UiRequest(op);       // 0 = fila cheia: o pedido NÃO foi enfileirado
		g_inflightAt = NowMs();
		g_okToast    = okToast ? okToast : "";
		if (!g_inflight) Toast("Sistema de skin ocupado, tente de novo", true);
	}

	void PreviewSlot(int k, const std::string& what)
	{
		Request(std::string("prev ") + SlotAt(k).name + " " + what, nullptr);
	}

	int VariantPos(const SlotState& u)
	{
		for (size_t i = 0; i < u.variants.size(); ++i) if (u.variants[i] == u.variant) return (int)i;
		return -1;
	}

	std::string InfoText(const SlotState* u, int k)
	{
		if (!u || k < 0) return "Selecione um item na lista";
		std::string s = SlotLabel(SlotAt(k).name) + " (" + SlotAt(k).name + ")";
		if (!u->hasMaterial) return s + "  -  indisponível no modelo";
		switch (u->state)
		{
			case kEquipped:
				if (u->varMod > 0) s += "  -  USADO ATUALMENTE, rosto " + std::to_string(u->variant % u->varMod) + " (tom " + std::to_string(u->variant / u->varMod) + ")";
				else               s += "  -  USADO ATUALMENTE, variante " + std::to_string(u->variant);
				break;
			case kDisabled: s += "  -  desligado"; break;
			case kCovered:  s += "  -  coberto por outra peça"; break;
			default:        s += u->variants.empty() ? "  -  sem variantes no pack" : "  -  padrão, nada equipado (" + std::to_string(u->variants.size()) + " variantes)"; break;
		}
		if (u->previewed) s += "  -  PRÉVIA";
		else if (u->srvKnown) s += std::string("  -  servidor: ") + ProvadorModel::SrvWord(ProvadorModel::Srv{true, (uint8_t)u->srvEst, (uint16_t)u->srvVar}) + (u->srvEst == 2 ? " " + std::to_string(u->varMod > 0 ? u->srvVar % u->varMod : u->srvVar) : std::string());
		return s;
	}
}


// =====================================================================================
//  /provador FEMININO (modelos 9 = FemininaNormal e 272 = policial feminina)
//  Usa a BASE sincronizada (CorpTex, RPC 231): pele/rosto, cabelo, camisa, calca, calcado, intima (9) ou corp (272).
//  Previa so no seu ator; APLICAR envia "/lookx base ..." ao servidor (autoridade); CANCELAR/sair sem salvar volta ao que estava.
//  Nao usa o motor de slots do /skin7 (CustomSkin) -- o provador masculino (7/271) continua exatamente como era.
// =====================================================================================
namespace
{
	bool          g_fem = false;                 // /provador aberto no modo feminino
	int           g_fModel = 0;                  // 9 ou 272
	CorpTex::Base g_fOrig, g_fWork;              // original (o que o servidor tem) x em edicao
	int           g_fCat = 0, g_fRow = 0;        // categoria e linha (valor) selecionada
	float         g_fScrollList = 0.0f, g_fScrollVar = 0.0f;
	std::vector<std::string> g_fCorps;

	enum FemKind { FK_FACE = 0, FK_HAIR, FK_SHIRT, FK_PANTS, FK_BOOT, FK_UNDER, FK_CORP, FK_MAKEUP, FK_BROW, FK_ACC, FK_TAT };
	struct FemCat { const char* title; FemKind kind; };
	const FemCat kFemCats9[]   = { {"PELE E ROSTO", FK_FACE}, {"CABELO", FK_HAIR}, {"CAMISA", FK_SHIRT}, {"CALÇA", FK_PANTS}, {"CALÇADO", FK_BOOT},
	                               {"MAQUIAGEM", FK_MAKEUP}, {"SOBRANCELHA", FK_BROW}, {"ACESSÓRIOS", FK_ACC}, {"TATUAGEM", FK_TAT} };
	const FemCat kFemCats272[] = { {"PELE E ROSTO", FK_FACE}, {"CABELO", FK_HAIR}, {"MAQUIAGEM", FK_MAKEUP}, {"SOBRANCELHA", FK_BROW}, {"CORP", FK_CORP} };
	int            FemCatCount() { return g_fModel == 272 ? 5 : 9; }
	// categoria de extras (CorpTex::Base::ex) de uma linha: acessorios 2..5 (brinco, relogio, mascara, mochila) | tatuagens 6..11 (peito, costas, braco E/D, perna E/D)
	int FemExK(int kind, int row) { return kind == FK_ACC ? 1 + row : kind == FK_TAT ? 5 + row : -1; }
	const FemCat&  FemCatAt(int i) { return g_fModel == 272 ? kFemCats272[i] : kFemCats9[i]; }

	struct FemRow { std::string label; int val; };

	std::vector<FemRow> FemRows(FemKind kind)
	{
		std::vector<FemRow> v;
		switch (kind)
		{
			case FK_FACE:  for (int t = 1; t <= 6; ++t) v.push_back({"Tom de pele " + std::to_string(t), t}); break;
			case FK_HAIR:  v.push_back({"Sem cabelo", 0}); for (int t = 1; t <= 5; ++t) v.push_back({"Estilo " + std::to_string(t), t}); break;
			// nomes = config.lua (clothesManage[9]); pasta 1 de calca/camisa = calcinha/sutia (roupa de baixo padrao)
			case FK_SHIRT: { static const char* n[6] = {"Top (sutiã)", "Blusa cropped", "Cropped", "Cropped c/ decote", "Camiseta", "Moletom"};
			                 v.push_back({"Padrão (sutiã 1)", 0}); for (int t = 1; t <= 6; ++t) v.push_back({std::to_string(t) + " - " + n[t - 1], t}); break; }
			case FK_PANTS: { static const char* n[6] = {"Calcinha", "Jeans", "Moletom", "Cargo", "Rasgada", "Shorts"};
			                 v.push_back({"Padrão (calcinha 1)", 0}); for (int t = 1; t <= 6; ++t) v.push_back({std::to_string(t) + " - " + n[t - 1], t}); break; }
			case FK_BOOT:  { static const char* n[7] = {"Chinelo", "Chinelo slide", "Slide c/ meia", "Tênis sneaker", "Tênis esportivo", "Tênis regular", "Bota jump"};
			                 v.push_back({"Descalça", 0}); for (int t = 1; t <= 7; ++t) v.push_back({std::to_string(t) + " - " + n[t - 1], t}); break; }
			case FK_UNDER: v.push_back({"Padrão (preta)", 0}); for (int t = 1; t <= 6; ++t) v.push_back({"Cor " + std::to_string(t), t}); v.push_back({"Nenhuma", 9}); break;
			case FK_CORP:  v.push_back({"(sem corp)", 0}); for (size_t i = 0; i < g_fCorps.size(); ++i) v.push_back({g_fCorps[i], (int)i + 1}); break;
			// extras do config.lua (faceshapes / clothes ear,clock,mask,backpack / tattoo): linha = item (maquiagem, sobrancelha) ou tipo (acessorio, tatuagem; item na grade)
			case FK_MAKEUP: v.push_back({"Sem maquiagem", 0}); for (int i = 1; i <= CorpTex::FemExtraCount(1); ++i) v.push_back({"Maquiagem " + std::to_string(i), i}); break;
			case FK_BROW:   v.push_back({"Padrão (1)", 0}); for (int i = 1; i <= CorpTex::FemExtraCount(0); ++i) v.push_back({"Sobrancelha " + std::to_string(i), i}); break;
			case FK_ACC: { static const char* n[4] = {"Brinco", "Relógio", "Máscara", "Mochila"};
			               v.push_back({"Remover todos", 0}); for (int t = 1; t <= 4; ++t) v.push_back({n[t - 1], t}); break; }
			case FK_TAT: { static const char* n[6] = {"Peito", "Costas", "Braço esquerdo", "Braço direito", "Perna esquerda", "Perna direita"};
			               v.push_back({"Remover todas", 0}); for (int t = 1; t <= 6; ++t) v.push_back({n[t - 1], t}); break; }
		}
		return v;
	}

	int FemGridCount(FemKind kind, int row)
	{
		switch (kind)
		{
			case FK_FACE:  return CorpTex::FemCount(0, row);
			case FK_HAIR:  return CorpTex::FemCount(1, row);
			case FK_BOOT:  return CorpTex::FemCount(2, row);
			case FK_PANTS: return CorpTex::FemCount(3, row);
			case FK_SHIRT: return CorpTex::FemCount(4, row);
			case FK_ACC: case FK_TAT: return row > 0 ? CorpTex::FemExtraCount(FemExK(kind, row)) : 0;
			default:       return 0;
		}
	}

	// valor (linha) e variante atuais da categoria, segundo a base em edicao
	int FemCurRow(FemKind kind)
	{
		const CorpTex::Base& b = g_fWork;
		switch (kind)
		{
			case FK_FACE: return b.tone;        case FK_HAIR: return b.hairStyle;  case FK_SHIRT: return b.shirtSet;
			case FK_PANTS: return b.pantsSet;   case FK_BOOT: return b.bootSet;   case FK_UNDER: return b.under;
			case FK_CORP:
				for (size_t i = 0; i < g_fCorps.size(); ++i) if (g_fCorps[i] == b.corp) return (int)i + 1;
				return 0;
			case FK_MAKEUP: return b.ex[1];     case FK_BROW: return b.ex[0];
			case FK_ACC: for (int t = 1; t <= 4; ++t) if (b.ex[FemExK(kind, t)]) return t; return 0;
			case FK_TAT: for (int t = 1; t <= 6; ++t) if (b.ex[FemExK(kind, t)]) return t; return 0;
		}
		return 0;
	}
	// a linha 'val' tem item equipado? (acessorios/tatuagens: varios tipos podem estar em uso ao mesmo tempo)
	bool FemRowUsed(FemKind kind, int val, int curRow)
	{
		if (kind == FK_ACC || kind == FK_TAT) return val > 0 && g_fWork.ex[FemExK(kind, val)] != 0;
		return val == curRow;
	}
	int FemCurVar(FemKind kind)
	{
		const CorpTex::Base& b = g_fWork;
		switch (kind)
		{
			case FK_FACE: return b.face;        case FK_HAIR: return b.hairColor;  case FK_SHIRT: return b.shirtVar;
			case FK_PANTS: return b.pantsVar;   case FK_BOOT: return b.bootVar;
			case FK_ACC: case FK_TAT: return g_fRow > 0 ? b.ex[FemExK(kind, g_fRow)] : 0;
			default: return 0;
		}
	}

	void FemPreview() { CorpTex::PreviewLocalBase(g_fWork); }

	void FemSetRow(FemKind kind, int row)
	{
		CorpTex::Base& b = g_fWork;
		auto clampVar = [&](int cnt, uint8_t cur) -> uint8_t { return row ? (uint8_t)std::clamp<int>(cur ? cur : 1, 1, std::max(cnt, 1)) : (uint8_t)0; };
		switch (kind)
		{
			case FK_FACE:  b.tone = (uint8_t)row; b.face = clampVar(FemGridCount(kind, row), b.face); break;
			case FK_HAIR:  b.hairStyle = (uint8_t)row; b.hairColor = clampVar(FemGridCount(kind, row), b.hairColor); break;
			case FK_SHIRT: b.shirtSet = (uint8_t)row; b.shirtVar = row ? 1 : 0; break;
			case FK_PANTS: b.pantsSet = (uint8_t)row; b.pantsVar = row ? 1 : 0; break;
			case FK_BOOT:  b.bootSet = (uint8_t)row; b.bootVar = row ? 1 : 0; break;
			case FK_UNDER: b.under = (uint8_t)row; break;
			case FK_MAKEUP: b.ex[1] = (uint8_t)row; break;
			case FK_BROW:   b.ex[0] = (uint8_t)row; break;
			case FK_ACC:    if (row == 0) for (int i = 2; i <= 5; ++i) b.ex[i] = 0; break;     // linha 0 = remove todos; senao so seleciona o tipo (item na grade)
			case FK_TAT:    if (row == 0) for (int i = 6; i <= 11; ++i) b.ex[i] = 0; break;
			case FK_CORP:  std::memset(b.corp, 0, sizeof(b.corp)); if (row >= 1 && row <= (int)g_fCorps.size()) std::strncpy(b.corp, g_fCorps[(size_t)row - 1].c_str(), sizeof(b.corp) - 1); break;
		}
		FemPreview();
	}

	void FemSetVar(FemKind kind, int row, int var)
	{
		CorpTex::Base& b = g_fWork;
		switch (kind)
		{
			case FK_FACE:  b.tone = (uint8_t)row; b.face = (uint8_t)var; break;
			case FK_HAIR:  b.hairStyle = (uint8_t)row; b.hairColor = (uint8_t)var; break;
			case FK_SHIRT: b.shirtSet = (uint8_t)row; b.shirtVar = (uint8_t)var; break;
			case FK_PANTS: b.pantsSet = (uint8_t)row; b.pantsVar = (uint8_t)var; break;
			case FK_BOOT:  b.bootSet = (uint8_t)row; b.bootVar = (uint8_t)var; break;
			case FK_ACC: case FK_TAT: { const int k = FemExK(kind, row); if (k < 0 || k > 11) return; b.ex[k] = (b.ex[k] == (uint8_t)var) ? (uint8_t)0 : (uint8_t)var; break; }   // tocar de novo remove
			default: return;
		}
		FemPreview();
	}

	void FemOpen(int model)
	{
		g_fem = true; g_fModel = model;
		g_fCat = 0; g_fScrollList = 0.0f; g_fScrollVar = 0.0f;
		g_fCorps = CorpTex::FemCorps();
		CorpTex::GetLocalBase(g_fOrig);
		g_fWork = g_fOrig;
		g_fRow = FemCurRow(FemCatAt(0).kind);
		g_toastUntil = 0; g_confirmExit.store(false);
	}

	void FemRestore() { if (g_fWork != g_fOrig) { g_fWork = g_fOrig; FemPreview(); } }

	// desenha + trata toques. Retorna true se a tela foi fechada neste frame.
	bool DrawFem(ImGuiRenderer* r)
	{
		const ImVec2 scr = pUI ? pUI->displaySize() : ImVec2(1920, 1080);
		const float  k   = scr.y / 540.0f;
		const bool pending = g_fWork != g_fOrig;
		g_pendingMirror.store(pending);
		g_awaitMirror.store(false);
		if (!pending && g_confirmExit.load()) g_confirmExit.store(false);
		const bool confirmExit = g_confirmExit.load();

		const float m = 8.0f * k, hdrY = 4.0f * k, hdrH = 38.0f * k, top = hdrY + hdrH + 6.0f * k, bot = scr.y - m;
		const float catW = std::clamp(scr.x * 0.14f, 105.0f * k, 150.0f * k);
		const float panW = std::clamp(scr.x * 0.30f, 210.0f * k, 300.0f * k);
		const float catX = m, panX = scr.x - m - panW, cx0 = catX + catW + m, cx1 = panX - m;
		const float pad = 8.0f * k, fs = 12.0f * k, fsS = 10.5f * k;
		const float catH = 34.0f * k, catGap = 4.0f * k, titleH = 20.0f * k, rowH = 30.0f * k;
		const float listTop = top + titleH, listBot = top + (bot - top) * 0.52f;
		const float varTop = listBot + 4.0f * k + titleH, varBot = bot;
		const int   cols = 5;
		const float cellGap = 4.0f * k, cellW = (panW - 2.0f * pad - (cols - 1) * cellGap) / cols, cellH = 30.0f * k;
		const float gap = 6.0f * k, barY = bot - 104.0f * k, rowAY = barY, rowCY = barY + 68.0f * k, rowCH = 34.0f * k;
		const float bw = cx1 - cx0, cbw = (bw - 2.0f * gap) / 3.0f;
		const ImVec2 apA(cx0, rowCY), apB(cx0 + cbw, rowCY + rowCH), caA(apB.x + gap, rowCY), caB(caA.x + cbw, rowCY + rowCH), bkA(caB.x + gap, rowCY), bkB(cx1, rowCY + rowCH);

		const FemKind kind = FemCatAt(g_fCat).kind;
		const std::vector<FemRow> rows = FemRows(kind);
		const int nRows = (int)rows.size();
		const int nVars = FemGridCount(kind, g_fRow);
		const float listMax = std::max(0.0f, nRows * rowH - (listBot - listTop));
		const int   varRows = (nVars + cols - 1) / cols;
		const float varMax = std::max(0.0f, varRows * (cellH + cellGap) - (varBot - varTop));
		g_fScrollList = std::clamp(g_fScrollList, 0.0f, listMax);
		g_fScrollVar  = std::clamp(g_fScrollVar, 0.0f, varMax);

		const float exW = std::min(scr.x * 0.7f, 460.0f * k), exH = 132.0f * k, exX0 = (scr.x - exW) * 0.5f, exY0 = (scr.y - exH) * 0.5f;
		const float exBw = (exW - 4.0f * gap) / 3.0f, exBy = exY0 + exH - 46.0f * k, exBh = 34.0f * k;
		const ImVec2 exSaveA(exX0 + gap, exBy), exSaveB(exSaveA.x + exBw, exBy + exBh), exDiscA(exSaveB.x + gap, exBy), exDiscB(exDiscA.x + exBw, exBy + exBh),
		             exStayA(exDiscB.x + gap, exBy), exStayB(exStayA.x + exBw, exBy + exBh);

		auto inRect = [](float x, float y, float ax, float ay, float bx, float by) { return x >= ax && x <= bx && y >= ay && y <= by; };
		auto inBox = [&](float x, float y, const ImVec2& a, const ImVec2& b) { return inRect(x, y, a.x, a.y, b.x, b.y); };

		auto hitTest = [&](float x, float y, int& region) -> Hit {
			Hit h; region = 0;
			if (confirmExit)
			{
				if (inBox(x, y, exSaveA, exSaveB)) h.kind = K_EXIT_SAVE;
				else if (inBox(x, y, exDiscA, exDiscB)) h.kind = K_EXIT_DISCARD;
				else if (inBox(x, y, exStayA, exStayB)) h.kind = K_EXIT_STAY;
				return h;
			}
			if (inBox(x, y, bkA, bkB)) { h.kind = K_BACK; return h; }
			if (inBox(x, y, apA, apB)) { h.kind = K_APPLY; return h; }
			if (inBox(x, y, caA, caB)) { h.kind = K_CANCEL; return h; }
			if (inRect(x, y, catX, top, catX + catW, top + FemCatCount() * (catH + catGap)))
			{
				const int i = (int)((y - top) / (catH + catGap));
				if (i >= 0 && i < FemCatCount()) { h.kind = K_CAT; h.idx = i; return h; }
			}
			if (inRect(x, y, panX, listTop, panX + panW, listBot))
			{
				region = 1;
				const int i = (int)std::floor((y - listTop + g_fScrollList) / rowH);
				if (i >= 0 && i < nRows) { h.kind = K_ROW; h.idx = i; }
				return h;
			}
			if (inRect(x, y, panX, varTop, panX + panW, varBot))
			{
				region = 2;
				const float lx = x - (panX + pad), ly = y - varTop + g_fScrollVar;
				if (lx >= 0.0f && ly >= 0.0f)
				{
					const int c = (int)(lx / (cellW + cellGap)), rr = (int)(ly / (cellH + cellGap)), i = rr * cols + c;
					if (c < cols && (lx - c * (cellW + cellGap)) <= cellW && (ly - rr * (cellH + cellGap)) <= cellH && i >= 0 && i < nVars) { h.kind = K_VAR; h.idx = i; }
				}
				return h;
			}
			return h;
		};
		auto sameHit = [](const Hit& a, const Hit& b) { return a.kind == b.kind && a.idx == b.idx; };

		auto doAction = [&](const Hit& h) -> bool {
			switch (h.kind)
			{
				case K_BACK:
					if (pending) { g_confirmExit.store(true); break; }
					Provador::Close();
					return true;
				case K_EXIT_SAVE:
					g_confirmExit.store(false);
					if (!CorpTex::SendLocalBase(g_fWork)) { Toast("Sem conexão com o servidor: não foi possível salvar", true); break; }
					g_fOrig = g_fWork;
					Provador::Close();
					return true;
				case K_EXIT_DISCARD:
					g_confirmExit.store(false);
					Provador::Close();                 // Close() restaura a prévia
					return true;
				case K_EXIT_STAY: g_confirmExit.store(false); break;
				case K_CAT:
					if (h.idx != g_fCat && h.idx >= 0 && h.idx < FemCatCount()) { g_fCat = h.idx; g_fRow = FemCurRow(FemCatAt(h.idx).kind); g_fScrollList = g_fScrollVar = 0.0f; }
					break;
				case K_ROW:
					if (h.idx < 0 || h.idx >= nRows) break;
					g_fRow = rows[(size_t)h.idx].val; g_fScrollVar = 0.0f;
					FemSetRow(kind, g_fRow);
					break;
				case K_VAR:
					if (h.idx < 0 || h.idx >= nVars || g_fRow <= 0) break;
					FemSetVar(kind, g_fRow, h.idx + 1);
					break;
				case K_APPLY:
					if (!pending) break;
					if (!CorpTex::SendLocalBase(g_fWork)) { Toast("Sem conexão com o servidor: não foi possível aplicar", true); break; }
					g_fOrig = g_fWork;
					Toast("Aparência enviada ao servidor", false);
					break;
				case K_CANCEL:
					if (!pending) break;
					FemRestore(); g_fRow = FemCurRow(kind);
					Toast("Prévia cancelada", false);
					break;
			}
			return false;
		};

		std::vector<TouchEv> evs;
		{ std::lock_guard<std::mutex> l(g_evMutex); evs.swap(g_events); }
		for (const TouchEv& e : evs)
		{
			if (e.type == 2 && g_drag.finger < 0)
			{
				g_drag = Drag(); g_drag.finger = e.num;
				int region = 0; g_drag.hit = hitTest(e.x, e.y, region); g_drag.region = region;
				g_drag.sy = e.y; g_drag.scroll0 = (region == 1) ? g_fScrollList : g_fScrollVar;
			}
			else if (e.num == g_drag.finger && e.type == 3)
			{
				if (g_drag.region != 0)
				{
					if (std::fabs(e.y - g_drag.sy) > 10.0f * k) g_drag.scrolling = true;
					if (g_drag.scrolling)
					{
						if (g_drag.region == 1) g_fScrollList = std::clamp(g_drag.scroll0 - (e.y - g_drag.sy), 0.0f, listMax);
						else                    g_fScrollVar  = std::clamp(g_drag.scroll0 - (e.y - g_drag.sy), 0.0f, varMax);
					}
				}
			}
			else if (e.num == g_drag.finger && e.type == 1)
			{
				const bool ok = !e.cancel && !g_drag.scrolling && g_drag.hit.kind != K_NONE;
				Hit up; int reg2 = 0;
				if (ok) up = hitTest(e.x, e.y, reg2);
				const bool same = ok && sameHit(up, g_drag.hit);
				const Hit act = g_drag.hit;
				g_drag = Drag();
				if (same && doAction(act)) return true;
			}
		}

		// ---- pintura ----
		r->drawRect(ImVec2(m, hdrY), ImVec2(scr.x - m, hdrY + hdrH), cPanel, true);
		r->drawRect(ImVec2(m, hdrY + hdrH - 2.0f * k), ImVec2(scr.x - m, hdrY + hdrH), cBlue, true);
		r->drawText(ImVec2(m + 10.0f * k, hdrY + 4.0f * k), cBlue, g_fModel == 272 ? "/PROVADOR - SKIN 272" : "/PROVADOR - SKIN 9", false, fsS);
		r->drawText(ImVec2(m + 10.0f * k, hdrY + 16.0f * k), cText, "PERSONALIZAÇÃO", false, 17.0f * k);

		auto button = [&](const ImVec2& a, const ImVec2& b, const char* text, bool enabled, bool accent, float size) {
			r->drawRect(a, b, enabled ? (accent ? cBlue : cPanel2) : cOff, true);
			if (enabled && !accent) r->drawRect(a, b, cLine, false);
			const ImVec2 ts = r->calculateTextSize(text, size);
			r->drawText(ImVec2((a.x + b.x - ts.x) * 0.5f, (a.y + b.y - ts.y) * 0.5f), enabled ? cText : cDim, text, false, size);
		};

		for (int i = 0; i < FemCatCount(); ++i)
		{
			const float ay = top + i * (catH + catGap);
			const bool act = (i == g_fCat);
			r->drawRect(ImVec2(catX, ay), ImVec2(catX + catW, ay + catH), act ? cPanel2 : cPanel, true);
			if (act)
			{
				r->drawRect(ImVec2(catX, ay), ImVec2(catX + 3.0f * k, ay + catH), cBlue, true);
				r->drawRect(ImVec2(catX, ay), ImVec2(catX + catW, ay + catH), cBlueBg, true);
			}
			const char* t = FemCatAt(i).title;
			const ImVec2 ts = r->calculateTextSize(t, fs);
			r->drawText(ImVec2(catX + 12.0f * k, ay + (catH - ts.y) * 0.5f), act ? cText : cDim, t, false, fs);
		}

		r->drawRect(ImVec2(panX, top), ImVec2(panX + panW, bot), cPanel, true);
		r->drawText(ImVec2(panX + pad, top + 4.0f * k), cBlue, FemCatAt(g_fCat).title, false, fsS);
		r->drawRect(ImVec2(panX, listTop - 2.0f * k), ImVec2(panX + panW, listTop - 1.0f * k), cLine, true);

		const int curRow = FemCurRow(kind);
		r->pushClipRect(ImVec2(panX, listTop), ImVec2(panX + panW, listBot), true);
		for (int i = 0; i < nRows; ++i)
		{
			const float ry = listTop + i * rowH - g_fScrollList;
			if (ry + rowH < listTop || ry > listBot) continue;
			const bool sel = rows[(size_t)i].val == g_fRow, used = FemRowUsed(kind, rows[(size_t)i].val, curRow);
			if (sel) { r->drawRect(ImVec2(panX, ry), ImVec2(panX + panW, ry + rowH), cBlueBg, true); r->drawRect(ImVec2(panX, ry), ImVec2(panX + 3.0f * k, ry + rowH), cBlue, true); }
			else if (i & 1) r->drawRect(ImVec2(panX, ry), ImVec2(panX + panW, ry + rowH), ImColor(255, 255, 255, 8), true);
			r->drawText(ImVec2(panX + pad + 4.0f * k, ry + (rowH - fs) * 0.5f), used ? cText : cDim, rows[(size_t)i].label, false, fs);
			if (used) { const char* u = "USADO"; const ImVec2 us = r->calculateTextSize(u, fsS); r->drawText(ImVec2(panX + panW - pad - us.x, ry + (rowH - us.y) * 0.5f), cBlue, u, false, fsS); }
		}
		r->popClipRect();

		r->drawRect(ImVec2(panX, listBot + 2.0f * k), ImVec2(panX + panW, listBot + 3.0f * k), cLine, true);
		r->drawText(ImVec2(panX + pad, listBot + 6.0f * k), cBlue, nVars ? "VARIANTES" : "SEM VARIANTES", false, fsS);
		const int curVar = FemCurVar(kind);
		r->pushClipRect(ImVec2(panX, varTop), ImVec2(panX + panW, varBot), true);
		for (int i = 0; i < nVars; ++i)
		{
			const int c = i % cols, rr = i / cols;
			const float x0 = panX + pad + c * (cellW + cellGap), y0 = varTop + rr * (cellH + cellGap) - g_fScrollVar;
			if (y0 + cellH < varTop || y0 > varBot) continue;
			const bool used = (kind == FK_ACC || kind == FK_TAT) ? (i + 1 == curVar) : ((g_fRow == curRow) && (i + 1 == curVar));
			r->drawRect(ImVec2(x0, y0), ImVec2(x0 + cellW, y0 + cellH), used ? cBlue : cPanel2, true);
			if (!used) r->drawRect(ImVec2(x0, y0), ImVec2(x0 + cellW, y0 + cellH), cLine, false);
			const std::string n = std::to_string(i + 1);
			const ImVec2 ts = r->calculateTextSize(n, fsS);
			r->drawText(ImVec2(x0 + (cellW - ts.x) * 0.5f, y0 + (cellH - ts.y) * 0.5f), used ? cText : cDim, n, false, fsS);
		}
		r->popClipRect();

		{
			std::string msg; ImColor mc = cDim;
			if (NowMs() < g_toastUntil) { msg = g_toast; mc = g_toastErr ? cRed : cBlue; }
			else if (pending) { msg = "Prévia (não salva): toque em APLICAR para salvar no servidor"; mc = cAmber; }
			else msg = (kind == FK_ACC || kind == FK_TAT) ? "Escolha o tipo na lista e o item na grade (toque de novo para remover)" : "Escolha um item na lista e a variante na grade";
			r->pushClipRect(ImVec2(cx0, rowAY), ImVec2(cx1, rowAY + 26.0f * k), true);
			r->drawText(ImVec2(cx0 + pad, rowAY + 6.0f * k), mc, msg, false, fs);
			r->popClipRect();
		}
		button(apA, apB, "APLICAR", pending, true, fs);
		button(caA, caB, "CANCELAR", pending, false, fs);
		button(bkA, bkB, "VOLTAR", true, false, fs);

		if (confirmExit)
		{
			r->drawRect(ImVec2(0.0f, 0.0f), scr, ImColor(0, 0, 0, 170), true);
			r->drawRect(ImVec2(exX0, exY0), ImVec2(exX0 + exW, exY0 + exH), cPanel, true);
			r->drawRect(ImVec2(exX0, exY0), ImVec2(exX0 + exW, exY0 + exH), cAmber, false);
			const char* t1 = "Sair sem salvar?";
			const char* t2 = "Você tem uma prévia que ainda não foi salva.";
			const ImVec2 s1 = r->calculateTextSize(t1, 16.0f * k), s2 = r->calculateTextSize(t2, fsS);
			r->drawText(ImVec2(exX0 + (exW - s1.x) * 0.5f, exY0 + 12.0f * k), cText, t1, false, 16.0f * k);
			r->drawText(ImVec2(exX0 + (exW - s2.x) * 0.5f, exY0 + 40.0f * k), cDim, t2, false, fsS);
			button(exSaveA, exSaveB, "SALVAR E SAIR", true, true, fsS);
			button(exDiscA, exDiscB, "SAIR SEM SALVAR", true, false, fsS);
			button(exStayA, exStayB, "CONTINUAR", true, false, fsS);
		}
		return false;
	}
}

// =====================================================================================
//  API estatica
// =====================================================================================
bool Provador::IsOpen() { return g_open.load(); }

void Provador::Open()
{
	if (g_open.load()) return;
	{
		std::lock_guard<std::mutex> l(g_evMutex);
		g_events.clear();
	}
	for (auto& c : g_claimed) c.store(false);
	{   // skin feminina (9/272): abre o provador feminino (base sincronizada); 7/271 e as demais seguem no provador de sempre
		const int lm = CorpTex::LocalModel();
		if (lm == 9 || lm == 272)
		{
			g_drag = Drag();
			FemOpen(lm);
			g_flush.store(true);
			g_pendingMirror.store(false);
			g_awaitMirror.store(false);
			g_open.store(true);
			return;
		}
	}
	g_fem = false;
	CCustomSkin::UiSetActive(true);        // antes do pedido: o estado e republicado depois de cada comando
	g_resetView.store(true);               // a thread de desenho zera a tela e pede o estado real
	g_flush.store(true);                   // o hook solta os dedos que o jogo considera pressionados
	g_confirmExit.store(false);
	g_pendingMirror.store(false);
	g_awaitMirror.store(false);
	g_open.store(true);
}

void Provador::Close()
{
	if (!g_open.exchange(false)) return;
	g_confirmExit.store(false);
	{
		std::lock_guard<std::mutex> l(g_evMutex);
		g_events.clear();
	}
	// g_claimed e mantido: o dedo que ainda esta na tela continua sendo engolido ate o UP.
	if (g_fem)                              // provador feminino: desfaz a previa nao salva e sai (nao mexe no CCustomSkin)
	{
		FemRestore();
		g_fem = false;
		g_pendingMirror.store(false);
		return;
	}
	CCustomSkin::UiRequest("cancel");      // desfaz a previa nao confirmada (nao faz nada se nao houver)
	CCustomSkin::UiSetActive(false);
}

// Voltar / "/provador" / "/provador fechar": se ha previa nao salva, NAO fecha: abre a pergunta. Esperando o servidor: ignora
// (o tempo de espera e limitado pelo CCustomSkin; fechar agora perderia o resultado do salvamento).
void Provador::RequestClose()
{
	if (!g_open.load()) return;
	if (g_confirmExit.load()) return;
	if (g_awaitMirror.load()) return;
	if (g_pendingMirror.load()) { g_confirmExit.store(true); return; }
	Close();
}

void Provador::Toggle() { if (g_open.load()) RequestClose(); else Open(); }

bool Provador::ConsumeFlushRequest() { return g_flush.exchange(false); }

bool Provador::OnBackPressed()
{
	// Debounce: onKeyDown e onBackPressed podem ambos disparar no mesmo toque; o segundo nao pode cair
	// no MaximoMenu (abriria o menu logo apos fechar esta tela).
	static int64_t s_last = 0;
	const int64_t now = NowMs();
	if (now - s_last < 400) return true;
	if (!g_open.load()) return false;
	s_last = now;
	RequestClose();
	return true;
}

bool Provador::HandleRawTouch(int type, int num, int x, int y)
{
	const int n = num & 3;
	const bool open = g_open.load();
	if (!open && !g_claimed[n].load()) return false;     // fluxo normal do jogo

	if (open)
	{
		g_claimed[n].store(true);
		std::lock_guard<std::mutex> l(g_evMutex);
		if (g_events.size() < 256) g_events.push_back({type, n, (float)x, (float)y, g_bTouchCancel});
	}
	if (type == 1) g_claimed[n].store(false);             // pop/cancel: dedo solto
	return true;                                          // consumido: nada chega ao GTA/SA-MP
}

Provador::Provador() : Widget() {}

// =====================================================================================
//  Desenho + tratamento dos toques
// =====================================================================================
void Provador::draw(ImGuiRenderer* r)
{
	if (!g_open.load())
	{
		g_drag = Drag();
		return;
	}

	if (g_fem) { DrawFem(r); return; }      // provador feminino (9/272)

	const ImVec2 scr = pUI ? pUI->displaySize() : ImVec2(1920, 1080);
	const float  k   = scr.y / 540.0f;

	// ---- abertura: zera a tela e pede o estado real ----
	if (g_resetView.exchange(false))
	{
		g_drag = Drag();
		g_cat = 0; g_sel = -1; g_haveSt = false; g_stVer = ~0u; g_noticeInit = false;
		g_toastUntil = 0; g_okToast.clear(); g_afterPrepare = false; g_exitAfterSave = false; g_confirmExit.store(false);
		RebuildCategory();
		Request("refresh", nullptr);
	}

	// ---- estado real publicado pelo CCustomSkin (so copia quando mudou) ----
	{
		const uint32_t v = CCustomSkin::UiVersion();
		if (v != g_stVer)
		{
			CCustomSkin::UiState tmp;
			if (CCustomSkin::UiGetState(tmp))
			{
				g_st = std::move(tmp);
				g_haveSt = true;
				g_stVer = v;
				if (g_sel < 0 || StateOf(g_sel) == nullptr) SelectDefault();
				if (!g_noticeInit) { g_noticeInit = true; g_noticeSeq = g_st.noticeSeq; }   // avisos antigos (de antes de abrir) não aparecem
				else if (g_st.noticeSeq != g_noticeSeq)                                      // servidor confirmou/recusou, conexão caiu...
				{
					g_noticeSeq = g_st.noticeSeq;
					if (!g_st.notice.empty()) Toast(g_st.notice, g_st.notice.find("salva") == std::string::npos);
					if (g_exitAfterSave)                 // SALVAR E SAIR: so fecha se o servidor confirmou (ou nao havia o que enviar)
					{
						g_exitAfterSave = false;
						if (g_st.notice.rfind("Aparência salva", 0) == 0 || g_st.notice.rfind("Aplicado ", 0) == 0)
						{
							Provador::Close();
							return;
						}
					}
				}
				if (g_inflight && g_st.reqId == g_inflight)
				{
					g_inflight = 0;
					if (!g_st.reqOk) { g_exitAfterSave = false; Toast(g_st.msg.empty() ? "Não foi possível aplicar" : g_st.msg, true); }
					else if (!g_okToast.empty()) Toast(g_okToast, false);
					g_okToast.clear();
					if (g_afterPrepare)                  // o apply pode ter falhado: confere o estado real, nao o pedido
					{
						g_afterPrepare = false;
						if (g_st.skinReady) Toast("Skin preparada", false);
						else Toast(CCustomSkin::Info(), true);
					}
				}
			}
		}
		if (g_inflight && NowMs() - g_inflightAt > 2500)   // pedido perdido: nao deixa a tela travada
		{
			g_inflight = 0;
			Toast("Sem resposta do sistema de skin", true);
		}
	}

	const SlotState* selU = StateOf(g_sel);
	const bool ready    = g_haveSt && g_st.skinReady;
	const bool pending  = g_haveSt && g_st.pending;
	const bool awaiting = g_haveSt && g_st.awaiting;
	const bool online   = g_haveSt && g_st.online;
	const bool dffOk    = !g_haveSt || g_st.dffOk;
	const bool busy     = g_inflight != 0 || awaiting;      // esperando o servidor = nada novo pode ser pedido
	g_pendingMirror.store(pending);
	g_awaitMirror.store(awaiting);
	if (!pending && !awaiting && g_confirmExit.load() && !g_exitAfterSave) g_confirmExit.store(false);   // nada mais a salvar
	const bool confirmExit = g_confirmExit.load();

	// ---- layout (unidades de projeto: altura 540 -> k) ----
	const float m     = 8.0f * k;
	const float hdrY  = 4.0f * k, hdrH = 38.0f * k;
	const float top   = hdrY + hdrH + 6.0f * k;
	const float bot   = scr.y - m;
	const float catW  = std::clamp(scr.x * 0.14f, 105.0f * k, 150.0f * k);
	const float panW  = std::clamp(scr.x * 0.30f, 210.0f * k, 300.0f * k);
	const float catX  = m;
	const float panX  = scr.x - m - panW;
	const float cx0   = catX + catW + m, cx1 = panX - m;      // centro (ped do jogo aparece aqui)
	const float pad   = 8.0f * k;
	const float fs    = 12.0f * k, fsS = 10.5f * k;

	const float catH = 34.0f * k, catGap = 4.0f * k;

	const float titleH  = 20.0f * k;
	const float listTop = top + titleH;
	const float listBot = top + (bot - top) * 0.52f;
	const float varTitleY = listBot + 4.0f * k;
	const float varTop  = varTitleY + titleH;
	const float varBot  = bot;
	const float rowH    = 30.0f * k;

	const int   cols    = 5;
	const float cellGap = 4.0f * k;
	const float cellW   = (panW - 2.0f * pad - (cols - 1) * cellGap) / cols;
	const float cellH   = 30.0f * k;

	const float barH = 104.0f * k, barY = bot - barH;
	const float rowAY = barY, rowAH = 26.0f * k;
	const float rowBY = barY + 30.0f * k, rowBH = 34.0f * k;
	const float rowCY = barY + 68.0f * k, rowCH = 34.0f * k;
	const float bw = cx1 - cx0;
	const float gap = 6.0f * k;

	// lista de opcoes / grade de variantes
	const int   nRows = (int)g_catSlots.size();
	const float listContent = nRows * rowH;
	const float listView = listBot - listTop;
	const float listMax = std::max(0.0f, listContent - listView);
	g_scrollList = std::clamp(g_scrollList, 0.0f, listMax);

	const int   nVars = selU ? (int)selU->variants.size() : 0;
	const int   varRows = (nVars + cols - 1) / cols;
	const float varContent = varRows * (cellH + cellGap);
	const float varView = varBot - varTop;
	const float varMax = std::max(0.0f, varContent - varView);
	if (g_scrollToUsed)
	{   // volta ao item USADO: deixa a linha dele e a celula da variante em uso (id do <n>.png) dentro da vista
		g_scrollToUsed = false;
		for (int i = 0; i < nRows; ++i)
			if (g_catSlots[(size_t)i] == g_sel)
			{
				const float top0 = i * rowH, bot0 = top0 + rowH;
				if (top0 < g_scrollList) g_scrollList = top0;
				else if (bot0 > g_scrollList + listView) g_scrollList = bot0 - listView;
				break;
			}
		g_scrollList = std::clamp(g_scrollList, 0.0f, listMax);
		if (selU && selU->variant >= 0)
			for (int i = 0; i < nVars; ++i)
				if (selU->variants[(size_t)i] == selU->variant)
				{
					const float top0 = (i / cols) * (cellH + cellGap), bot0 = top0 + cellH;
					if (top0 < g_scrollVar) g_scrollVar = top0;
					else if (bot0 > g_scrollVar + varView) g_scrollVar = bot0 - varView;
					break;
				}
	}
	g_scrollVar = std::clamp(g_scrollVar, 0.0f, varMax);

	// botoes
	const float stepW = 30.0f * k, togW = 74.0f * k;
	const ImVec2 prevA(cx1 - stepW * 2 - togW - gap * 2, rowBY), prevB(prevA.x + stepW, rowBY + rowBH);
	const ImVec2 nextA(prevB.x + gap, rowBY),                    nextB(nextA.x + stepW, rowBY + rowBH);
	const ImVec2 togA (nextB.x + gap, rowBY),                    togB (cx1, rowBY + rowBH);
	const float  cbw = (bw - 2.0f * gap) / 3.0f;
	const ImVec2 apA(cx0, rowCY),                 apB(cx0 + cbw, rowCY + rowCH);
	const ImVec2 caA(apB.x + gap, rowCY),         caB(caA.x + cbw, rowCY + rowCH);
	const ImVec2 bkA(caB.x + gap, rowCY),         bkB(cx1, rowCY + rowCH);
	const ImVec2 prepA(cx0, rowBY),               prepB(cx1, rowBY + rowBH);

	// pergunta de saida (so desenhada/tocavel com previa nao salva)
	const float exW = std::min(scr.x * 0.7f, 460.0f * k), exH = 132.0f * k;
	const float exX0 = (scr.x - exW) * 0.5f, exY0 = (scr.y - exH) * 0.5f;
	const float exBw = (exW - 4.0f * gap) / 3.0f, exBy = exY0 + exH - 46.0f * k, exBh = 34.0f * k;
	const ImVec2 exSaveA(exX0 + gap, exBy),                       exSaveB(exSaveA.x + exBw, exBy + exBh);
	const ImVec2 exDiscA(exSaveB.x + gap, exBy),                  exDiscB(exDiscA.x + exBw, exBy + exBh);
	const ImVec2 exStayA(exDiscB.x + gap, exBy),                  exStayB(exStayA.x + exBw, exBy + exBh);

	auto inRect = [](float x, float y, float ax, float ay, float bx, float by) {
		return x >= ax && x <= bx && y >= ay && y <= by;
	};
	auto inBox = [&](float x, float y, const ImVec2& a, const ImVec2& b) { return inRect(x, y, a.x, a.y, b.x, b.y); };

	const bool canStep   = ready && selU && selU->hasMaterial && nVars > 0;
	const bool isToneRow = g_sel >= 0 && IsToneSlotName(SlotAt(g_sel).name);
	const bool canToggle = ready && selU && selU->hasMaterial && !isToneRow;   // o tom de pele nao se desliga (corpo invisivel)

	auto hitTest = [&](float x, float y, int& region) -> Hit {
		Hit h; region = 0;
		if (confirmExit)                                   // pergunta aberta: so os 3 botoes dela recebem toque
		{
			if (inBox(x, y, exSaveA, exSaveB)) h.kind = K_EXIT_SAVE;
			else if (inBox(x, y, exDiscA, exDiscB)) h.kind = K_EXIT_DISCARD;
			else if (inBox(x, y, exStayA, exStayB)) h.kind = K_EXIT_STAY;
			return h;
		}
		if (inBox(x, y, bkA, bkB)) { h.kind = K_BACK; return h; }
		if (!ready)
		{
			if (inBox(x, y, prepA, prepB)) { h.kind = K_PREPARE; return h; }
		}
		else
		{
			if (inBox(x, y, prevA, prevB)) { h.kind = K_PREV; return h; }
			if (inBox(x, y, nextA, nextB)) { h.kind = K_NEXT; return h; }
			if (inBox(x, y, togA, togB))   { h.kind = K_TOGGLE; return h; }
		}
		if (inBox(x, y, apA, apB)) { h.kind = K_APPLY; return h; }
		if (inBox(x, y, caA, caB)) { h.kind = K_CANCEL; return h; }
		if (inRect(x, y, catX, top, catX + catW, top + SkinSlots::kUiCategoryCount * (catH + catGap)))
		{
			const int i = (int)((y - top) / (catH + catGap));
			if (i >= 0 && i < SkinSlots::kUiCategoryCount) { h.kind = K_CAT; h.idx = i; return h; }
		}
		if (inRect(x, y, panX, listTop, panX + panW, listBot))
		{
			region = 1;
			const int i = (int)std::floor((y - listTop + g_scrollList) / rowH);
			if (i >= 0 && i < nRows) { h.kind = K_ROW; h.idx = i; }
			return h;
		}
		if (inRect(x, y, panX, varTop, panX + panW, varBot))
		{
			region = 2;
			const float lx = x - (panX + pad);
			const float ly = y - varTop + g_scrollVar;
			if (lx >= 0.0f)
			{
				const int c = (int)(lx / (cellW + cellGap));
				const int rr = (int)(ly / (cellH + cellGap));
				const int i = rr * cols + c;
				const bool inCellX = c < cols && (lx - c * (cellW + cellGap)) <= cellW;
				const bool inCellY = (ly - rr * (cellH + cellGap)) <= cellH;
				if (ly >= 0.0f && inCellX && inCellY && i >= 0 && i < nVars) { h.kind = K_VAR; h.idx = i; }
			}
			return h;
		}
		return h;
	};

	auto sameHit = [](const Hit& a, const Hit& b) { return a.kind == b.kind && a.idx == b.idx; };

	// ---- acoes ----
	auto doAction = [&](const Hit& h) -> bool {      // retorna true se a tela foi fechada
		switch (h.kind)
		{
			case K_BACK:
				if (awaiting) { Toast("Aguarde o servidor confirmar...", false); break; }
				if (pending) { g_confirmExit.store(true); break; }   // previa nao salva: pergunta antes de sair
				Provador::Close();
				return true;
			case K_EXIT_SAVE:
				g_confirmExit.store(false);
				if (!pending || busy) break;
				if (!online) { Toast("Sem conexão com o servidor: não é possível salvar", true); break; }
				g_exitAfterSave = true;
				Request("commit", "Enviado ao servidor...");
				break;
			case K_EXIT_DISCARD:
				g_confirmExit.store(false);
				Provador::Close();                   // restaura exatamente o estado anterior (cancel)
				return true;
			case K_EXIT_STAY:
				g_confirmExit.store(false);
				break;
			case K_PREPARE:
				if (busy || !dffOk) break;
				g_afterPrepare = true;
				Request("prepare", nullptr);         // instala o molde (= /skin7 apply) e leva o estado do servidor para ele
				break;
			case K_CAT:
				if (h.idx != g_cat) { g_cat = h.idx; RebuildCategory(); SelectDefault(); }
				break;
			case K_ROW:
			{
				if (h.idx < 0 || h.idx >= nRows) break;
				const int k2 = g_catSlots[(size_t)h.idx];
				const SlotState* u = StateOf(k2);
				if (!u || !u->hasMaterial) { Toast("Este item não existe no modelo carregado", true); break; }
				g_sel = k2; g_scrollVar = 0.0f; g_scrollToUsed = true;
				if (!ready) { Toast("Skin não instalada: toque em PREPARAR", true); break; }
				if (busy || u->state == kEquipped) break;   // ja equipado: so seleciona (visivel != equipado)
				if (!u->variants.empty()) PreviewSlot(k2, std::to_string(u->variant >= 0 ? u->variant : u->variants.front()));
				else PreviewSlot(k2, "on");
				break;
			}
			case K_VAR:
			{
				if (!selU || h.idx < 0 || h.idx >= nVars || busy) break;
				if (!ready) { Toast("Skin não instalada: toque em PREPARAR", true); break; }
				const int v = selU->variants[(size_t)h.idx];
				if (selU->state == kEquipped && selU->variant == v) break;
				PreviewSlot(g_sel, std::to_string(v));
				break;
			}
			case K_PREV:
			case K_NEXT:
			{
				if (!canStep || busy) break;
				const int n = nVars, pos = VariantPos(*selU);
				const int dir = (h.kind == K_NEXT) ? 1 : -1;
				const int to = (pos < 0) ? (dir > 0 ? 0 : n - 1) : (pos + dir + n) % n;
				PreviewSlot(g_sel, std::to_string(selU->variants[(size_t)to]));
				break;
			}
			case K_TOGGLE:
				if (!canToggle || busy) break;
				PreviewSlot(g_sel, (selU->state == kDisabled || selU->state == kCovered) ? "on" : "off");
				break;
			case K_APPLY:
				if (!pending || busy) break;
				if (!online) { Toast("Sem conexão com o servidor: não é possível aplicar", true); break; }
				Request("commit", "Enviado ao servidor...");
				break;
			case K_CANCEL:
				if (!pending || busy) break;
				Request("cancel", "Prévia cancelada");
				break;
		}
		return false;
	};

	// ---- toques acumulados desde o ultimo frame ----
	std::vector<TouchEv> evs;
	{
		std::lock_guard<std::mutex> l(g_evMutex);
		evs.swap(g_events);
	}

	for (const TouchEv& e : evs)
	{
		if (e.type == 2 && g_drag.finger < 0)                    // push (primeiro dedo livre)
		{
			g_drag = Drag();
			g_drag.finger = e.num;
			int region = 0;
			g_drag.hit = hitTest(e.x, e.y, region);
			g_drag.region = region;
			g_drag.sy = e.y;
			g_drag.scroll0 = (region == 1) ? g_scrollList : g_scrollVar;
		}
		else if (e.num == g_drag.finger && e.type == 3)          // move
		{
			if (g_drag.region != 0)
			{
				if (std::fabs(e.y - g_drag.sy) > 10.0f * k) g_drag.scrolling = true;
				if (g_drag.scrolling)
				{
					if (g_drag.region == 1) g_scrollList = std::clamp(g_drag.scroll0 - (e.y - g_drag.sy), 0.0f, listMax);
					else                    g_scrollVar  = std::clamp(g_drag.scroll0 - (e.y - g_drag.sy), 0.0f, varMax);
				}
			}
		}
		else if (e.num == g_drag.finger && e.type == 1)          // pop / cancel
		{
			const bool ok = !e.cancel && !g_drag.scrolling && g_drag.hit.kind != K_NONE;
			Hit up; int reg2 = 0;
			if (ok) up = hitTest(e.x, e.y, reg2);
			const bool same = ok && sameHit(up, g_drag.hit);     // soltou no mesmo botao em que tocou
			const Hit act = g_drag.hit;
			g_drag = Drag();
			if (same && doAction(act)) return;                   // fechou: nao desenha mais neste frame
		}
		// toques de outros dedos: consumidos e ignorados
	}

	// ---- pintura ----
	// cabecalho
	r->drawRect(ImVec2(m, hdrY), ImVec2(scr.x - m, hdrY + hdrH), cPanel, true);
	r->drawRect(ImVec2(m, hdrY + hdrH - 2.0f * k), ImVec2(scr.x - m, hdrY + hdrH), cBlue, true);
	r->drawText(ImVec2(m + 10.0f * k, hdrY + 4.0f * k), cBlue, "/SKIN7", false, fsS);
	r->drawText(ImVec2(m + 10.0f * k, hdrY + 16.0f * k), cText, "PERSONALIZAÇÃO", false, 17.0f * k);
	{
		std::string who = "Jogador";
		if (pNetGame && pNetGame->GetPlayerPool())
		{
			const char* nm = pNetGame->GetPlayerPool()->GetLocalPlayerName();
			who = std::string(nm ? nm : "Jogador") + "  [" + std::to_string((int)pNetGame->GetPlayerPool()->GetLocalPlayerID()) + "]";
		}
		const ImVec2 ts = r->calculateTextSize(who, fs);
		r->drawText(ImVec2(scr.x - m - 10.0f * k - ts.x, hdrY + (hdrH - ts.y) * 0.5f), cText, who, false, fs);
	}

	auto button = [&](const ImVec2& a, const ImVec2& b, const char* text, bool enabled, bool accent, float size) {
		r->drawRect(a, b, enabled ? (accent ? cBlue : cPanel2) : cOff, true);
		if (enabled && !accent) r->drawRect(a, b, cLine, false);
		const ImVec2 ts = r->calculateTextSize(text, size);
		r->drawText(ImVec2((a.x + b.x - ts.x) * 0.5f, (a.y + b.y - ts.y) * 0.5f),
					enabled ? cText : cDim, text, false, size);
	};

	// categorias
	for (int i = 0; i < SkinSlots::kUiCategoryCount; ++i)
	{
		const float ay = top + i * (catH + catGap);
		const bool act = (i == g_cat);
		r->drawRect(ImVec2(catX, ay), ImVec2(catX + catW, ay + catH), act ? cPanel2 : cPanel, true);
		if (act)
		{
			r->drawRect(ImVec2(catX, ay), ImVec2(catX + 3.0f * k, ay + catH), cBlue, true);
			r->drawRect(ImVec2(catX, ay), ImVec2(catX + catW, ay + catH), cBlueBg, true);
		}
		const char* t = SkinSlots::kUiCategories[i].title;
		const ImVec2 ts = r->calculateTextSize(t, fs);
		r->drawText(ImVec2(catX + 12.0f * k, ay + (catH - ts.y) * 0.5f), act ? cText : cDim, t, false, fs);
	}

	// painel de opcoes (lista rolavel)
	r->drawRect(ImVec2(panX, top), ImVec2(panX + panW, bot), cPanel, true);
	r->drawText(ImVec2(panX + pad, top + 4.0f * k), cBlue, SkinSlots::kUiCategories[g_cat].title, false, fsS);
	r->drawRect(ImVec2(panX, listTop - 2.0f * k), ImVec2(panX + panW, listTop - 1.0f * k), cLine, true);

	r->pushClipRect(ImVec2(panX, listTop), ImVec2(panX + panW, listBot), true);
	for (int i = 0; i < nRows; ++i)
	{
		const float ry = listTop + i * rowH - g_scrollList;
		if (ry + rowH < listTop || ry > listBot) continue;
		const int k2 = g_catSlots[(size_t)i];
		const SlotState* u = StateOf(k2);
		const bool avail = u && u->hasMaterial;
		const bool sel = (k2 == g_sel);

		if (sel)
		{
			r->drawRect(ImVec2(panX, ry), ImVec2(panX + panW, ry + rowH), cBlueBg, true);
			r->drawRect(ImVec2(panX, ry), ImVec2(panX + 3.0f * k, ry + rowH), cBlue, true);
		}
		else if (i & 1) r->drawRect(ImVec2(panX, ry), ImVec2(panX + panW, ry + rowH), ImColor(255, 255, 255, 8), true);

		const float ty = ry + (rowH - fs) * 0.5f;
		r->drawText(ImVec2(panX + pad + 4.0f * k, ty), avail ? cText : cDim, SlotLabel(SlotAt(k2).name), false, fs);

		std::string st;
		ImColor stc = cDim;
		if (!u)           st = "...";
		else if (!avail)  st = "indisponível";
		else if (IsToneSlotName(SlotAt(k2).name) && u->variant >= 1) { st = "TOM " + std::to_string(u->variant); stc = cBlue; }   // mesmo coberto por roupa
		else if (u->state == kEquipped) { st = "USADO ATUALMENTE " + std::to_string(VarLabel(u, u->variant)); stc = cBlue; }
		else if (u->state == kCovered)  st = "coberto";
		else if (u->state == kDisabled && SkinSlots::GroupOf(SlotAt(k2).name) >= 0) st = "";   // grupo exclusivo: so o USADO e marcado; os demais ficam sem rotulo
		else if (u->state == kDisabled) st = "desligado";
		else if (u->partial)            st = "parcial";      // ex.: pele das pernas parcialmente coberta por uma bermuda
		else                            st = "padrão";       // visível com a textura original = nada equipado
		if (u && avail && u->previewed && !st.empty()) { st += " *"; stc = cAmber; }
		const ImVec2 ts = r->calculateTextSize(st, fsS);
		r->drawText(ImVec2(panX + panW - pad - ts.x, ry + (rowH - ts.y) * 0.5f), stc, st, false, fsS);
	}
	r->popClipRect();

	// grade de variantes (rolavel)
	r->drawRect(ImVec2(panX, varTitleY - 2.0f * k), ImVec2(panX + panW, varTitleY - 1.0f * k), cLine, true);
	{
		std::string vt = "VARIANTES";
		if (selU && selU->varMod > 0 && !selU->variants.empty()) vt = "ROSTOS DO TOM " + std::to_string(selU->variants.front() / selU->varMod) + " (cor do corpo)";
		else if (isToneRow) vt = "TOM DE PELE (corpo e rosto)";
		r->drawText(ImVec2(panX + pad, varTitleY + 3.0f * k), cBlue, vt, false, fsS);
	}
	r->pushClipRect(ImVec2(panX, varTop), ImVec2(panX + panW, varBot), true);
	if (!selU || !selU->hasMaterial)
		r->drawText(ImVec2(panX + pad, varTop + 6.0f * k), cDim, selU ? "Item indisponível no modelo" : "Escolha um item acima", false, fsS);
	else if (nVars == 0)
		r->drawText(ImVec2(panX + pad, varTop + 6.0f * k), cDim, "Sem variantes no pack (use LIGAR/DESLIGAR)", false, fsS);
	else
	{
		for (int i = 0; i < nVars; ++i)
		{
			const float cxp = panX + pad + (i % cols) * (cellW + cellGap);
			const float cyp = varTop + (i / cols) * (cellH + cellGap) - g_scrollVar;
			if (cyp + cellH < varTop || cyp > varBot) continue;
			const int v = selU->variants[(size_t)i];
			const bool cur = (selU->state == kEquipped || (isToneRow && selU->variant >= 1)) && selU->variant == v;
			const bool pv  = cur && selU->previewed;
			r->drawRect(ImVec2(cxp, cyp), ImVec2(cxp + cellW, cyp + cellH), pv ? cAmberBg : (cur ? cBlueBg : cPanel2), true);
			r->drawRect(ImVec2(cxp, cyp), ImVec2(cxp + cellW, cyp + cellH), pv ? cAmber : (cur ? cBlue : cLine), false);
			const std::string t = std::to_string(VarLabel(selU, v));
			const ImVec2 ts = r->calculateTextSize(t, fs);
			r->drawText(ImVec2(cxp + (cellW - ts.x) * 0.5f, cyp + (cellH - ts.y) * 0.5f), cur ? cText : cDim, t, false, fs);
		}
	}
	r->popClipRect();

	// barra inferior (centro): estado, variante atual, navegacao e botoes
	r->drawRect(ImVec2(cx0, barY - 2.0f * k), ImVec2(cx1, bot), cPanel, true);

	{   // linha A: aviso (toast / previa pendente / skin nao instalada)
		std::string msg; ImColor mc = cDim;
		if (NowMs() < g_toastUntil) { msg = g_toast; mc = g_toastErr ? cRed : cBlue; }
		else if (!g_haveSt) msg = "Carregando...";
		else if (!dffOk) { msg = "skin.dff não encontrado em SAMP/skins"; mc = cRed; }
		else if (!ready) { msg = g_st.prepMsg.empty() ? "Preparando a skin..." : g_st.prepMsg; mc = g_st.prepMsg.empty() ? cDim : cRed; }
		else if (awaiting) { msg = "Salvando no servidor..."; mc = cAmber; }
		else if (!online) { msg = "Sem conexão com o servidor: só visualização"; mc = cRed; }
		else if (pending) { msg = "PRÉVIA (não salva): toque APLICAR para confirmar"; mc = cAmber; }
		r->pushClipRect(ImVec2(cx0, rowAY), ImVec2(cx1, rowAY + rowAH), true);
		r->drawText(ImVec2(cx0 + pad, rowAY + (rowAH - fsS) * 0.5f), mc, msg, false, fsS);
		r->popClipRect();
	}

	if (!ready)
	{
		button(prepA, prepB, busy ? "PREPARANDO..." : (dffOk ? "PREPARAR (tentar de novo)" : "skin.dff ausente"), !busy && dffOk, true, fs);
	}
	else
	{
		r->pushClipRect(ImVec2(cx0, rowBY), ImVec2(prevA.x - gap, rowBY + rowBH), true);
		r->drawText(ImVec2(cx0 + pad, rowBY + (rowBH - fs) * 0.5f), cText, InfoText(selU, g_sel), false, fs);
		r->popClipRect();
		button(prevA, prevB, "<", canStep && !busy, false, 16.0f * k);
		button(nextA, nextB, ">", canStep && !busy, false, 16.0f * k);
		button(togA, togB, (selU && (selU->state == kDisabled || selU->state == kCovered)) ? "LIGAR" : "DESLIGAR", canToggle && !busy, false, fsS);
	}

	button(apA, apB, awaiting ? "SALVANDO..." : "APLICAR", pending && online && !busy, true, fs);
	button(caA, caB, "CANCELAR", pending && !busy, false, fs);
	button(bkA, bkB, "VOLTAR", true, false, fs);

	if (confirmExit)
	{
		r->drawRect(ImVec2(0.0f, 0.0f), scr, ImColor(0, 0, 0, 170), true);
		r->drawRect(ImVec2(exX0, exY0), ImVec2(exX0 + exW, exY0 + exH), cPanel, true);
		r->drawRect(ImVec2(exX0, exY0), ImVec2(exX0 + exW, exY0 + exH), cAmber, false);
		const char* t1 = "Sair sem salvar?";
		const char* t2 = "Você tem uma prévia que ainda não foi salva.";
		const ImVec2 s1 = r->calculateTextSize(t1, 16.0f * k), s2 = r->calculateTextSize(t2, fsS);
		r->drawText(ImVec2(exX0 + (exW - s1.x) * 0.5f, exY0 + 12.0f * k), cText, t1, false, 16.0f * k);
		r->drawText(ImVec2(exX0 + (exW - s2.x) * 0.5f, exY0 + 40.0f * k), cDim, t2, false, fsS);
		button(exSaveA, exSaveB, "SALVAR E SAIR", online, true, fsS);
		button(exDiscA, exDiscB, "SAIR SEM SALVAR", true, false, fsS);
		button(exStayA, exStayB, "CONTINUAR", true, false, fsS);
	}
}
