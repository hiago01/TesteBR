#include "main.h"
#include "game/game.h"
#include "game/RW/RenderWare.h"
#include "net/netgame.h"
#include "util/CUtil.h"
#include "game/Flag.h"
#include <cstdlib>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <set>
#include <vector>
char* g_pszStorage = nullptr; uintptr_t g_libGTASA = 0; CGame* pGame = nullptr; CNetGame* pNetGame = nullptr;
static CCamera g_cam; static CGame g_game; static CNetGame g_net; static CPlayerPool g_pool; static CLocalPlayer g_lp; static CPed_ g_ped; static CPlayerPed g_pp{&g_ped};
CLocalPlayer* CPlayerPool::GetLocalPlayer() { return &g_lp; } CPlayerPool* CNetGame::GetPlayerPool() { return &g_pool; } CPlayerPed* CLocalPlayer::GetPlayerPed() { return &g_pp; }
static std::vector<std::string> g_log;
void FLog(const char* f, ...) { char b[512]; va_list a; va_start(a, f); vsnprintf(b, sizeof b, f, a); va_end(a); g_log.push_back(b); }
static RwRaster r1{1}, r2{2}; static RwTexture t1{&r1}, t2{&r2}; bool g_texOk = true; int g_texCalls = 0;
RwTexture* CUtil::LoadTextureFromDB(const char* db, const char* t) { g_texCalls++; if (!g_texOk) return nullptr; if (strcmp(db, "mapasmaximo")) return nullptr; return !strcmp(t, "flag_maximo") ? &t1 : !strcmp(t, "flag_maximo_verso") ? &t2 : nullptr; }
static void* g_raster; static int g_batches[3], g_tris[3], g_maxv, g_open, g_bad; static std::set<int> g_states;
RwBool RwRenderStateSet(int s, void* v) { g_states.insert(s); if (s == rwRENDERSTATETEXTURERASTER) g_raster = v; return 1; }
static int g_nv;
void* RwIm3DTransform(RwIm3DVertex* v, RwUInt32 n, void*, RwUInt32 fl) { if (g_open) g_bad++; g_open = 1; g_nv = n; g_maxv = n > (RwUInt32)g_maxv ? n : g_maxv; if ((fl & rwIM3D_VERTEXUV) == 0 && g_raster) g_bad++; return v; }
RwBool RwIm3DRenderIndexedPrimitive(int, RwImVertexIndex* idx, int ni) { if (!g_open) g_bad++; const uint16_t* p = (const uint16_t*)idx; for (int i = 0; i < ni; i++) if (p[i] >= g_nv) g_bad++;
  int k = g_raster == &r1 ? 1 : g_raster == &r2 ? 2 : 0; g_batches[k]++; g_tris[k] += ni / 3; return 1; }
