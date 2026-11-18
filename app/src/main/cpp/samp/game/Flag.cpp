#include "Flag.h"

#include "FlagMesh.h"
#include "FlagModel.h"
#include "FlagPhysics.h"

#include "../main.h"
#include "../vendor/armhook/patch.h"
#include "game.h"
#include "RW/RenderWare.h"
#include "../net/netgame.h"
#include "../net/localplayer.h"
#include "../util/CUtil.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

extern CGame*    pGame;
extern CNetGame* pNetGame;
extern uintptr_t g_libGTASA;

// =====================================================================================
//  Bandeira 3D com fisica de tecido - cola com o jogo (ver Flag.h / BANDEIRA_MAXIMO.md)
//  Toda a matematica esta em FlagPhysics/FlagModel/FlagMesh (modulos puros, testados no PC).
//  Aqui ficam: carga do DFF e das texturas, ciclo de vida das bandeiras, LOD, desenho Im3D, comando, logs.
// =====================================================================================

using namespace MxFlag;

static_assert(sizeof(MxFlag::DrawVertex) == sizeof(RxObjSpace3DVertex), "layout Im3D");
static_assert(offsetof(MxFlag::DrawVertex, nx) == offsetof(RxObjSpace3DVertex, objNormal), "layout Im3D");
static_assert(offsetof(MxFlag::DrawVertex, color) == offsetof(RxObjSpace3DVertex, color), "layout Im3D");
static_assert(offsetof(MxFlag::DrawVertex, u) == offsetof(RxObjSpace3DVertex, u), "layout Im3D");

namespace
{
	using namespace MxFlag;
	using Clock = std::chrono::steady_clock;

	constexpr int   kMaxFlags    = 24;
	constexpr float kUpdateDist  = 120.0f;   // alem disso nao simula
	constexpr float kRenderDist  = 150.0f;   // alem disso nao desenha
	constexpr float kFarDist     = 35.0f;    // alem disso: LOD barato (menos iteracoes, sem flexao)
	constexpr float kDeg2Rad     = 3.14159265358979f / 180.0f;
	constexpr int   kLocalIdMax  = 999;      // ids 1..999 = locais (arquivo/comando); 1000+ = servidor (convencao)

	const char* const kModelFile = "SAMP/models/flag_maximo.dff";
	const char* const kFlagsFile = "SAMP/flags.txt";
	const char* const kBank      = "mapasmaximo";
	const char* const kTexFront  = "flag_maximo";
	const char* const kTexBack   = "flag_maximo_verso";

	struct FlagObj
	{
		int      id = 0;
		bool     fromServer = false;
		bool     enabled = true;
		uint8_t  interior = 0;
		Vec3     pos{};
		float    yawDeg = 0, windDirDeg = 0, windInt = 1.0f;
		CFlagPhysics phys;
		CFlagMesh    mesh;
		float    dist = 0;
		bool     active = false;        // simulando neste quadro
		float    idle = 0;              // segundos parada (fora de alcance/desativada)
		uint32_t seenResets = 0;
		std::vector<Vec3> sim, nrm;     // buffers de trabalho
	};

	struct FlagMgr
	{
		std::mutex mtx;
		std::vector<std::unique_ptr<FlagObj>> flags;

		// modelo
		int           modelState = 0;           // 0 = nao tentou, 1 = ok, -1 = falhou
		FlagModelData model;
		// texturas
		RwTexture*    texFront = nullptr;
		RwTexture*    texBack  = nullptr;
		int           texTries = 0;
		Clock::time_point nextTexTry{};
		// arquivo
		bool          fileLoaded = false;
		// tempo / estatisticas
		Clock::time_point last{};
		bool          haveLast = false;
		Clock::time_point lastLog{};
		double        accMs = 0; uint64_t accFrames = 0, accSteps = 0;
		int           lastActive = 0, lastDrawn = 0;
		float         brightness = 1.0f;
		bool          paused = false;           // /bandeira parar
	};
	FlagMgr g_fm;

	std::string FlPath(const char* rel) { return g_pszStorage ? std::string(g_pszStorage) + rel : std::string(); }

	bool FlReadAll(const std::string& path, std::vector<uint8_t>& out)
	{
		FILE* f = std::fopen(path.c_str(), "rb");
		if (!f) return false;
		std::fseek(f, 0, SEEK_END); const long n = std::ftell(f); std::fseek(f, 0, SEEK_SET);
		if (n <= 0 || n > 8 * 1024 * 1024) { std::fclose(f); return false; }
		out.resize((size_t)n);
		const bool ok = std::fread(out.data(), 1, (size_t)n, f) == (size_t)n;
		std::fclose(f);
		return ok;
	}

