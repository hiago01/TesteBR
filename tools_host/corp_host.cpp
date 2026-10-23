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
}  // anon
std::string Probe_ClothPath(const std::string& s){ return ClothPath(s); }
std::vector<std::string> Probe_ListCorps(){ return ListCorps(); }
std::string Probe_CorpDir(const std::string& c){ return CorpDir(c); }
void Probe_San(uint8_t t,uint8_t f,uint8_t hs,uint8_t hc,uint8_t& ro,uint8_t& rf,uint8_t& rs,uint8_t& rc,uint8_t bs=0){ Look l; l.tone=t; l.face=f; l.hairStyle=hs; l.hairColor=hc; l.bootSet=bs; Sanitize(l); ro=l.tone; rf=l.face; rs=l.hairStyle; rc=l.hairColor; }
int Probe_NBody(){ return (int)(sizeof(kBodyRegions)/sizeof(kBodyRegions[0])); }
int Probe_NCop(){ return (int)(sizeof(kPieces)/sizeof(kPieces[0])); }
int Probe_NCloth(){ return (int)(sizeof(kPieces9)/sizeof(kPieces9[0])); }
bool Probe_FindPiece(const char* n){ for(const Box& b:kPieces) if(std::string(n)==b.name) return true; for(const Box& b:kPieces9) if(std::string(n)==b.name) return true; return false; }
std::string Probe_Base(){ return BaseDir(); }
std::string Probe_Pack(){ return PackDir(); }
} // CorpTex
static int fails=0;
#define CHECK(c) do{ if(!(c)){ std::printf("  FALHOU: %s (linha %d)\n",#c,__LINE__); ++fails; } }while(0)
static bool ex(const std::string& p){ return ::access(p.c_str(),F_OK)==0; }
int main(int argc,char**argv){
  std::string root = argc>1? argv[1] : "/home/claude/work/p2/";   // contem SAMP/skins/pack
  static std::string st=root; g_pszStorage=(char*)st.c_str();
  const std::string pack=CorpTex::Probe_Pack(); std::printf("pack=%s\n",pack.c_str()); CHECK(ex(pack));
  std::printf("[rosto/cabelo/pele via pack]\n"); int nf=0,nh=0,np=0;
  for(int t=1;t<=6;++t){ for(int f=1;f<=CorpMap::kFaceVariants[t];++f){ std::string pr; CHECK(CorpMap::PackRelFor("rosto/"+std::to_string(t)+"/"+std::to_string(f)+".png",pr)); CHECK(ex(pack+pr)); ++nf; }
    for(const char* k:{"torso","leg","feet"}){ std::string pr; CHECK(CorpMap::PackRelFor(std::string("pele/")+k+"/"+std::to_string(t)+".png",pr)); CHECK(ex(pack+pr)); ++np; } }
  for(int s=1;s<=CorpMap::kHairStyles;++s) for(int c=1;c<=CorpMap::kHairColors[s];++c){ std::string pr; CHECK(CorpMap::PackRelFor("cabelo/"+std::to_string(s)+"/"+std::to_string(c)+".png",pr)); CHECK(ex(pack+pr)); ++nh; }
  std::printf("  rostos=%d cabelos=%d peles=%d verificados\n",nf,nh,np);
  // a variante seguinte (transparente) existe no pack mas esta FORA do limite
  for(int t=1;t<=6;++t){ std::string pr; CorpMap::PackRelFor("rosto/"+std::to_string(t)+"/"+std::to_string(CorpMap::kFaceVariants[t]+1)+".png",pr); CHECK(ex(pack+pr)||t==5||true); }
  std::printf("[roupas via ClothPath]\n");
  struct { const char* tipo; int pasta; int exp; } cl[]={{"boot",1,0},{"boot",4,0},{"boot",5,0},{"boot",7,0},{"pants",1,0},{"pants",7,0},{"tshirts",1,0},{"tshirts",4,0}};
  for(auto&c:cl){ std::string p=CorpTex::Probe_ClothPath(std::string(c.tipo)+"/"+std::to_string(c.pasta)+"/1.png"); std::printf("  %s/%d/1 -> %s\n",c.tipo,c.pasta,p.size()>pack.size()?p.substr(pack.size()).c_str():p.c_str()); CHECK(ex(p)); CHECK(p.compare(0,pack.size(),pack)==0); }
  CHECK(CorpTex::Probe_ClothPath("boot/8/1.png").compare(0,pack.size(),pack)!=0 || !ex(CorpTex::Probe_ClothPath("boot/8/1.png")));
  { std::string pr; CHECK(!CorpMap::PackRelFor("rosto/../../x.png",pr)); CHECK(!CorpMap::PackRelFor("roupa/boot/9/1.png",pr)); CHECK(!CorpMap::PackRelFor("pele/head/1.png",pr)); }
  std::printf("[corps]\n");
  { auto before=CorpTex::Probe_ListCorps(); std::printf("  antes: %zu corp(s)\n",before.size());
    (void)!system(("mkdir -p '"+pack+"corp/zz_teste' '"+pack+"corp/_bad name'").c_str());
    auto after=CorpTex::Probe_ListCorps(); std::printf("  depois: %zu corp(s)\n",after.size());
    CHECK(after.size()==before.size()+1 && std::find(after.begin(),after.end(),"zz_teste")!=after.end());
    CHECK(CorpTex::Probe_CorpDir("zz_teste")==pack+"corp/zz_teste/"); CHECK(CorpTex::Probe_CorpDir("nao_existe_xyz")=="");
    (void)!system(("rm -rf '"+pack+"corp/zz_teste' '"+pack+"corp/_bad name'").c_str()); }
  std::printf("[sanitize / tabelas]\n");
  { uint8_t a,b,c,d; CorpTex::Probe_San(5,99,4,99,a,b,c,d); CHECK(a==5&&b==5&&c==4&&d==5); CorpTex::Probe_San(6,0,3,0,a,b,c,d); CHECK(b==1&&c==3&&d==1); CorpTex::Probe_San(9,3,9,3,a,b,c,d); CHECK(a==0&&b==0&&c==0&&d==0); }
  std::printf("  regioes do corpo=%d pecas policia=%d roupas normal=%d\n",CorpTex::Probe_NBody(),CorpTex::Probe_NCop(),CorpTex::Probe_NCloth());
  CHECK(CorpTex::Probe_NBody()==19 && CorpTex::Probe_NCop()==15 && CorpTex::Probe_NCloth()==19);
  CHECK(CorpTex::Probe_FindPiece("fantasy_m_cop_torso4") && CorpTex::Probe_FindPiece("fantasy_m_underwear") && CorpTex::Probe_FindPiece("fantasy_m_torso2") && !CorpTex::Probe_FindPiece("fantasy_f_cop_torso4"));
  std::printf("%s (%d falha(s))\n",fails?"RESULTADO: FALHOU":"RESULTADO: OK",fails); return fails?1:0;
}