RwBool RwIm3DEnd() { g_open = 0; return 1; }
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("  FALHOU: " #c " -- "); printf(__VA_ARGS__); printf("\n"); } } while (0)
static void frame(int n, float dt = 1/60.f) { extern void usleep_ms(int); for (int i = 0; i < n; i++) { Flag_Update(); Flag_Render(); usleep_ms((int)(dt * 1000)); } }
#include <unistd.h>
void usleep_ms(int ms) { usleep(ms * 1000); }
static void dumplog(size_t from) { for (size_t i = from; i < g_log.size(); i++) printf("    LOG %s\n", g_log[i].c_str()); }
int main()
{
	static char store[512]; snprintf(store, sizeof store, "%s/", getenv("FLAG_STORE") ? getenv("FLAG_STORE") : "/tmp/flag_mockstore"); g_pszStorage = store; g_libGTASA = (uintptr_t)&g_cam - 0xBBA8D0;
	pGame = &g_game; pNetGame = &g_net; g_cam.pos = {0, -10, 3}; g_cam.mat.m_forward = {0, 1, 0};
	char fp[600]; snprintf(fp, sizeof fp, "%sSAMP/flags.txt", store); FILE* f = fopen(fp, "w"); fputs("# teste\n100 100 10 0 0 1.0\n110 100 10 90 90 0.5   # com vento fraco\nlixo aqui\n120 100 10 0 0 1 3\n", f); fclose(f);
	printf("[A] carga: flags.txt + modelo + texturas\n");
	frame(3); size_t l0 = 0; dumplog(l0);
	printf("  %s\n", Flag_Command("info").c_str());
	printf("  %s\n", Flag_Command("lista").c_str());
	printf("[B] 5 s a ~60 fps, camera a ~100 m das bandeiras\n");
	g_cam.pos = {105, 60, 12}; g_cam.mat.m_forward = {0, 1, 0};
	memset(g_batches, 0, sizeof g_batches); memset(g_tris, 0, sizeof g_tris);
	frame(120);
	printf("  lotes estatico/frente/verso = %d/%d/%d | maior lote=%d vert | erros de chamada=%d\n", g_batches[0], g_batches[1], g_batches[2], g_maxv, g_bad);
	CHECK(g_bad == 0, "uso invalido da API Im3D"); CHECK(g_maxv <= 240, "lote grande"); CHECK(g_batches[1] > 0 && g_batches[0] > 0, "nao desenhou");
	printf("  %s\n", Flag_Command("info").c_str());
	printf("[C] interior diferente => nao simula nem desenha\n");
	g_game.interior = 5; memset(g_batches, 0, sizeof g_batches); frame(10); CHECK(g_batches[0] + g_batches[1] + g_batches[2] == 0, "desenhou em outro interior"); g_game.interior = 0;
	printf("[D] camera atras: nao desenha; longe (>150 m): nao desenha\n");
	g_cam.pos = {105, 140, 12}; memset(g_batches, 0, sizeof g_batches); frame(5); CHECK(g_batches[0] + g_batches[1] + g_batches[2] == 0, "desenhou atras da camera");
	g_cam.pos = {105, -300, 12}; memset(g_batches, 0, sizeof g_batches); frame(5); CHECK(g_batches[0] + g_batches[1] + g_batches[2] == 0, "desenhou alem de 150 m");
	printf("[E] comandos\n");
	g_cam.pos = {105, 60, 12};
	for (const char* c : { "criar", "criar 50 50 5 45", "criar abc", "vento todas 180 2", "vento 1 45 0.5", "girar 1 30", "mover 1 101 100 10", "desativar 2", "ativar 2", "luz 0.6", "luz 9", "remover 999", "remover 4", "teste", "parar", "seguir", "xyz", "ajuda" })
		printf("  /bandeira %-22s -> %s\n", c, Flag_Command(c).c_str());
	frame(30);
	printf("[F] servidor (RPC 207)\n");
	FlagPacket p; p.op = FlagOp::Create; p.id = 1000; p.x = 100; p.y = 105; p.z = 10; p.yawDeg = 0; p.windDirDeg = 0; p.windInt = 1; Flag_OnServerPacket(p);
	p.id = 1001; p.x = NAN; Flag_OnServerPacket(p);
	p.op = FlagOp::Wind; p.id = -1; p.windDirDeg = 120; p.windInt = 2; Flag_OnServerPacket(p);
	p.op = FlagOp::Enable; p.id = 1000; p.flags = 0; Flag_OnServerPacket(p); p.flags = 1; Flag_OnServerPacket(p);
	p.op = FlagOp::Move; p.id = 1000; p.x = 101; p.y = 106; p.z = 10; Flag_OnServerPacket(p);
	p.op = 99; Flag_OnServerPacket(p);
	frame(30); printf("  %s\n", Flag_Command("lista").c_str());
	Flag_Reset(); printf("  apos Flag_Reset (reconexao): %s\n", Flag_Command("lista").c_str());
	CHECK(Flag_Command("lista").find("#1000") == std::string::npos, "flag do servidor nao removida");
	printf("[G] limite de 24 bandeiras\n");
	for (int i = 0; i < 30; i++) Flag_Command("criar 10 10 10"); printf("  %s\n", Flag_Command("ajuda").c_str());
	printf("[H] sem textura no banco: nao desenha, tenta de novo ate 5x e para\n");
	Flag_Command("remover todas"); g_texOk = false; Flag_Command("recarregar"); g_texCalls = 0; memset(g_batches, 0, sizeof g_batches);
	frame(5); CHECK(g_batches[1] == 0 && g_batches[2] == 0, "desenhou sem textura"); printf("  chamadas ao banco: %d | %s\n", g_texCalls, Flag_Command("info").c_str());
	printf("\n--- log completo [FLAG_PHYSICS] (primeiras 30 linhas) ---\n"); for (size_t i = 0; i < g_log.size() && i < 30; i++) printf("%s\n", g_log[i].c_str());
	printf("\n%s (%d falhas)\n", fails ? "REPROVADO" : "APROVADO", fails); return fails;
}