	// ---------------------------------------------------------------------------------
	bool EnsureModel()   // g_fm.mtx travado
	{
		if (g_fm.modelState != 0) return g_fm.modelState == 1;
		g_fm.modelState = -1;
		const std::string p = FlPath(kModelFile);
		std::vector<uint8_t> buf;
		if (p.empty() || !FlReadAll(p, buf)) { FLog("[FLAG_PHYSICS] modelo nao encontrado: %s (bandeiras desativadas)", kModelFile); return false; }
		std::string err;
		if (!LoadFlagModel(buf.data(), buf.size(), g_fm.model, err)) { FLog("[FLAG_PHYSICS] DFF invalido (%s): %s", kModelFile, err.c_str()); return false; }
		g_fm.modelState = 1;
		FLog("[FLAG_PHYSICS] modelo carregado: %s | mastro %d v | base %d v | tecido %d v (%d particulas de simulacao, grade %dx%d) | %zu tris de tecido",
			 kModelFile, g_fm.model.parts[0].vertexCount(), g_fm.model.parts[1].vertexCount(), g_fm.model.parts[2].vertexCount(),
			 g_fm.model.cols * g_fm.model.rows, g_fm.model.cols, g_fm.model.rows, g_fm.model.parts[2].tris.size());
		return true;
	}

	bool EnsureTextures()   // g_fm.mtx travado; chamado do Render (thread do jogo: mexe em banco de texturas do RW)
	{
		if (g_fm.texFront && g_fm.texBack) return true;
		if (g_fm.texTries >= 5) return false;
		const auto now = Clock::now();
		if (now < g_fm.nextTexTry) return false;
		g_fm.nextTexTry = now + std::chrono::seconds(3);
		g_fm.texTries++;
		if (!g_fm.texFront) g_fm.texFront = CUtil::LoadTextureFromDB(kBank, kTexFront);
		if (!g_fm.texBack)  g_fm.texBack  = CUtil::LoadTextureFromDB(kBank, kTexBack);
		if (g_fm.texFront && g_fm.texBack)
		{
			FLog("[FLAG_PHYSICS] texturas carregadas do banco %s: %s e %s", kBank, kTexFront, kTexBack);
			return true;
		}
		FLog("[FLAG_PHYSICS] texturas ausentes no banco %s (frente=%s verso=%s) tentativa %d/5", kBank,
			 g_fm.texFront ? "ok" : "FALTA", g_fm.texBack ? "ok" : "FALTA", g_fm.texTries);
		return false;
	}

	// ---------------------------------------------------------------------------------
	FlagObj* FlFind(int id) { for (auto& f : g_fm.flags) if (f->id == id) return f.get(); return nullptr; }

	int FreeLocalId()
	{
		for (int i = 1; i <= kLocalIdMax; i++) if (!FlFind(i)) return i;
		return -1;
	}

	void ApplyLod(FlagObj& f)
	{
		const bool far = f.dist > kFarDist;
		f.phys.params.iterations = far ? 4 : 8;
		f.phys.params.bending    = !far;
	}

