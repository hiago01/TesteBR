#include "RadarSkin.h"
#include "../main.h"
#include "../vendor/armhook/patch.h"
#include "../vendor/SimpleIni/SimpleIni.h"
#include "../vendor/stb/stb_image.h"   // implementacao em game/sprite2d.cpp
#include "RW/RenderWare.h"
#include "SkinImageUtil.h"
#include "Textures/TextureDatabaseRuntime.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <list>
#include <string>
#include <unordered_map>
#include <vector>
#include <sys/stat.h>

namespace
{
	constexpr int kTiles = 12, kCount = 144;
	constexpr uintptr_t kRadarTexturesOff = 0xC21090;   // gRadarTextures (RwTexture*[144]) - simbolo exportado de libGTASA
	constexpr uintptr_t kOceanInsn[3] = { 0x528CF0, 0x528CF4, 0x528CF8 };   // MOVZ w1/w2/w3 = 111,137,170 em DrawRadarSection
	constexpr int kUiMaxResident = 48;                  // tiles 256x256 (256 KB) na interface do mapa

	std::atomic<bool> g_enabled{ false };
	bool g_installed = false, g_missing[kCount] = {}, g_oceanPatched = false;
	int  g_ocean[3] = { 143, 189, 240 };
	int  g_loaded = 0, g_failed = 0;

	void (*CRadar__RequestMapSection)(int, int) = nullptr;

