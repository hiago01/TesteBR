#include "../../main.h"
#include "../gui.h"
#include "../sffonts.h"
#include "../../game/camstick.h"
#include "../../game/game.h"
#include "../../vendor/SimpleIni/SimpleIni.h"
#include "../phone/PhoneApps.h"
#include "../phone/PhoneNet.h"
#include "../phone/PhoneGram.h"
#include "../phone/PhoneStore.h"
#include "../phone/PhoneCamMath.h"
#include "../../voice_new/Plugin.h"
#include "../../net/netgame.h"
#include "../../net/playerpool.h"
#include "../../vendor/raknet/SAMP/SAMPRPC.h"
#include "../../vendor/raknet/BitStream.h"
#include "phone.h"
#include "phonephotos.h"
#include "maximomenu.h"
#include "maximoos.h"
#include "inventory.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <ctime>
#include <map>
#include <mutex>
#include <vector>

extern UI* pUI;
extern CNetGame* pNetGame;
// definido em game/multitouch.cpp: true enquanto o evento em despacho veio de ACTION_CANCEL
extern volatile bool g_bTouchCancel;

using namespace Phone;

namespace
{
	std::atomic<bool> g_wantOpen{ false };
	std::atomic<int>  g_backReq{ 0 };

	PhoneNet g_net;
	int g_poseSent = 0;                  // ultima pose pedida ao servidor (0 = nenhuma)
	int64_t g_poseAt = 0;
	bool g_selfieCam = false;            // a camera do jogo esta sob controle da foto (traseira ou selfie)
	Phone::Store::Service* g_storeSvc = nullptr;         // Loja / IMEI / papel de parede (usa o transporte e o token do Gram)
	Gram::Service* g_gram = nullptr;              // rede social (criado junto com o celular; fala direto com o servico de fotos do servidor)
	std::atomic<bool> g_gramReset{ false };
	bool g_callPending = false;                  // so a thread de desenho toca nisto
	std::atomic<bool> g_openToggle{ false };     // item celular usado no inventario / slot rapido
	std::atomic<bool> g_openCall{ false };       // ligacao recebida: abrir o celular na tela de ligacao

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
		s.handsFree = ini.GetLongValue("celular", "vivavoz", 1) != 0;
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
		ini.SetLongValue("celular", "vivavoz", s.handsFree ? 1 : 0);
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
			g_net.attach(&p->context());
			// RPC 240 (cliente -> servidor). Mesmo caminho de SendChatCommand (thread de desenho). O numero do remetente NAO vai: o servidor usa o da conta.
			g_net.send = [](const Net::Request& r) -> bool
			{
				if (!pNetGame || pNetGame->GetGameState() != GAMESTATE_CONNECTED || !pNetGame->GetRakClient()) return false;
				RakNet::BitStream bs;
				bs.Write((uint8_t)r.op);
				bs.Write((uint16_t)r.req);
				if (r.op == Net::OpSend || r.op == Net::OpDial)
				{
					bs.Write((uint8_t)7);
					bs.Write(r.number.data(), 7);
				}
				if (r.op == Net::OpPose) bs.Write((uint8_t)r.arg);
				if (r.op == Net::OpSend)
				{
					bs.Write((uint16_t)r.text.size());
					bs.Write(r.text.data(), (unsigned)r.text.size());
				}
				pNetGame->GetRakClient()->RPC(&RPC_PhoneRequest, &bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, false, UNASSIGNED_NETWORK_ID, nullptr);
				return true;
			};
			p->context().talk = [](bool on) { Plugin::SetExternalTalk(on); };      // "Segure para falar" da ligacao -> mesmo caminho do botao de microfone do SampVoice
			g_net.notify = [](const std::string& t, const std::string& x, const std::string& app, int level) { PhoneWidget::PostNotification(t, x, app, level); };
			g_net.incomingCall = [](const std::string&) { g_openCall.store(true); };
			g_net.openRequest = []() { g_openToggle.store(true); };
			// Gram (rede social) + fotos: o gamemode so entrega o token (RPC op 6/30); fotos/feed vao direto por HTTP para o servico do servidor
			g_gram = new Gram::Service(new HttpClient(2));
			g_gram->requestToken = [](int64_t now) { return g_net.requestGramToken(now); };
			g_gram->host = []() { return pNetGame ? std::string(pNetGame->m_szHostOrIp) : std::string(); };
			g_net.gramToken = [](int status, const std::string& token, int port) { if (g_gram) g_gram->onToken(status, token, port, NowMs()); };
			PhonePhotos::SetFetcher([](const std::string& media, std::function<void(bool, const std::string&)> cb) { return g_gram && g_gram->fetchImage(media, std::move(cb)); });
			p->context().gram = g_gram;
			g_storeSvc = new Phone::Store::Service(g_gram);
			p->context().store = g_storeSvc;
			p->context().removeChip = []() { return g_net.removeChip(); };
			// o servidor do jogo avisa qual celular (item do inventario) esta em uso e qual chip tem: sessao do Gram / Loja valem POR APARELHO
			g_net.deviceChanged = [](bool hasPhone)
			{
				if (g_gram) g_gram->reset();
				if (g_storeSvc) { g_storeSvc->reset(); if (!hasPhone) g_storeSvc->markNoPhone(); else g_storeSvc->refresh(NowMs(), true); }
			};
			p->context().photos = PhonePhotos::Api();
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
		void setFont(int role) override { m_role = role; }
		void text(Vec2 p, Color c, const std::string& s, float size) override
		{
			if (!c.a || s.empty()) return;
			ImFont* sf = SfFonts::get(m_role); ImFont* old = sf ? m_r->swapFont(sf) : nullptr;
			m_r->drawText(V(p.x, p.y), C(c), s, false, size);
			if (old) m_r->swapFont(old);
		}
		Vec2 measure(const std::string& s, float size) override
		{
			ImFont* sf = SfFonts::get(m_role); ImFont* old = sf ? m_r->swapFont(sf) : nullptr;
			const ImVec2 v = m_r->calculateTextSize(s, size);
			if (old) m_r->swapFont(old);
			return { v.x, v.y };
		}
		void pushClip(const Rect& r) override { m_r->pushClipRect(V(r.x, r.y), V(r.right(), r.bottom()), true); ++m_clips; }
		void popClip() override { if (m_clips > 0) { m_r->popClipRect(); --m_clips; } }
		void setAlpha(float a) override { m_alpha = std::clamp(a, 0.0f, 1.0f); }
		void image(const Rect& r, uint64_t tex, Vec2 uv0, Vec2 uv1) override
		{
			if (!tex || r.empty()) return;
			m_r->drawImage(V(r.x, r.y), V(r.right(), r.bottom()), (ImTextureID)(uintptr_t)tex, ImColor(255, 255, 255, (int)std::lround(255.0f * m_alpha)), ImVec2(uv0.x, uv0.y), ImVec2(uv1.x, uv1.y));
		}
	private:
		int m_role = 0;
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

void PhoneWidget::OnServerPacket(const unsigned char* data, int bytes)
{
	if (!data || bytes <= 0 || bytes > 2048) return;
	RakNet::BitStream bs(const_cast<unsigned char*>(data), bytes, false);
	PM();                                                  // garante que g_net esta ligado
	g_net.onServerPacketBS(bs);
}
void PhoneWidget::Reset() { g_net.connectionReset(); g_gramReset.store(true); g_poseSent = 0; CamStick::SetMode(0); if (g_selfieCam) { g_selfieCam = false; CCamera::SetBehindPlayer(); } }

void PhoneWidget::PostNotification(const std::string& title, const std::string& text, const std::string& appId, int level)
{
	std::lock_guard<std::mutex> l(g_nmx);
	if (g_pending.size() < 32) g_pending.push_back({ title, text, appId, level });
}

namespace
{