	// cria (ou substitui) uma bandeira. id < 0 = escolhe um id local livre. Devolve o id ou -1.
	int CreateLocked(int id, bool fromServer, const Vec3& pos, float yawDeg, float windDirDeg, float windInt, bool enabled, uint8_t interior)
	{
		if (!EnsureModel()) return -1;
		if (!std::isfinite(pos.x + pos.y + pos.z + yawDeg + windDirDeg + windInt)) { FLog("[FLAG_PHYSICS] criar: valores invalidos (NaN/Inf)"); return -1; }
		if (id < 0) id = FreeLocalId();
		if (id < 0) { FLog("[FLAG_PHYSICS] sem id local livre"); return -1; }

		FlagObj* f = FlFind(id);
		if (!f)
		{
			if ((int)g_fm.flags.size() >= kMaxFlags) { FLog("[FLAG_PHYSICS] limite de %d bandeiras atingido (id %d recusado)", kMaxFlags, id); return -1; }
			g_fm.flags.push_back(std::make_unique<FlagObj>());
			f = g_fm.flags.back().get();
			f->id = id;
			std::vector<Vec3> rest((size_t)g_fm.model.cols * g_fm.model.rows);
			for (size_t i = 0; i < rest.size(); i++) rest[i] = { g_fm.model.simRest[i * 3], g_fm.model.simRest[i * 3 + 1], g_fm.model.simRest[i * 3 + 2] };
			if (!f->phys.Init(rest.data(), g_fm.model.cols, g_fm.model.rows, 0x9E3779B1u * (uint32_t)(id + 1)) || !f->mesh.Init(&g_fm.model))
			{
				FLog("[FLAG_PHYSICS] falha ao iniciar fisica/malha da bandeira %d", id);
				g_fm.flags.pop_back();
				return -1;
			}
			f->sim.resize(rest.size()); f->nrm.resize(rest.size());
		}
		f->fromServer = fromServer; f->enabled = enabled; f->interior = interior;
		f->pos = pos; f->yawDeg = yawDeg; f->windDirDeg = windDirDeg; f->windInt = windInt;
		f->phys.SetTransform(pos, yawDeg * kDeg2Rad);          // tambem reinicia o tecido em repouso
		f->phys.SetWind(windDirDeg * kDeg2Rad, windInt);
		f->phys.SetEnabled(enabled);
		f->mesh.SetStatic(pos, g_fm.brightness);
		f->idle = 0;
		FLog("[FLAG_PHYSICS] bandeira %d criada em %.1f %.1f %.1f | giro %.0f | vento %.0f graus x %.2f | interior %u | %s (total %zu)",
			 id, pos.x, pos.y, pos.z, yawDeg, windDirDeg, windInt, (unsigned)interior, fromServer ? "servidor" : "local", g_fm.flags.size());
		return id;
	}

	bool RemoveLocked(int id)
	{
		for (size_t i = 0; i < g_fm.flags.size(); i++)
			if (g_fm.flags[i]->id == id) { g_fm.flags.erase(g_fm.flags.begin() + i); FLog("[FLAG_PHYSICS] bandeira %d removida (total %zu)", id, g_fm.flags.size()); return true; }
		return false;
	}

	int ClearLocked(bool serverOnly)
	{
		int n = 0;
		for (size_t i = 0; i < g_fm.flags.size();)
			if (!serverOnly || g_fm.flags[i]->fromServer) { g_fm.flags.erase(g_fm.flags.begin() + i); n++; } else i++;
		if (n) FLog("[FLAG_PHYSICS] %d bandeira(s) removida(s)%s", n, serverOnly ? " (do servidor)" : "");
		return n;
	}

	// SAMP/flags.txt:  x y z [giro [dirVento [intensidade [interior]]]]   ('#' comentario). Sem dirVento = igual ao giro.
	int LoadFileLocked()
	{
		g_fm.fileLoaded = true;
		const std::string p = FlPath(kFlagsFile);
		FILE* f = p.empty() ? nullptr : std::fopen(p.c_str(), "r");
		if (!f) return 0;
		int made = 0, bad = 0, ln = 0;
		char line[256];
		while (std::fgets(line, sizeof(line), f))
		{
			ln++;
			char* h = std::strchr(line, '#'); if (h) *h = 0;
			float v[7] = {}; int cnt = 0;
			char* s = line;
			while (cnt < 7)
			{
				char* e = nullptr; const float x = std::strtof(s, &e);
				if (e == s) break;
				v[cnt++] = x; s = e;
			}
			bool blank = true; for (const char* c = s; *c; c++) if (!std::isspace((unsigned char)*c)) blank = false;
			if (cnt == 0 && blank) continue;
			if (cnt < 3 || !blank) { FLog("[FLAG_PHYSICS] %s linha %d ignorada (formato: x y z [giro [vento [intensidade [interior]]]])", kFlagsFile, ln); bad++; continue; }
			const float yaw = cnt > 3 ? v[3] : 0.0f, wd = cnt > 4 ? v[4] : yaw, wi = cnt > 5 ? v[5] : 1.0f;
			if (CreateLocked(-1, false, { v[0], v[1], v[2] }, yaw, wd, wi, true, (uint8_t)(cnt > 6 ? (int)v[6] : 0)) >= 0) made++; else bad++;
		}
		std::fclose(f);
		FLog("[FLAG_PHYSICS] %s lido: %d bandeira(s) criada(s), %d linha(s) com problema", kFlagsFile, made, bad);
		return made;
	}