	std::string Dir() { return g_pszStorage ? std::string(g_pszStorage) + "SAMP/radar/" : std::string(); }
	std::string TilePath(int idx) { char b[40]; std::snprintf(b, sizeof(b), "tiles/radar%02d.png", idx); return Dir() + b; }
	bool FileExists(const std::string& p) { struct stat st; return !p.empty() && stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode); }

	RwTexture** Array() { return reinterpret_cast<RwTexture**>(g_libGTASA + kRadarTexturesOff); }

	// RGBA -> RwTexture (mesmo caminho do CorpTex/inventario): sem mipmap, linear, CLAMP (borda do tile nao sangra)
	RwTexture* MakeTexture(const uint8_t* px, int w, int h, const char* name)
	{
		if (!RwImageCreate || !RwImageAllocatePixels || !RwImageFindRasterFormat || !RwRasterCreate || !RwRasterSetFromImage) return nullptr;
		RwImage* image = RwImageCreate(w, h, 32);
		if (!image) return nullptr;
		if (!RwImageAllocatePixels(image)) { RwImageDestroy(image); return nullptr; }
		for (int y = 0; y < h; ++y) std::memcpy(image->cpPixels + (size_t)y * image->stride, px + (size_t)y * w * 4, (size_t)w * 4);
		int rw = w, rh = h, rd = 32, rf = 0;
		RwImageFindRasterFormat(image, rwRASTERTYPETEXTURE, &rw, &rh, &rd, &rf);
		rf &= ~(rwRASTERFORMATMIPMAP | rwRASTERFORMATAUTOMIPMAP);
		if (rf & (rwRASTERFORMATPAL8 | rwRASTERFORMATPAL4))
		{
			rf &= ~(rwRASTERFORMATPAL8 | rwRASTERFORMATPAL4 | rwRASTERFORMATPIXELFORMATMASK);
			rf |= rwRASTERFORMAT8888; rd = 32;
		}
		if ((rf & rwRASTERFORMATPIXELFORMATMASK) == 0) rf |= rwRASTERFORMAT8888;
		RwRaster* raster = RwRasterCreate(rw, rh, rd, rf);
		if (!raster) { RwImageDestroy(image); return nullptr; }
		if (!RwRasterSetFromImage(raster, image)) { if (RwRasterDestroy) RwRasterDestroy(raster); RwImageDestroy(image); return nullptr; }
		RwImageDestroy(image);
		RwTexture* tex = RwTextureCreate(raster);
		if (!tex) { if (RwRasterDestroy) RwRasterDestroy(raster); return nullptr; }
		RwTextureSetName(tex, name);
		RwTextureSetFilterMode(tex, rwFILTERLINEAR);
		RwTextureSetAddressing(tex, rwTEXTUREADDRESSCLAMP);
		return tex;
	}

	// size = 0: tamanho do arquivo; senao reduz para size x size (media de area)
	RwTexture* LoadTile(int idx, int size)
	{
		if (idx < 0 || idx >= kCount || g_missing[idx]) return nullptr;
		const std::string path = TilePath(idx);
		int w = 0, h = 0, ch = 0;
		unsigned char* px = FileExists(path) ? stbi_load(path.c_str(), &w, &h, &ch, 4) : nullptr;
		if (!px) { g_missing[idx] = true; if (++g_failed == 1) FLog("[RADARSKIN] tile ausente/invalido: %s (usa o nativo)", path.c_str()); return nullptr; }
		char nm[24]; std::snprintf(nm, sizeof(nm), "radarskin%02d", idx);
		RwTexture* t = nullptr;
		if (size > 0 && (w != size || h != size))
		{
			std::vector<uint8_t> small;
			if (SkinImg::ResizeRgba(px, w, h, size, size, small)) t = MakeTexture(small.data(), size, size, nm);
		}
		else t = MakeTexture(px, w, h, nm);
		stbi_image_free(px);
		if (!t) FLog("[RADARSKIN] falha ao criar textura do tile %02d", idx);
		return t;
	}


	// ---- banco texdb "mapa" (texdb/mapa/mapa.{txt,etc|dxt.*}) - mesmo caminho do radar nativo (banco gta3, refCount+1) ----
	// Textura comprimida (ETC1/DXT1) lida pelo motor: sem decodificar PNG, 32 KB por tile (256x256) em vez de 256 KB RGBA.
	// -1 = indisponivel, 0 = ainda nao tentou, 1 = carregado
	int  g_dbState = 0;
	bool g_dbMissing[kCount] = {};
	int  g_dbHit = 0;
	const char* g_dbFmt = "-";

	std::string TexdbDir() { return g_pszStorage ? std::string(g_pszStorage) + "texdb/mapa/" : std::string(); }

	void InitDb()
	{
		if (g_dbState != 0) return;
		g_dbState = -1;
		const std::string d = TexdbDir();
		if (d.empty() || !FileExists(d + "mapa.txt")) return;
		TextureDatabaseRuntime* ref = TextureDatabaseRuntime::GetDatabase("gta3");      // formato que ESTE aparelho usa
		if (!ref) { FLog("[RADARSKIN] banco gta3 nao encontrado: sem formato de referencia"); return; }
		const TextureDatabaseFormat fmt = ref->loadedFormat;
		const char* ext = fmt == DF_ETC ? "etc" : fmt == DF_DXT ? "dxt" : nullptr;
		if (!ext) { FLog("[RADARSKIN] formato do aparelho (%d) nao gerado pelo banco mapa (so etc/dxt): usa PNG", (int)fmt); return; }
		for (const char* e : { "dat", "toc" })
			if (!FileExists(d + "mapa." + ext + "." + e)) { FLog("[RADARSKIN] falta mapa.%s.%s: usa PNG", ext, e); return; }
		if (!TextureDatabaseRuntime::GetDatabase("mapa") && !TextureDatabaseRuntime::Load("mapa", true, fmt)) { FLog("[RADARSKIN] Load(mapa) falhou: usa PNG"); return; }
		if (!TextureDatabaseRuntime::GetDatabase("mapa")) { FLog("[RADARSKIN] banco mapa nao ficou carregado: usa PNG"); return; }
		g_dbFmt = ext; g_dbState = 1;
		FLog("[RADARSKIN] banco texdb mapa carregado INTEIRO na memoria (%s), sem streaming", ext);
	}

	bool DbAvailable() { InitDb(); return g_dbState == 1; }

	// nullptr se o banco/tile nao existir. Chamar so na thread do jogo (mesma regra do RequestMapSection).
	RwTexture* LoadTileDb(int idx)
	{
		if (idx < 0 || idx >= kCount || g_dbMissing[idx] || !DbAvailable()) return nullptr;
		TextureDatabaseRuntime* db = TextureDatabaseRuntime::GetDatabase("mapa");
		if (!db) return nullptr;
		char nm[16]; std::snprintf(nm, sizeof(nm), "gmap%03d", idx);
		TextureDatabaseRuntime::Register(db);
		RwTexture* t = TextureDatabaseRuntime::GetTexture(nm);
		TextureDatabaseRuntime::Unregister(db);          // sempre, mesmo sem textura
		if (!t) { g_dbMissing[idx] = true; return nullptr; }
		++t->refCount;                                    // igual ao radar nativo; RemoveMapSection/RwTextureDestroy devolve
		++g_dbHit;
		return t;
	}

	void EnsureInit();
	void CRadar__RequestMapSection_hook(int x, int y)
	{
		EnsureInit();
		if (g_enabled.load() && x >= 0 && x < kTiles && y >= 0 && y < kTiles)
		{
			RwTexture** arr = Array();
			const int idx = y * kTiles + x;
			if (!arr[idx])
			{
				if (RwTexture* t = LoadTileDb(idx)) { arr[idx] = t; g_loaded++; return; }      // 1o: banco texdb (comprimido, sem decodificar PNG)
				if (RwTexture* t = LoadTile(idx, 0)) { arr[idx] = t; g_loaded++; return; }      // 2o: PNG
   // refCount 1; RemoveMapSection destroi
			}
		}
		CRadar__RequestMapSection(x, y);   // nativo
	}

	void PatchOcean(bool custom)
	{
		const int rgb[3] = { custom ? g_ocean[0] : 111, custom ? g_ocean[1] : 137, custom ? g_ocean[2] : 170 };
		for (int i = 0; i < 3; i++)
		{
			const uint32_t insn = 0x52800000u | ((uint32_t)(rgb[i] & 0xFF) << 5) | (uint32_t)(i + 1);   // movz w(i+1), #rgb
			CHook::WriteMemory(g_libGTASA + kOceanInsn[i], (uintptr_t)&insn, 4);
		}
		g_oceanPatched = custom;
	}

	// O jogo solta todos os tiles e volta a pedir (o hook decide de onde vem cada um). Feito no proximo
	// desenho do radar (thread do jogo), nunca direto do chat/interface.
	std::atomic<bool> g_reloadPending{ false };
	void ReloadGameTiles() { g_reloadPending.store(true); }

	void (*CRadar__DrawRadarMap)() = nullptr;
	void CRadar__DrawRadarMap_hook()
	{
		if (g_reloadPending.exchange(false)) CHook::CallFunction<void>("_ZN6CRadar19RemoveRadarSectionsEv");
		CRadar__DrawRadarMap();
	}

	void LoadIni()
	{
		const std::string p = Dir() + "radarskin.ini";
		CSimpleIniA ini; ini.SetUnicode();
		if (ini.LoadFile(p.c_str()) < 0) return;
		if (ini.GetValue("radarskin", "enabled")) g_enabled.store(ini.GetLongValue("radarskin", "enabled", 1) != 0);
		if (const char* o = ini.GetValue("radarskin", "ocean"))
		{
			int r, g, b;
			if (std::sscanf(o, "%d , %d , %d", &r, &g, &b) == 3 || std::sscanf(o, "%d,%d,%d", &r, &g, &b) == 3)
			{ g_ocean[0] = std::clamp(r, 0, 255); g_ocean[1] = std::clamp(g, 0, 255); g_ocean[2] = std::clamp(b, 0, 255); }
		}
	}

	void SaveEnabled()
	{
		const std::string p = Dir() + "radarskin.ini";
		CSimpleIniA ini; ini.SetUnicode(); ini.LoadFile(p.c_str());
		ini.SetLongValue("radarskin", "enabled", g_enabled.load() ? 1 : 0);
		char b[32]; std::snprintf(b, sizeof(b), "%d,%d,%d", g_ocean[0], g_ocean[1], g_ocean[2]);
		ini.SetValue("radarskin", "ocean", b);
		ini.SaveFile(p.c_str());
	}

	bool g_init = false;
	void EnsureInit()
	{
		if (g_init || !g_pszStorage) return;
		g_init = true;
		g_enabled.store(FileExists(TilePath(0)) || FileExists(TexdbDir() + "mapa.txt"));      // padrao: liga se houver banco ou PNG
		LoadIni();
		if (g_enabled.load() && !FileExists(TilePath(0)) && !FileExists(TexdbDir() + "mapa.txt")) g_enabled.store(false);
		if (g_enabled.load()) PatchOcean(true);
		FLog("[RADARSKIN] %s (tiles em %stiles/, oceano %d,%d,%d)", g_enabled.load() ? "LIGADO" : "desligado/sem PNG", Dir().c_str(), g_ocean[0], g_ocean[1], g_ocean[2]);
	}

	// ---- tiles da interface do mapa aberto (256x256, LRU) ----
	struct UiEntry { RwTexture* tex; std::list<int>::iterator it; };
	std::unordered_map<int, UiEntry> g_ui;
	std::list<int> g_uiLru;
	bool g_uiMissing[kCount] = {};
}

