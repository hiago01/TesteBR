// simulacao no host do caminho completo: hook LoadCol -> troca de buffer -> conferencia
#include <vector>
#include <cstring>
char* g_pszStorage = nullptr;
#include "samp/main.h"
#include "samp/vendor/armhook/patch.h"
#include "samp/game/Models/ModelInfo.h"
static CColModel gCol; static CBaseModelInfo gMi;
static CBaseModelInfo* FakeFind(const char* n,int* id){ *id=12931; gMi.m_pColModel=&gCol; return &gMi; }
uintptr_t CHook::getSym(const char* s){ return (uintptr_t)&FakeFind; }
#include "CustomCol.h"
static const uint8_t* gSeen=nullptr; static int gSeenSize=0; static int gSlot=-1;
static bool FakeNative(int slot,uint8_t* buf,int size){ gSlot=slot; gSeen=buf; gSeenSize=size;
  // o "loader nativo" simulado: acha CE_brewery e grava o numero de triangulos que leu
  auto r=CustomColParse::Parse(buf,size); int i=CustomColParse::Find(r,"CE_brewery"); if(i<0) return false;
  gCol.tris=CustomColParse::Rd16(buf+r.entries[i].offset+4+72); return true; }
static std::vector<uint8_t> Rd(const char* p){ std::vector<uint8_t> v; FILE*f=fopen(p,"rb"); fseek(f,0,SEEK_END); v.resize(ftell(f)); fseek(f,0,SEEK_SET); fread(v.data(),1,v.size(),f); fclose(f); return v; }
int main(){
  system("rm -rf /tmp/sroot && mkdir -p /tmp/sroot/SAMP/col /tmp/sroot/texdb /tmp/sroot/texdb");
  static char root[]="/tmp/sroot"; g_pszStorage=root;
  auto orig=Rd("/mnt/user-data/uploads/countrye_10_original_.col");
  CustomCol::g_origLoadCol=&FakeNative;
  puts("\n### 1) sem arquivo externo: deve usar o buffer do IMG");
  gCol.tris=0; CustomCol::LoadColHook(7,orig.data(),(int)orig.size());
  printf("-> nativo recebeu %s, triangulos=%u\n", gSeen==orig.data()?"BUFFER DO IMG":"OUTRO", gCol.tris);
  puts("\n### 2) com arquivo externo valido");
  system("cp /mnt/user-data/uploads/countrye_10.col /tmp/sroot/SAMP/col/"); CustomCol::g_haveChecked=false;
  gCol.tris=0; CustomCol::LoadColHook(7,orig.data(),(int)orig.size());
  printf("-> nativo recebeu %s (%d bytes) slot=%d triangulos=%u\n", gSeen==orig.data()?"BUFFER DO IMG":"COPIA DO ARQUIVO EXTERNO", gSeenSize, gSlot, gCol.tris);
  puts("\n### 3) outro .col do jogo (nao deve trocar)");
  std::vector<uint8_t> other(orig.begin()+17492,orig.begin()+19664); CustomCol::g_haveChecked=false;
  CustomCol::LoadColHook(9,other.data(),(int)other.size()); printf("-> nativo recebeu %s\n", gSeen==other.data()?"BUFFER ORIGINAL (correto)":"TROCADO (ERRO)");
  puts("\n### 4) arquivo externo corrompido: deve recusar e manter o IMG");
  auto bad=Rd("/mnt/user-data/uploads/countrye_10.col"); bad.resize(bad.size()-77); FILE*f=fopen("/tmp/sroot/SAMP/col/countrye_10.col","wb"); fwrite(bad.data(),1,bad.size(),f); fclose(f);
  CustomCol::g_haveChecked=false; gCol.tris=0; CustomCol::LoadColHook(7,orig.data(),(int)orig.size());
  printf("-> nativo recebeu %s\n", gSeen==orig.data()?"BUFFER DO IMG (correto)":"TROCADO (ERRO)");
  puts("\n### 4b) pasta 1 ilegivel (chmod 000) -> deve cair na pasta 3 (texdb)");
  system("cp /mnt/user-data/uploads/countrye_10.col /tmp/sroot/SAMP/col/ && chmod 000 /tmp/sroot/SAMP/col/countrye_10.col && cp /mnt/user-data/uploads/countrye_10.col /tmp/sroot/texdb/");
  CustomCol::g_haveChecked=false; gCol.tris=0; CustomCol::LoadColHook(7,orig.data(),(int)orig.size());
  printf("-> nativo recebeu %s\n", gSeen==orig.data()?"BUFFER DO IMG":"COPIA DO ARQUIVO EXTERNO");
  system("chmod 644 /tmp/sroot/SAMP/col/countrye_10.col; rm -f /tmp/sroot/texdb/countrye_10.col");
  puts("\n### 5) col_off.on");
  system("cp /mnt/user-data/uploads/countrye_10.col /tmp/sroot/SAMP/col/; touch /tmp/sroot/SAMP/col_off.on"); CustomCol::g_haveChecked=false;
  CustomCol::LoadColHook(7,orig.data(),(int)orig.size()); printf("-> nativo recebeu %s\n", gSeen==orig.data()?"BUFFER DO IMG (correto)":"TROCADO (ERRO)");
}
// (teste extra 6/7 abaixo e' chamado por main2)