	// ---------------------------------------------------------------------------------
	//  Desenho
	// ---------------------------------------------------------------------------------
	void SetStates()
	{
		CHook::CallFunction<void>(g_libGTASA + 0x6F4EC4);   // DefinedState(), igual ao Hose.cpp / IdCard3D.cpp

		RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, RWRSTATE(FALSE));      // sem transparencia (spec)
		RwRenderStateSet(rwRENDERSTATEZTESTENABLE,       RWRSTATE(TRUE));
		RwRenderStateSet(rwRENDERSTATEZWRITEENABLE,      RWRSTATE(TRUE));
		RwRenderStateSet(rwRENDERSTATECULLMODE,          RWRSTATE(rwCULLMODECULLNONE));
		RwRenderStateSet(rwRENDERSTATEFOGENABLE,         RWRSTATE(TRUE));
		RwRenderStateSet(rwRENDERSTATETEXTUREFILTER,     RWRSTATE(rwFILTERLINEAR));
		RwRenderStateSet(rwRENDERSTATETEXTUREADDRESS,    RWRSTATE(rwTEXTUREADDRESSCLAMP));
	}

	struct Im3DSink : ISink
	{
		RwRaster* front = nullptr;
		RwRaster* back  = nullptr;
		void Submit(int pass, const DrawVertex* v, int nv, const uint16_t* idx, int ni) override
		{
			RwRaster* r = pass == PassFront ? front : pass == PassBack ? back : nullptr;
			if (pass != PassStatic && !r) return;
			RwRenderStateSet(rwRENDERSTATETEXTURERASTER, RWRSTATE(r));
			// indice do Im3D e de 16 bits nesta libGTASA (ver nota no Hose.cpp)
			if (RwIm3DTransform(reinterpret_cast<RwIm3DVertex*>(const_cast<DrawVertex*>(v)), (RwUInt32)nv, nullptr, r ? rwIM3D_VERTEXUV : 0))
			{
				RwIm3DRenderIndexedPrimitive(rwPRIMTYPETRILIST, reinterpret_cast<RwImVertexIndex*>(const_cast<uint16_t*>(idx)), ni);
				RwIm3DEnd();
			}
		}
	};

	bool LocalPos(Vec3& out)
	{
		if (!pNetGame) return false;
		CPlayerPool* pool = pNetGame->GetPlayerPool();
		CLocalPlayer* lp = pool ? pool->GetLocalPlayer() : nullptr;
		CPlayerPed* pp = lp ? lp->GetPlayerPed() : nullptr;
		if (!pp || !pp->m_pPed) return false;
		const CVector p = pp->m_pPed->GetPosition();
		out = { p.x, p.y, p.z };
		return true;
	}

	std::vector<std::string> FlSplit(const std::string& s)
	{
		std::vector<std::string> a;
		size_t i = 0;
		while (i < s.size())
		{
			while (i < s.size() && s[i] == ' ') i++;
			size_t j = i;
			while (j < s.size() && s[j] != ' ') j++;
			if (j > i) a.push_back(s.substr(i, j - i));
			i = j;
		}
		return a;
	}
	bool FlNum(const std::string& s, float& v) { char* e = nullptr; v = std::strtof(s.c_str(), &e); return e && *e == 0 && !s.empty() && std::isfinite(v); }
}

// =====================================================================================
//  API
// =====================================================================================

void Flag_OnServerPacket(const FlagPacket& p)
{
	std::lock_guard<std::mutex> lk(g_fm.mtx);
	switch (p.op)
	{
	case FlagOp::Create:
		CreateLocked(p.id, true, { p.x, p.y, p.z }, p.yawDeg, p.windDirDeg, p.windInt, (p.flags & 1) != 0, p.interior);
		break;
	case FlagOp::Remove: RemoveLocked(p.id); break;
	case FlagOp::Clear:  ClearLocked(true);  break;
	case FlagOp::Wind:
		if (!std::isfinite(p.windDirDeg + p.windInt)) break;
		for (auto& f : g_fm.flags)
			if (p.id < 0 ? f->fromServer : f->id == p.id)
			{
				f->windDirDeg = p.windDirDeg; f->windInt = p.windInt;
				f->phys.SetWind(p.windDirDeg * kDeg2Rad, p.windInt);
			}
		break;
	case FlagOp::Enable:
		if (FlagObj* f = FlFind(p.id)) { f->enabled = (p.flags & 1) != 0; f->phys.SetEnabled(f->enabled); }
		break;
	case FlagOp::Move:
		if (FlagObj* f = FlFind(p.id))
		{
			if (!std::isfinite(p.x + p.y + p.z + p.yawDeg)) break;
			f->pos = { p.x, p.y, p.z }; f->yawDeg = p.yawDeg;
			f->phys.SetTransform(f->pos, p.yawDeg * kDeg2Rad);
			f->mesh.SetStatic(f->pos, g_fm.brightness);
		}
		break;
	default: FLog("[FLAG_PHYSICS] RPC 207: op %u desconhecida", (unsigned)p.op); break;
	}
}

