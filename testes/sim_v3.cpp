// simulacao no host da MISTURA v3. g++ -std=c++17 -Istub/samp -Istub -I../app/src/main/cpp/samp/game -Istub/samp/game sim_v3.cpp
#include <vector>
#include <cstring>
#include <cstdlib>
char* g_pszStorage = nullptr;
#include "samp/main.h"
#include "samp/vendor/armhook/patch.h"
#include "samp/game/Models/ModelInfo.h"
static CColModel gCol; static CBaseModelInfo gMi; static const char* gAsked="";
static CBaseModelInfo* FakeFind(const char* n,int* id){ gAsked=n; *id=12931; gMi.m_pColModel=&gCol; return &gMi; }
uintptr_t CHook::getSym(const char* s){ return (uintptr_t)&FakeFind; }
#include "CustomCol.h"
using namespace CustomColParse;
static std::vector<uint8_t> gGot; static int gSlot=-1;
static bool FakeNative(int slot,uint8_t* buf,int size){ gSlot=slot; gGot.assign(buf,buf+size);
  auto r=Parse(buf,size); int i=Find(r,"CE_brewery"); if(i<0) return false; gCol.tris=Rd16(buf+r.entries[i].offset+4+72); return true; }
static std::vector<uint8_t> Rd(const char* p){ std::vector<uint8_t> v; FILE*f=fopen(p,"rb"); fseek(f,0,SEEK_END); v.resize(ftell(f)); fseek(f,0,SEEK_SET); fread(v.data(),1,v.size(),f); fclose(f); return v; }
static int fails=0;
#define CHECK(c,msg) do{ bool ok_=(c); printf("   [%s] %s\n", ok_?"OK":"FALHOU", msg); if(!ok_) fails++; }while(0)
static void reset(){ system("rm -rf /tmp/sroot && mkdir -p /tmp/sroot/SAMP /tmp/sroot/texdb/col /tmp/sroot/SAMP/col"); CustomCol::g_haveChecked=false; CustomCol::g_indexed=false; CustomCol::g_key.clear(); CustomCol::g_files.clear(); CustomCol::g_idx.clear(); CustomCol::g_hadFail=false; gGot.clear(); gCol.tris=0; }
static void run(const std::vector<uint8_t>& in,int slot=7){ CustomCol::g_haveChecked=false; gGot.clear(); gCol.tris=0; CustomCol::LoadColHook(slot,const_cast<uint8_t*>(in.data()),(int)in.size()); }
static const uint8_t* entryBytes(const std::vector<uint8_t>& buf,const char* name,size_t& len){ auto r=Parse(buf.data(),buf.size()); int i=Find(r,name); if(i<0){len=0;return nullptr;} len=8+r.entries[i].size; return buf.data()+r.entries[i].offset; }
int main(){
  static char root[]="/tmp/sroot"; g_pszStorage=root;
  auto orig=Rd("/mnt/user-data/uploads/countrye_10_original_.col");
  CustomCol::g_origLoadCol=&FakeNative;

  puts("### 1) sem arquivos externos -> buffer do IMG intacto"); reset(); run(orig);
  CHECK(gGot==orig,"nativo recebeu EXATAMENTE o buffer do IMG");

  puts("### 2) so brewery_only.col (41 KB) em texdb/col -> MISTURA"); reset(); system("cp /tmp/fx/brewery_only.col /tmp/sroot/texdb/col/");
  run(orig);
  { auto rm=Parse(gGot.data(),gGot.size()), ro=Parse(orig.data(),orig.size());
    CHECK(rm.status==Status::Ok && rm.entries.size()==ro.entries.size(),"buffer misturado e' COL valido com o MESMO numero de entradas (38)");
    CHECK(gCol.tris==3035,"CE_brewery chegou ao loader com 3035 triangulos (novo)");
    int same=0,diff=0; for(auto&e:ro.entries){ size_t la,lb; auto a=entryBytes(orig,e.name,la); auto b=entryBytes(gGot,e.name,lb); if(la==lb&&!memcmp(a,b,la)) same++; else diff++; }
    CHECK(same==37&&diff==1,"as outras 37 entradas sao BYTE A BYTE as do IMG; so 1 mudou");
    CHECK(gSlot==7,"slot repassado sem alteracao"); }
  CHECK(CustomCol::Exists("/tmp/sroot/SAMP/col_index.txt"),"indice gravado em SAMP/col_index.txt");

  puts("### 3) duas entradas em OUTRO arquivo + brewery -> 3 trocadas"); system("cp /tmp/fx/two.col /tmp/sroot/SAMP/col/"); run(orig);
  { auto rm=Parse(gGot.data(),gGot.size()); CHECK(rm.status==Status::Ok&&rm.entries.size()==38,"continua valido com 38 entradas");
    size_t la,lb; auto a=entryBytes(gGot,"cunte_roads11",la); auto n=Rd("/mnt/user-data/uploads/countrye_10.col"); auto b=entryBytes(n,"cunte_roads11",lb); CHECK(la==lb&&!memcmp(a,b,la),"cunte_roads11 veio do arquivo externo"); }

  puts("### 4) mesmo nome em 2 arquivos: o ULTIMO (ordem alfabetica) vence"); reset();
  system("cp /tmp/fx/brewery_only.col /tmp/sroot/texdb/col/a_novo.col; cp /tmp/fx/brewery_orig.col /tmp/sroot/texdb/col/z_orig.col"); run(orig);
  CHECK(gCol.tris==490,"z_orig.col (490 tri) venceu a_novo.col (3035 tri)");

  puts("### 5) .col de OUTRA colecao (sem nomes no indice) -> intacto"); reset(); system("cp /tmp/fx/brewery_only.col /tmp/sroot/texdb/col/");
  std::vector<uint8_t> other(orig.begin()+17492,orig.begin()+19664); run(other); CHECK(gGot==other,"buffer entregue sem copia/alteracao");

  puts("### 6) arquivo com entrada corrompida -> entrada ignorada, IMG segue"); reset(); system("cp /tmp/fx/bad.col /tmp/sroot/texdb/col/"); run(orig);
  CHECK(gGot==orig,"nativo recebeu o buffer original");

  puts("### 7) corrompida + boa no mesmo diretorio -> usa so a boa"); system("cp /tmp/fx/two.col /tmp/sroot/texdb/col/"); run(orig);
  { size_t la; auto a=entryBytes(gGot,"cunte_roads11",la); auto n=Rd("/mnt/user-data/uploads/countrye_10.col"); size_t lb; auto b=entryBytes(n,"cunte_roads11",lb); CHECK(la==lb&&!memcmp(a,b,la),"cunte_roads11 trocada"); CHECK(gCol.tris==490,"CE_brewery ficou ORIGINAL (a entrada ruim foi recusada)"); }

  puts("### 8) INDICE EM CACHE: segundo boot nao le os .col"); reset(); system("cp /tmp/fx/brewery_only.col /tmp/sroot/texdb/col/"); run(orig);
  CustomCol::g_idx.clear(); CustomCol::g_files.clear(); CustomCol::g_indexed=false; CustomCol::g_key.clear(); run(orig);
  CHECK(CustomCol::g_fromCache,"indice veio do cache"); CHECK(gCol.tris==3035,"mistura funciona usando o indice do cache");
  system("touch -d '2001-01-01' /tmp/sroot/texdb/col/brewery_only.col"); CustomCol::g_indexed=false; CustomCol::g_key.clear(); run(orig);
  CHECK(!CustomCol::g_fromCache,"mtime mudou -> cache descartado e reindexado");

  puts("### 9) legado: countrye_10.col inteiro em texdb/ (como no v2)"); reset(); system("cp /mnt/user-data/uploads/countrye_10.col /tmp/sroot/texdb/"); run(orig);
  CHECK(gCol.tris==3035,"funciona como antes"); { auto r=Parse(gGot.data(),gGot.size()); CHECK(r.status==Status::Ok&&r.entries.size()==38,"38 entradas validas"); }

  puts("### 10) arquivo editado depois de indexado -> aborta com seguranca e reindexa"); reset(); system("cp /tmp/fx/brewery_only.col /tmp/sroot/texdb/col/x.col"); run(orig);
  system("cp /tmp/fx/brewery_orig.col /tmp/sroot/texdb/col/x.col"); CustomCol::g_haveChecked=true; /* sem reindexar ainda */ gGot.clear(); CustomCol::LoadColHook(7,const_cast<uint8_t*>(orig.data()),(int)orig.size());
  CHECK(gGot==orig,"nesta chamada entregou o IMG (nao usou dados inconsistentes)"); run(orig); CHECK(gCol.tris==490,"na chamada seguinte ja usa o arquivo novo");

  puts("### 11) col_off.on"); reset(); system("cp /tmp/fx/brewery_only.col /tmp/sroot/texdb/col/; touch /tmp/sroot/SAMP/col_off.on"); run(orig); CHECK(gGot==orig,"desligado: IMG intacto");

  puts("### 12) sobra nao zerada depois da ultima entrada -> aborta"); reset(); system("cp /tmp/fx/brewery_only.col /tmp/sroot/texdb/col/");
  auto dirty=orig; dirty[dirty.size()-5]=0x77; run(dirty); CHECK(gGot==dirty,"entregou o buffer original");
  printf("\n==== %s (%d falha(s)) ====\n", fails?"FALHOU":"TUDO OK", fails); return fails?1:0;
}