namespace RadarSkin
{
	bool HasTiles() { return FileExists(TilePath(0)) || FileExists(TexdbDir() + "mapa.txt"); }
	bool Enabled() { EnsureInit(); return g_enabled.load() && HasTiles(); }

	void InstallHooks()
	{
		if (g_installed) return;
		g_installed = true;
		CHook::InlineHook("_ZN6CRadar17RequestMapSectionEii", &CRadar__RequestMapSection_hook, &CRadar__RequestMapSection);
		CHook::InlineHook("_ZN6CRadar12DrawRadarMapEv", &CRadar__DrawRadarMap_hook, &CRadar__DrawRadarMap);
		// o resto (ler ini, ligar, trocar a cor do oceano) acontece no 1o pedido de tile (EnsureInit): g_pszStorage ja existe
	}

	std::string Command(const std::string& args)
	{
		EnsureInit();
		if (args == "on" || args == "off")
		{
			if (args == "on" && !HasTiles()) return "[RADARSKIN] sem banco texdb/mapa nem PNG em SAMP/radar/tiles/";
			g_enabled.store(args == "on");
			PatchOcean(args == "on");
			SaveEnabled();
			ReloadGameTiles();
			return args == "on" ? "[RADARSKIN] ligado (tiles PNG)" : "[RADARSKIN] desligado (tiles nativos)";
		}
		if (args == "reload")
		{
			std::memset(g_missing, 0, sizeof(g_missing)); std::memset(g_uiMissing, 0, sizeof(g_uiMissing)); std::memset(g_dbMissing, 0, sizeof(g_dbMissing));
			g_loaded = g_failed = 0;
			LoadIni();
			if (g_enabled.load()) PatchOcean(true);
			ReloadGameTiles();
			return "[RADARSKIN] recarregado";
		}
		char b[300];
		std::snprintf(b, sizeof(b), "[RADARSKIN] %s | PNG=%d banco=%s(%d) | carregados=%d ausentes=%d | oceano=%d,%d,%d | use /radarskin on|off|reload",
			g_enabled.load() ? "ligado" : "desligado", (int)FileExists(TilePath(0)), g_dbState == 1 ? g_dbFmt : (g_dbState < 0 ? "indisp" : "?"), g_dbHit, g_loaded, g_failed, g_ocean[0], g_ocean[1], g_ocean[2]);
		return b;
	}

	void* UiTileRaster(int idx)
	{
		EnsureInit();
		if (!g_enabled.load() || idx < 0 || idx >= kCount || g_uiMissing[idx]) return nullptr;
		auto it = g_ui.find(idx);
		if (it != g_ui.end())
		{
			g_uiLru.erase(it->second.it); g_uiLru.push_front(idx); it->second.it = g_uiLru.begin();
			return it->second.tex->raster;
		}
		RwTexture* t = LoadTileDb(idx);
		if (!t) t = LoadTile(idx, 256);
		if (!t) { g_uiMissing[idx] = true; return nullptr; }
		g_uiLru.push_front(idx);
		g_ui[idx] = { t, g_uiLru.begin() };
		while ((int)g_ui.size() > (g_dbState == 1 ? kCount : kUiMaxResident))   // banco: 144 x 32 KB cabe inteiro; PNG: 48 x 256 KB
		{
			const int old = g_uiLru.back(); g_uiLru.pop_back();
			auto o = g_ui.find(old);
			if (o != g_ui.end()) { if (RwTextureDestroy) RwTextureDestroy(o->second.tex); g_ui.erase(o); }
		}
		return t->raster;
	}
}