void Flag_Reset()
{
	std::lock_guard<std::mutex> lk(g_fm.mtx);
	ClearLocked(true);
}

void Flag_Update()
{
	std::lock_guard<std::mutex> lk(g_fm.mtx);
	if (!g_fm.fileLoaded && g_pszStorage && g_pszStorage[0]) LoadFileLocked();

	const auto now = Clock::now();
	float dt = 0.0f;
	if (g_fm.haveLast) dt = std::chrono::duration<float>(now - g_fm.last).count();
	g_fm.last = now; g_fm.haveLast = true;

	g_fm.lastActive = 0;
	if (g_fm.flags.empty() || !pGame || g_fm.paused) return;
	if (!(dt > 0.0f)) return;
	dt = std::min(dt, MxFlag::kMaxFrameDt);

	static CCamera& TheCamera = *reinterpret_cast<CCamera*>(g_libGTASA + 0xBBA8D0);
	const CVector cam = TheCamera.GetPosition();
	const uint8_t interior = pGame->GetActiveInterior();

	const auto t0 = Clock::now();
	int steps = 0;
	for (auto& up : g_fm.flags)
	{
		FlagObj& f = *up;
		const float dx = f.pos.x - cam.x, dy = f.pos.y - cam.y, dz = f.pos.z + 1.7f - cam.z;
		f.dist = std::sqrt(dx * dx + dy * dy + dz * dz);
		f.active = f.enabled && f.interior == interior && f.dist < kUpdateDist;
		if (!f.active) { f.idle += dt; continue; }
		if (f.idle > 1.0f) f.phys.Reset();             // voltou depois de um tempo: recomeca do repouso (sem "salto" do tecido parado)
		f.idle = 0;
		ApplyLod(f);
		f.phys.Update(dt);
		steps += f.phys.stats.lastSubsteps;
		g_fm.lastActive++;
		if (f.phys.stats.resets != f.seenResets)   // so loga quando o contador de resets AUMENTA
		{
			f.seenResets = f.phys.stats.resets;
			FLog("[FLAG_PHYSICS] bandeira %d: guarda anti-explosao disparou (reset #%u)", f.id, f.phys.stats.resets);
		}
	}
	g_fm.accMs += std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
	g_fm.accFrames++; g_fm.accSteps += steps;

	if (now - g_fm.lastLog > std::chrono::seconds(30) && g_fm.lastActive > 0 && g_fm.accFrames > 0)
	{
		FLog("[FLAG_PHYSICS] %d ativa(s), %d desenhada(s) | fisica %.3f ms/quadro | %.2f passos/quadro (%llu quadros)",
			 g_fm.lastActive, g_fm.lastDrawn, g_fm.accMs / (double)g_fm.accFrames, (double)g_fm.accSteps / (double)g_fm.accFrames, (unsigned long long)g_fm.accFrames);
		g_fm.accMs = 0; g_fm.accFrames = 0; g_fm.accSteps = 0; g_fm.lastLog = now;
	}
}

