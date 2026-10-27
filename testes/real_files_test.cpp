// g++ -std=c++17 -I../app/src/main/cpp/samp/game -o real real_files_test.cpp && ./real countrye_10.col countrye_10_original_.col
#include <cstdio>
#include <vector>
#include "CustomColParse.h"
static std::vector<uint8_t> Rd(const char* p){ std::vector<uint8_t> v; if(FILE*f=fopen(p,"rb")){ fseek(f,0,SEEK_END); v.resize(ftell(f)); fseek(f,0,SEEK_SET); fread(v.data(),1,v.size(),f); fclose(f);} return v; }
int main(int c,char**a){
  auto cu=Rd(a[1]), og=Rd(a[2]);
  auto rc=CustomColParse::Parse(cu.data(),cu.size()), ro=CustomColParse::Parse(og.data(),og.size());
  char why[200]; 
  bool vc=CustomColParse::ValidateAll(cu.data(),cu.size(),rc,why,sizeof why); printf("custom: %zu entradas status=%s deep=%s %s\n",rc.entries.size(),CustomColParse::StatusStr(rc.status),vc?"OK":"FALHOU",why);
  bool vo=CustomColParse::ValidateAll(og.data(),og.size(),ro,why,sizeof why); printf("orig  : %zu entradas status=%s deep=%s %s\n",ro.entries.size(),CustomColParse::StatusStr(ro.status),vo?"OK":"FALHOU",why);
  printf("SameCollection(custom, buffer original do IMG com padding) = %d\n",CustomColParse::SameCollection(rc,og.data(),og.size()));
  std::vector<uint8_t> other(og.begin()+2200,og.end()); // comeca em outra entrada
  printf("SameCollection(custom, buffer que comeca em outra entrada)  = %d\n",CustomColParse::SameCollection(rc,other.data(),other.size()));
  std::vector<uint8_t> junk(5000,0x41); printf("SameCollection(custom, lixo) = %d\n",CustomColParse::SameCollection(rc,junk.data(),junk.size()));
  // corrupcoes: indice fora do limite
  auto bad=cu; int i=CustomColParse::Find(rc,"CE_brewery"); auto&e=rc.entries[i];
  uint32_t oFac=CustomColParse::Rd32(bad.data()+e.offset+4+96); bad[e.offset+4+oFac]=0xFF; bad[e.offset+4+oFac+1]=0xFF;
  auto rb=CustomColParse::Parse(bad.data(),bad.size()); bool vb=CustomColParse::ValidateAll(bad.data(),bad.size(),rb,why,sizeof why); printf("corrompido (indice 65535): deep=%s | %s\n",vb?"OK(ERRADO!)":"recusado",why);
  auto cut=cu; cut.resize(cut.size()-100); auto rt=CustomColParse::Parse(cut.data(),cut.size()); bool vt=CustomColParse::ValidateAll(cut.data(),cut.size(),rt,why,sizeof why); printf("cortado: deep=%s | %s\n",vt?"OK(ERRADO!)":"recusado",why);
}
