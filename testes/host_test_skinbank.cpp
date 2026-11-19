// g++ -std=c++20 -I app/src/main/cpp/samp testes/host_test_skinbank.cpp -o t && ./t <pasta com .../skins/>
#include "game/SkinTexDbCore.h"
#include <cstdio>
#include <fstream>
#include <filesystem>
namespace fs=std::filesystem;
static int fails=0; static void chk(bool c,const char*m){ if(!c){printf("FALHOU: %s\n",m);fails++;} else printf("ok: %s\n",m); }
int main(int argc,char**argv){
  std::string dir=argv[1]; if(dir.back()!='/') dir+='/';
  size_t n=0; { std::ifstream f(dir+"skins.idx"); std::string l; while(std::getline(f,l)) if(!l.empty()) ++n; }
  std::string r=SkinTexDbCore::CheckBankFiles(dir,"skins",n); printf("  banco completo: '%s' (%zu texturas)\n",r.c_str(),n); chk(r.empty(),"banco completo aprovado");
  chk(!SkinTexDbCore::CheckBankFiles(dir,"skins",n-1).empty(),"idx com linha a menos reprovado");
  // cenario do crash: .toc/.tmb novos + .dat ANTIGO (truncado)
  std::string t="/tmp/vv/broken/"; fs::remove_all(t); fs::create_directories(t);
  for(auto&e:fs::directory_iterator(dir)) if(e.path().filename()!="skins.etc.dat") fs::copy_file(e.path(),t+e.path().filename().string());
  { std::ifstream in(dir+"skins.etc.dat",std::ios::binary); std::ofstream out(t+"skins.etc.dat",std::ios::binary); std::vector<char> b(1<<20); long long left=259936096; while(left>0&&in){ in.read(b.data(),std::min<long long>(b.size(),left)); auto g=in.gcount(); out.write(b.data(),g); left-=g; } }
  r=SkinTexDbCore::CheckBankFiles(t,"skins",n); printf("  .dat antigo: '%s'\n",r.c_str()); chk(r.find("faltam ")!=std::string::npos && r.find("foi atualizado")!=std::string::npos,"cenario do crash (.dat antigo) detectado com a causa");
  fs::resize_file(t+"skins.etc.tmb",fs::file_size(t+"skins.etc.tmb")-5); fs::copy_file(dir+"skins.etc.dat",t+"skins.etc.dat",fs::copy_options::overwrite_existing);
  r=SkinTexDbCore::CheckBankFiles(t,"skins",n); printf("  tmb truncado: '%s'\n",r.c_str()); chk(!r.empty(),"tmb truncado detectado");
  fs::remove(t+"skins.etc.tmb"); r=SkinTexDbCore::CheckBankFiles(t,"skins",n); chk(!r.empty(),"tmb ausente detectado");
  fs::remove_all(t); chk(!SkinTexDbCore::CheckBankFiles("/tmp/vv/naoexiste/","skins",n).empty(),"pasta ausente detectada");
  printf(fails?"FALHAS %d\n":"TUDO OK\n",fails); return fails; }