void Flag_Render()
{
	std::lock_guard<std::mutex> lk(g_fm.mtx);
	g_fm.lastDrawn = 0;
	if (g_fm.flags.empty() || !pGame || g_fm.paused || g_fm.modelState != 1) return;
	if (!EnsureTextures()) return;                     // sem textura nao desenha (nao mostra mastro "pelado")

	static CCamera& TheCamera = *reinterpret_cast<CCamera*>(g_libGTASA + 0xBBA8D0);
	const CVector camC = TheCamera.GetPosition();
	const Vec3 cam = { camC.x, camC.y, camC.z };
	const CVector fwd = TheCamera.GetMatrix().m_forward;

	Im3DSink sink;
	sink.front = g_fm.texFront ? g_fm.texFront->raster : nullptr;
	sink.back  = g_fm.texBack  ? g_fm.texBack->raster  : nullptr;
	if (!sink.front || !sink.back) return;

	bool states = false;
	for (auto& up : g_fm.flags)
	{
		FlagObj& f = *up;
		if (!f.active || f.dist > kRenderDist) continue;
		const float cx = f.pos.x - cam.x, cy = f.pos.y - cam.y, cz = f.pos.z + 1.7f - cam.z;
		if (cx * fwd.x + cy * fwd.y + cz * fwd.z < -3.0f) continue;      // bem atras da camera (raio da bandeira ~2,7 m)

		f.phys.GetRenderPositions(f.sim.data());
		f.phys.ComputeNormals(f.sim.data(), f.nrm.data());
		f.mesh.UpdateCloth(f.sim.data(), f.nrm.data(), cam, g_fm.brightness);

		if (!states) { SetStates(); states = true; }
		f.mesh.Draw(sink);
		g_fm.lastDrawn++;
	}
}

// =====================================================================================
//  Comando do chat: /bandeira
// =====================================================================================

