// JPEG: o jogo compila o stb_image so com PNG (game/sprite2d.cpp). Aqui entra uma copia ESTATICA (so JPEG) para nao colidir com ela.
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#include "../../vendor/stb/stb_image.h"

#include "phonephotos.h"
#include "../../main.h"
#include "../../game/game.h"
#include "../../game/RW/RenderWare.h"
#include "../phone/Jpeg.h"
#include "../phone/PhoneGram.h"
#include "../../vendor/imgui/imgui.h"
#include "../../vendor/armhook/patch.h"
#include <GLES3/gl3.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace Phone;

namespace
{
	constexpr int kMaxPhotos = 60;        // fotos locais guardadas (as mais antigas saem sozinhas)
	constexpr int kMaxCache = 80;         // fotos do Gram em disco
	constexpr size_t kMaxTex = 24;        // texturas na memoria de video ao mesmo tempo
	constexpr int kShotW = 480, kShotH = 480, kMaxJpeg = 88 * 1024;

	int64_t NowMsReal() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }
	int64_t MonoMs() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

	std::string BaseDir() { if (!g_pszStorage || !g_pszStorage[0]) return std::string(); return std::string(g_pszStorage) + "SAMP/celular/"; }
	void MkDir(const std::string& p) { ::mkdir(p.c_str(), 0775); }
	void EnsureDirs() { const std::string b = BaseDir(); if (b.empty()) return; MkDir(std::string(g_pszStorage) + "SAMP"); MkDir(b); MkDir(b + "fotos"); MkDir(b + "cache"); }

	bool ValidLocal(const std::string& n)
	{
		if (n.size() < 10 + 5 + 4 || n.size() > 5 + 14 + 4 || n.compare(0, 5, "foto_") != 0 || n.compare(n.size() - 4, 4, ".jpg") != 0) return false;
		for (size_t i = 5; i + 4 < n.size(); ++i) if (n[i] < '0' || n[i] > '9') return false;
		return true;
	}
	bool ValidMedia(const std::string& m) { return Gram::ValidMedia(m); }

	bool ReadFile(const std::string& path, std::string& out, size_t maxBytes)
	{
		FILE* f = std::fopen(path.c_str(), "rb"); if (!f) return false;
		std::fseek(f, 0, SEEK_END); const long n = std::ftell(f); std::fseek(f, 0, SEEK_SET);
		if (n <= 0 || (size_t)n > maxBytes) { std::fclose(f); return false; }
		out.resize((size_t)n); const size_t got = std::fread(&out[0], 1, (size_t)n, f); std::fclose(f);
		return got == (size_t)n;
	}
	bool WriteFileAtomic(const std::string& path, const std::string& data)
	{
		const std::string tmp = path + ".tmp";
		FILE* f = std::fopen(tmp.c_str(), "wb"); if (!f) return false;
		const bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
		std::fclose(f);
		if (!ok) { ::unlink(tmp.c_str()); return false; }
		return ::rename(tmp.c_str(), path.c_str()) == 0;
	}
	bool LooksJpeg(const std::string& b) { return b.size() > 200 && (uint8_t)b[0] == 0xFF && (uint8_t)b[1] == 0xD8 && (uint8_t)b[2] == 0xFF; }

	// copia da MakeRaster do mapa: pixels RGBA -> RwRaster (textura do jogo)
	RwRaster* MakeRaster(const unsigned char* px, int w, int h)
	{
		if (!RwImageCreate || !RwImageAllocatePixels || !RwImageFindRasterFormat || !RwRasterCreate || !RwRasterSetFromImage) return nullptr;
		RwImage* image = RwImageCreate(w, h, 32);
		if (!image || !RwImageAllocatePixels(image)) { if (image) RwImageDestroy(image); return nullptr; }
		for (int y = 0; y < h; ++y) memcpy(image->cpPixels + (size_t)y * image->stride, px + (size_t)y * w * 4, (size_t)w * 4);
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
		return raster;
	}

	std::vector<std::string> ListDir(const std::string& dir, bool (*ok)(const std::string&))
	{
		std::vector<std::string> v; DIR* d = ::opendir(dir.c_str()); if (!d) return v;
		while (dirent* e = ::readdir(d)) { const std::string n = e->d_name; if (ok(n)) v.push_back(n); }
		::closedir(d); return v;
	}

	struct Shared               // estado tocado pela thread de gravacao
	{
		std::mutex mx; std::vector<std::string> list; bool listValid = false; int serial = 0; std::string err; std::atomic<bool> busy{ false };
	};

	class Store : public PhotoApi
	{
	public:
		Store() : m_sh(std::make_shared<Shared>()) {}
		PhonePhotos::Fetcher fetcher;

		bool canShoot() override { return !BaseDir().empty(); }
		bool shoot() override
		{
			if (BaseDir().empty()) { setErr("Armazenamento indisponível."); return false; }
			if (m_sh->busy.load() || m_req.load()) { setErr("Aguarde a foto anterior."); return false; }
			m_req.store(true); m_sh->busy.store(true); return true;
		}
		bool shooting() override { return m_sh->busy.load(); }
		int shotSerial() override { std::lock_guard<std::mutex> l(m_sh->mx); return m_sh->serial; }
		std::string lastError() override { std::lock_guard<std::mutex> l(m_sh->mx); return m_sh->err; }
		std::vector<std::string> list() override
		{
			std::lock_guard<std::mutex> l(m_sh->mx);
			if (!m_sh->listValid)
			{
				const std::string b = BaseDir();
				m_sh->list = b.empty() ? std::vector<std::string>() : ListDir(b + "fotos", &ValidLocal);
				std::sort(m_sh->list.begin(), m_sh->list.end(), [](const std::string& a, const std::string& c) { return a > c; });
				m_sh->listValid = true;
			}
			return m_sh->list;
		}
		bool read(const std::string& name, std::string& jpeg) override
		{
			if (!ValidLocal(name)) return false;
			return ReadFile(BaseDir() + "fotos/" + name, jpeg, 200 * 1024) && LooksJpeg(jpeg);
		}
		bool remove(const std::string& name) override
		{
			if (!ValidLocal(name)) return false;
			const bool ok = ::unlink((BaseDir() + "fotos/" + name).c_str()) == 0;
			std::lock_guard<std::mutex> l(m_sh->mx); m_sh->listValid = false; ++m_sh->serial;
			dropTex("L:" + name);
			return ok;
		}
		bool texture(const std::string& key, PhotoTex& out) override
		{
			const int frame = ImGui::GetFrameCount();
			auto it = m_tex.find(key);
			if (it != m_tex.end()) { it->second.frame = frame; out.id = (uint64_t)(uintptr_t)it->second.raster; out.w = it->second.w; out.h = it->second.h; return true; }
			auto fl = m_failed.find(key);
			if (fl != m_failed.end()) { if (key[0] == 'R' && MonoMs() - fl->second > 20000) m_failed.erase(fl); else return false; }
			if (key.size() < 3 || key[1] != ':') return false;
			if (frame == m_buildFrame) return false;           // uma textura por quadro (decodificar JPEG + subir para a GPU)
			std::string bytes;
			if (key[0] == 'L')
			{
				const std::string name = key.substr(2);
				if (!ValidLocal(name)) { m_failed[key] = MonoMs(); return false; }
				if (!ReadFile(BaseDir() + "fotos/" + name, bytes, 200 * 1024)) { m_failed[key] = MonoMs(); return false; }
			}
			else if (key[0] == 'R')
			{
				const std::string media = key.substr(2);
				if (!ValidMedia(media)) { m_failed[key] = MonoMs(); return false; }
				if (!ReadFile(BaseDir() + "cache/" + media + ".jpg", bytes, 200 * 1024)) { startDownload(media, key); return false; }
			}
			else return false;
			m_buildFrame = frame;
			int w = 0, h = 0, n = 0;
			unsigned char* px = stbi_load_from_memory((const unsigned char*)bytes.data(), (int)bytes.size(), &w, &h, &n, 4);
			if (!px || w < 1 || h < 1 || w > 1300 || h > 1300) { if (px) stbi_image_free(px); m_failed[key] = MonoMs(); if (key[0] == 'R') ::unlink((BaseDir() + "cache/" + key.substr(2) + ".jpg").c_str()); return false; }
			RwRaster* r = MakeRaster(px, w, h);
			stbi_image_free(px);
			if (!r) { m_failed[key] = MonoMs(); return false; }
			evictIfNeeded(frame);
			Tex t; t.raster = r; t.w = w; t.h = h; t.frame = frame; m_tex[key] = t;
			out.id = (uint64_t)(uintptr_t)r; out.w = w; out.h = h;
			return true;
		}
		bool failed(const std::string& key) override { return m_failed.count(key) != 0; }

		// ---- thread de desenho, 1x por quadro ----
		void frameHook()
		{
			const int frame = ImGui::GetFrameCount();
			// texturas soltas: so destruidas alguns quadros depois (o ImGui do quadro anterior ainda pode apontar para elas)
			for (size_t i = 0; i < m_grave.size();)
				if (frame - m_grave[i].frame > 4 || frame < m_grave[i].frame) { if (RwRasterDestroy) RwRasterDestroy(m_grave[i].raster); m_grave.erase(m_grave.begin() + i); } else ++i;
			if (m_req.exchange(false)) capture();
		}

	private:
		struct Tex { RwRaster* raster = nullptr; int w = 0, h = 0, frame = 0; };
		struct Dead { RwRaster* raster; int frame; };
		std::shared_ptr<Shared> m_sh; std::atomic<bool> m_req{ false };
		std::map<std::string, Tex> m_tex; std::map<std::string, int64_t> m_failed; std::vector<Dead> m_grave; std::set<std::string> m_dl;
		int m_buildFrame = -1; bool m_pruned = false;

		void setErr(const std::string& e) { std::lock_guard<std::mutex> l(m_sh->mx); m_sh->err = e; }
		void dropTex(const std::string& key)
		{
			auto it = m_tex.find(key); if (it == m_tex.end()) return;
			m_grave.push_back({ it->second.raster, ImGui::GetFrameCount() }); m_tex.erase(it);
		}
		void evictIfNeeded(int frame)
		{
			while (m_tex.size() >= kMaxTex)
			{
				auto best = m_tex.end();
				for (auto it = m_tex.begin(); it != m_tex.end(); ++it) if (it->second.frame < frame - 1 && (best == m_tex.end() || it->second.frame < best->second.frame)) best = it;
				if (best == m_tex.end()) break;       // tudo em uso agora: passa um pouco do limite (so enquanto estiver na tela)
				m_grave.push_back({ best->second.raster, frame }); m_tex.erase(best);
			}
		}
		void startDownload(const std::string& media, const std::string& key)
		{
			if (!fetcher || m_dl.count(media) || m_dl.size() >= 3) return;
			if (!m_pruned) { m_pruned = true; EnsureDirs(); pruneCache(); }
			m_dl.insert(media);
			const bool started = fetcher(media, [this, media, key](bool ok, const std::string& bytes)
			{
				m_dl.erase(media);
				if (ok && LooksJpeg(bytes)) { EnsureDirs(); if (!WriteFileAtomic(BaseDir() + "cache/" + media + ".jpg", bytes)) m_failed[key] = MonoMs(); }
				else m_failed[key] = MonoMs();
			});
			if (!started) { m_dl.erase(media); m_failed[key] = MonoMs() - 15000; }        // sem servico agora: tenta de novo em ~5 s
		}
		void pruneCache()
		{
			const std::string dir = BaseDir() + "cache/";
			std::vector<std::pair<time_t, std::string>> v;
			DIR* d = ::opendir(dir.c_str()); if (!d) return;
			while (dirent* e = ::readdir(d)) { const std::string n = e->d_name; if (n.size() == 36 && n.compare(32, 4, ".jpg") == 0) { struct stat st; if (::stat((dir + n).c_str(), &st) == 0) v.push_back({ st.st_mtime, n }); } }
			::closedir(d);
			if ((int)v.size() <= kMaxCache) return;
			std::sort(v.begin(), v.end());
			for (size_t i = 0; i + kMaxCache < v.size(); ++i) ::unlink((dir + v[i].second).c_str());
		}

		static bool AllBlack(const std::vector<uint8_t>& px)
		{
			for (size_t i = 0; i < px.size(); i += 4 * 211) if (px[i] || px[i + 1] || px[i + 2]) return false;
			return true;
		}
		// O GTA SA Android 2.10 desenha numa thread propria (fila de render, RQ): a thread do jogo NAO tem contexto GL, entao
		// glReadPixels direto aqui nao le nada (vinha tudo preto). emu_glReadPixels e a funcao do proprio motor: descarrega o
		// alvo alternativo, espera a fila de render esvaziar e le o framebuffer NA thread de render.
		typedef void (*EmuReadPixelsFn)(int, int, int, int, unsigned, unsigned, void*);
		static EmuReadPixelsFn EmuReadPixels()
		{
			static EmuReadPixelsFn fn = (EmuReadPixelsFn)CHook::getSym("_Z16emu_glReadPixelsiiiijjPv");
			return fn;
		}
		void capture()
		{
			const int w = (int)ImGui::GetIO().DisplaySize.x, h = (int)ImGui::GetIO().DisplaySize.y;
			if (w < 64 || h < 64 || w > 8192 || h > 8192) { fail("Captura: tela " + std::to_string(w) + "x" + std::to_string(h)); return; }
			EmuReadPixelsFn rd = EmuReadPixels();
			if (!rd) { fail("Captura indisponível (emu_glReadPixels não achada)"); return; }
			std::vector<uint8_t> px((size_t)w * h * 4);
			rd(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
			// tela toda preta = captura nao funcionou (nao grava foto preta fingindo que deu certo)
			if (AllBlack(px)) { fail("Captura preta (" + std::to_string(w) + "x" + std::to_string(h) + ")"); return; }
			std::shared_ptr<Shared> sh = m_sh;
			std::thread([sh, w, h, p = std::move(px)]() mutable
			{
				int ow = 0, oh = 0;
				std::vector<uint8_t> small = Image::Fit(p.data(), w, h, kShotW, kShotH, true, ow, oh);
				std::vector<uint8_t>().swap(p);
				std::string jpg;
				for (int q : { 72, 55, 40 }) { jpg = Jpeg::Encode(small.data(), ow, oh, q); if (!jpg.empty() && (int)jpg.size() <= kMaxJpeg) break; }
				bool ok = !jpg.empty() && (int)jpg.size() <= kMaxJpeg;
				std::string err;
				if (ok)
				{
					EnsureDirs();
					char nm[48]; std::snprintf(nm, sizeof(nm), "foto_%013lld.jpg", (long long)NowMsReal());
					ok = WriteFileAtomic(BaseDir() + "fotos/" + nm, jpg);
					if (!ok) err = "Não consegui gravar a foto.";
					else
					{
						const std::string dir = BaseDir() + "fotos";
						std::vector<std::string> all = ListDir(dir, &ValidLocal); std::sort(all.begin(), all.end());
						for (size_t i = 0; (int)all.size() - (int)i > kMaxPhotos; ++i) ::unlink((dir + "/" + all[i]).c_str());
					}
				}
				else err = "Não consegui gerar a foto.";
				std::lock_guard<std::mutex> l(sh->mx);
				if (ok) { sh->listValid = false; ++sh->serial; sh->err.clear(); } else sh->err = err;
				sh->busy.store(false);
			}).detach();
		}
		void fail(const std::string& e) { setErr(e); m_sh->busy.store(false); }
	};

	Store* S() { static Store* s = new Store(); return s; }
}

namespace PhonePhotos
{
	PhotoApi* Api() { return S(); }
	void FrameHook() { S()->frameHook(); }
	void SetFetcher(Fetcher f) { S()->fetcher = std::move(f); }
}