	// Camera da FOTO (traseira = modo 2, selfie = modo 3): recria a camera do jogo para que o enquadramento caia no VISOR do celular,
	// sem mover o aparelho (projecao fora de eixo, ver PhoneCamMath.h). Reaplicada a cada quadro; fora da foto devolve a camera normal.
	void PhotoCamera(int mode, PhoneManager& pm)
	{
		if (mode == 2 || mode == 3)
		{
			if (!pNetGame || !pNetGame->GetPlayerPool()) { CamStick::SetMode(0); return; }
			CLocalPlayer* lp = pNetGame->GetPlayerPool()->GetLocalPlayer();
			CPlayerPed* pp = lp ? lp->GetPlayerPed() : nullptr;
			if (!pp || !pp->m_pPed || pp->IsInVehicle()) { CamStick::SetMode(0); return; }
			// joystick do personagem com a camera (game/camstick.h): o motor anda so ao longo do heading; o giro e aplicado aqui, uma vez por quadro
			CamStick::SetMode(mode);
			{
				static int64_t s_prevT = 0; const int64_t t = NowMs();
				const float dtS = (s_prevT && t > s_prevT) ? (float)(t - s_prevT) / 1000.0f : 0.016f; s_prevT = t;
				const float turn = CamStick::TakeTurn(dtS);
				if (turn != 0.0f) pp->SetRotation((pp->m_pPed->m_fCurrentRotation + turn) * 57.29578f);
			}
			const PhoneContext& c = pm.context();
			Phone::CamIn in;
			in.mode = mode;
			const CVector p = pp->m_pPed->GetPosition();
			in.px = p.x; in.py = p.y; in.pz = p.z; in.heading = pp->m_pPed->m_fCurrentRotation;
			in.screenW = pm.metrics().screenW; in.screenH = pm.metrics().screenH;
			in.vfX = c.camVfX; in.vfY = c.camVfY; in.vfW = c.camVfW; in.vfH = c.camVfH;
			in.fovDeg = *reinterpret_cast<const float*>(g_libGTASA + 0x88E6BC);      // FOV horizontal atual (CDraw::SetFOV hook)
			const Phone::CamOut o = Phone::ComputePhotoCamera(in);
			if (!o.ok) { CamStick::SetMode(0); return; }
			CCamera::SetPosition(o.cx, o.cy, o.cz, 0.0f, 0.0f, 0.0f);
			CCamera::LookAtPoint(o.lx, o.ly, o.lz, 2);
			g_selfieCam = true;
		}
		else if (g_selfieCam)
		{
			CamStick::SetMode(0);
			g_selfieCam = false;
			CCamera::SetBehindPlayer();                                             // devolve a camera normal
		}
	}