std::string Flag_Command(const std::string& args)
{
	const std::vector<std::string> a = FlSplit(args);
	char buf[300];
	std::lock_guard<std::mutex> lk(g_fm.mtx);

	if (a.empty() || a[0] == "ajuda")
	{
		snprintf(buf, sizeof(buf), "[BANDEIRA] %zu bandeira(s) | uso: criar [x y z [giro]] | teste | lista | remover <id|todas> | vento <id|todas> <graus> <intensidade> | "
			 "girar <id> <graus> | mover <id> x y z | ativar|desativar <id> | luz <0.3-1.5> | parar|seguir | recarregar | info", g_fm.flags.size());
		return buf;
	}
	const std::string& c = a[0];

	if (c == "criar" || c == "teste")
	{
		Vec3 p{};
		float yaw = 0;
		if (c == "criar" && a.size() > 1 && a.size() < 4) return "[BANDEIRA] uso: criar [x y z [giro]]  (sem argumentos = ao lado de voce)";
		if (c == "criar" && a.size() >= 4)
		{
			if (!FlNum(a[1], p.x) || !FlNum(a[2], p.y) || !FlNum(a[3], p.z)) return "[BANDEIRA] coordenadas invalidas";
			if (a.size() >= 5 && !FlNum(a[4], yaw)) return "[BANDEIRA] giro invalido";
		}
		else
		{
			if (!LocalPos(p)) return "[BANDEIRA] jogador local indisponivel (passe x y z)";
			p.x += 2.5f; p.z -= 1.0f;                 // ao lado do jogador, no chao
		}
		const int id = CreateLocked(-1, false, p, yaw, yaw, 1.0f, true, pGame ? pGame->GetActiveInterior() : 0);
		if (id < 0) return g_fm.modelState == 1 ? "[BANDEIRA] nao foi possivel criar (limite/valores)" : "[BANDEIRA] modelo SAMP/models/flag_maximo.dff ausente ou invalido (veja o log [FLAG_PHYSICS])";
		snprintf(buf, sizeof(buf), "[BANDEIRA] bandeira %d criada em %.1f %.1f %.1f", id, p.x, p.y, p.z);
		return buf;
	}

	if (c == "lista")
	{
		if (g_fm.flags.empty()) return "[BANDEIRA] nenhuma bandeira";
		std::string s = "[BANDEIRA]";
		for (auto& f : g_fm.flags)
		{
			snprintf(buf, sizeof(buf), " #%d(%.0f,%.0f,%.0f %s%s %.0fm)", f->id, f->pos.x, f->pos.y, f->pos.z, f->enabled ? "on" : "off", f->fromServer ? " srv" : "", f->dist);
			s += buf;
		}
		return s;
	}

	if (c == "remover")
	{
		if (a.size() < 2) return "[BANDEIRA] uso: remover <id|todas>";
		if (a[1] == "todas") { snprintf(buf, sizeof(buf), "[BANDEIRA] %d removida(s)", ClearLocked(false)); return buf; }
		float v; if (!FlNum(a[1], v)) return "[BANDEIRA] id invalido";
		return RemoveLocked((int)v) ? "[BANDEIRA] removida" : "[BANDEIRA] id nao encontrado";
	}

	if (c == "vento")
	{
		float dir, in;
		if (a.size() < 4 || !FlNum(a[2], dir) || !FlNum(a[3], in)) return "[BANDEIRA] uso: vento <id|todas> <graus> <intensidade 0-4>";
		int n = 0;
		float v;
		for (auto& f : g_fm.flags)
			if (a[1] == "todas" || (FlNum(a[1], v) && f->id == (int)v)) { f->windDirDeg = dir; f->windInt = in; f->phys.SetWind(dir * kDeg2Rad, in); n++; }
		snprintf(buf, sizeof(buf), "[BANDEIRA] vento %.0f graus x %.2f em %d bandeira(s)", dir, in, n);
		return buf;
	}

	if (c == "girar" || c == "mover")
	{
		float id, x, y, z, yaw;
		if (c == "girar" && (a.size() < 3 || !FlNum(a[1], id) || !FlNum(a[2], yaw))) return "[BANDEIRA] uso: girar <id> <graus>";
		if (c == "mover" && (a.size() < 5 || !FlNum(a[1], id) || !FlNum(a[2], x) || !FlNum(a[3], y) || !FlNum(a[4], z))) return "[BANDEIRA] uso: mover <id> x y z";
		FlagObj* f = FlFind((int)id);
		if (!f) return "[BANDEIRA] id nao encontrado";
		if (c == "girar") f->yawDeg = yaw; else f->pos = { x, y, z };
		f->phys.SetTransform(f->pos, f->yawDeg * kDeg2Rad);
		f->mesh.SetStatic(f->pos, g_fm.brightness);
		return "[BANDEIRA] ok";
	}

	if (c == "ativar" || c == "desativar")
	{
		float id; if (a.size() < 2 || !FlNum(a[1], id)) return "[BANDEIRA] uso: ativar|desativar <id>";
		FlagObj* f = FlFind((int)id);
		if (!f) return "[BANDEIRA] id nao encontrado";
		f->enabled = (c == "ativar"); f->phys.SetEnabled(f->enabled);
		return f->enabled ? "[BANDEIRA] ativada" : "[BANDEIRA] desativada (nao simula nem desenha)";
	}

	if (c == "luz")
	{
		float v; if (a.size() < 2 || !FlNum(a[1], v) || v < 0.3f || v > 1.5f) return "[BANDEIRA] uso: luz <0.3-1.5> (brilho do sombreado fixo, p.ex. 0.6 a noite)";
		g_fm.brightness = v;
		for (auto& f : g_fm.flags) f->mesh.SetStatic(f->pos, v);
		return "[BANDEIRA] brilho ajustado";
	}

	if (c == "parar")  { g_fm.paused = true;  return "[BANDEIRA] simulacao e desenho pausados"; }
	if (c == "seguir") { g_fm.paused = false; return "[BANDEIRA] retomado"; }

	if (c == "recarregar")
	{
		ClearLocked(false);
		g_fm.modelState = 0; g_fm.texTries = 0; g_fm.nextTexTry = Clock::time_point{};
		g_fm.texFront = nullptr; g_fm.texBack = nullptr;   // pede de novo ao banco (a referencia antiga fica com o banco)
		const int n = LoadFileLocked();
		snprintf(buf, sizeof(buf), "[BANDEIRA] recarregado: %d bandeira(s) de %s (modelo %s)", n, kFlagsFile, g_fm.modelState == 1 ? "ok" : "ainda nao carregado (sera tentado ao criar)");
		return buf;
	}

	if (c == "info")
	{
		int resets = 0; float st = 1.0f;
		for (auto& f : g_fm.flags) { resets += (int)f->phys.stats.resets; st = std::max(st, f->phys.stats.maxStretchSeen); }
		snprintf(buf, sizeof(buf), "[BANDEIRA] modelo=%s texturas=%s | %zu bandeira(s), %d ativa(s), %d desenhada(s) | fisica %.3f ms/quadro | resets=%d maxEstiramento=%.2f",
				 g_fm.modelState == 1 ? "ok" : g_fm.modelState == 0 ? "nao carregado" : "FALHOU",
				 (g_fm.texFront && g_fm.texBack) ? "ok" : (g_fm.texTries >= 5 ? "FALTAM" : "pendente"),
				 g_fm.flags.size(), g_fm.lastActive, g_fm.lastDrawn, g_fm.accFrames ? g_fm.accMs / (double)g_fm.accFrames : 0.0, resets, st);
		return buf;
	}

	return "[BANDEIRA] comando desconhecido (/bandeira ajuda)";
}
