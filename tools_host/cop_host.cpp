#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include "SkinImageUtil.h"
#include "CorpTexMap.h"
#define FLog(...) do{}while(0)
typedef unsigned short PLAYERID;
constexpr int MAX_PLAYERS = 1004;
struct RpMaterial {}; struct RwTexture {}; struct RpClump {};
char* g_pszStorage = nullptr;
namespace CorpTex { namespace {
constexpr int kCorpModel = 271, kNormalModel = 7;
constexpr const char* kSkinFolder = "272"; constexpr const char* kAppFolder = "aparencia";
constexpr int kMaxMaterials = 512; constexpr size_t kMaxCachedCorps=3, kMaxAppTex=40;
constexpr const char* kPiecePrefix = "fantasy_m_cop_"; constexpr const char* kHideTexture="male01";
#include "corp_region.inc"
#include "corp_push.inc"
#include "corp_helpers.inc"
} // anon
std::string CopCmd(const std::string& cmd, const std::string& args, PLAYERID me) {
#include "cop_block.inc"
    return "(nao e /cop)";
}
const Look& LocalLook() { return s_localLook; }
size_t QueueSize() { return s_q.size(); }
std::string Desc(const Look& l) { return Describe(l); }
} // CorpTex
static int fails=0;
#define CHECK(c) do{ if(!(c)){ std::printf("  FALHOU: %s (linha %d)\n",#c,__LINE__); ++fails; } }while(0)
static bool has(const std::string& s,const char* t){ return s.find(t)!=std::string::npos; }
int main(int argc,char**argv){
  static std::string root = argc>1? argv[1] : "/tmp/coptest/";
  g_pszStorage=(char*)root.c_str();
  auto run=[&](const std::string& a){ std::string r=CorpTex::CopCmd("cop",a,5); std::printf("  /cop %-22s -> %s\n",a.c_str(),r.substr(0,150).c_str()); return r; };
  std::string r;
  r=run(""); CHECK(has(r,"pecas:") && has(r,"nenhuma"));
  r=run("vest3 gnr"); CHECK(has(r,"vest3=corp:gnr")); CHECK(CorpTex::QueueSize()==1);
  r=run("vest1 gnr"); CHECK(has(r,"nao tem a peca vest1"));
  r=run("vest1 gnr_transito"); CHECK(has(r,"vest1=corp:gnr_transito"));
  r=run("hat2 base 1"); CHECK(has(r,"hat2=cop/hat/2/1.png"));
  r=run("hat2 base 5"); CHECK(has(r,"nao achei"));
  r=run("short1 base"); CHECK(has(r,"short1=cop/pants/2/1.png"));
  r=run("torso4 base 1"); CHECK(has(r,"torso4=cop/tshirts/4/1.png"));
  r=run("belt1 off"); CHECK(has(r,"belt1=off"));
  r=run("vest3 psp"); CHECK(has(r,"vest3=corp:psp") && !has(r,"vest3=corp:gnr"));      // troca a mesma peca (nao duplica)
  { const auto& ov=CorpTex::LocalLook().ov; CHECK(ov.size()==6); CHECK(std::is_sorted(ov.begin(),ov.end())); }
  r=run("zzz off"); CHECK(has(r,"peca invalida"));
  r=run("torso9 gnr"); CHECK(has(r,"peca invalida"));
  r=run("vest3"); CHECK(has(r,"uso: /cop vest3"));
  r=run("vest3 nao_existe"); CHECK(has(r,"nao existe"));
  r=run("vest3 ../x"); CHECK(has(r,"invalido"));
  CHECK(has(CorpTex::Desc(CorpTex::LocalLook()),"cop: "));
  r=run("limpar"); CHECK(CorpTex::LocalLook().ov.empty());
  std::printf("%s (%d falha(s))\n",fails?"RESULTADO: FALHOU":"RESULTADO: OK",fails); return fails?1:0;
}