	// Pose do corpo conforme o celular: aberto = na mao; Camera = foto (traseira) ou selfie. O servidor sincroniza (RPC 230).
	void PosePump(PhoneManager& pm, int64_t now)
	{
		int want = 0;
		if (pm.isOpen()) want = (pm.foregroundId() == "camera") ? (pm.context().cameraSelfie ? 3 : 2) : 1;
		PhotoCamera(want, pm);
		if (want != g_poseSent && now - g_poseAt > 350 && g_net.sendPose(want)) { g_poseSent = want; g_poseAt = now; }
	}
}

void PhoneWidget::draw(ImGuiRenderer* r)
{
	PhoneManager& pm = PM();
	static int64_t s_prev = 0;
	const int64_t now = NowMs();
	const float dt = s_prev ? (float)std::min<int64_t>(now - s_prev, 100) : 16.0f;
	s_prev = now;

	if (!g_store.loaded)
	{
		LoadPrefs(pm.context());
		if (g_store.loaded) Data::FailStalePending(pm.context());       // "enviando" de uma sessao anterior nao pode ser confirmado
	}
	if (g_store.dirty && now - g_store.lastSave > 3000) SavePrefs(pm.context());      // no maximo 1 gravacao a cada 3 s

	// rede: roda mesmo com o celular fechado (mensagens recebidas precisam ser guardadas e avisadas)
	pm.context().nowMs = now;
	if (g_gramReset.exchange(false) && g_gram) { g_gram->reset(); if (g_storeSvc) g_storeSvc->reset(); }
	if (g_store.loaded) { g_net.pump(now); if (g_gram) g_gram->pump(now); }
	if (g_openToggle.exchange(false))
	{
		if (g_wantOpen.load()) PhoneWidget::Close();
		else { if (InventoryUI::IsOpen()) InventoryUI::Close(false); PhoneWidget::Open(); }
	}
	if (g_openCall.exchange(false)) { PhoneWidget::Open(); if (g_wantOpen.load()) g_callPending = true; }

	if (g_wantOpen.load() && !pm.isOpen()) { const ImVec2 sc = pUI ? pUI->displaySize() : ImVec2(1920, 1080); pm.setScreenSize(sc.x, sc.y); pm.open(); g_net.requestNumber(now); if (g_storeSvc) g_storeSvc->refresh(now); }
	if (g_callPending && pm.isOpen()) { g_callPending = false; if (pm.context().call.state == 2) pm.openApp("ligacao"); }
	if (!g_wantOpen.load() && pm.isOpen()) pm.close();
	PosePump(pm, now);
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
