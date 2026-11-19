// g++ -std=c++20 -I app/src/main/cpp/samp testes/host_test_hair5.cpp -o t && ./t   (testa slots/RGBA/protocolo/provador no host)
#include "game/SkinSlots.h"
#include "game/LookSlots.h"
#include "game/ProvadorModel.h"
#include <cstdio>
#include <cassert>
struct BS { std::vector<uint8_t> d; size_t p=0;
  template<class T> bool Read(T& v){ if(p+sizeof(T)>d.size()) return false; memcpy(&v,&d[p],sizeof(T)); p+=sizeof(T); return true; }
  template<class T> void W(T v){ const uint8_t* b=(const uint8_t*)&v; d.insert(d.end(),b,b+sizeof(T)); } };
int fails=0; void chk(bool c,const char*m){ if(!c){printf("FALHOU: %s\n",m);fails++;} else printf("ok: %s\n",m); }
int main(){
  using namespace SkinSlots;
  chk(kCount==64,"kCount==64");
  const Slot& h=kSlots[61];
  chk(std::string(h.name)=="hair5","id62 = hair5");
  chk(IsColorable(h),"hair5 colorivel");
  int ncol=0; for(int i=0;i<kCount;++i) ncol+=IsColorable(kSlots[i]); chk(ncol==2,"so hair5 e hair6 coloriveis (hair7 nao)");
  chk(h.tint==0xFFFFFFFFu,"tint padrao");
  chk(PackArgb(10,20,30,5)==PackArgb(10,20,30,kColorMinA),"A minimo aplicado");
  chk(ArgbA(PackArgb(1,2,3,255))==255 && ArgbR(PackArgb(1,2,3,255))==1,"pack/unpack");
  chk(CleanArgb(0)==0,"clean 0");
  chk(EffectiveArgb(h,0)==h.tint,"efetiva padrao"); chk(EffectiveArgb(kSlots[0],0x11223344)==0,"nao colorivel -> 0");
  // snapshot: hair5 est2 var1 + cor; hair (id1?) sem cor
  BS b; b.W<uint16_t>(7); b.W<uint8_t>(1); b.W<uint8_t>(0); b.W<uint8_t>(2);
  b.W<uint8_t>(62); b.W<uint8_t>(2); b.W<uint16_t>(2);
  b.W<uint8_t>(1); b.W<uint8_t>(1);
  b.W<uint8_t>(2); b.W<uint8_t>(62); b.W<uint32_t>(0xFF102030u); b.W<uint8_t>(1); b.W<uint32_t>(0xFF000000u);
  uint16_t pid; std::vector<LookSlots::St> st; std::string err;
  chk(LookSlots::Parse(b,pid,st,err),"Parse com cores");
  chk(st.size()==2,"2 slots"); 
  const LookSlots::St* s62=nullptr; for(auto&s:st) if(s.id==62) s62=&s;
  chk(s62&&s62->est==2&&s62->var==2&&s62->argb==0xFF102030u,"hair5 var2 argb");
  for(auto&s:st) if(s.id==1) chk(s.argb==0,"slot 1 sem cor (nao colorivel)");
  std::vector<LookSlots::Op> ops; LookSlots::Plan(st,ops);
  bool okop=false; for(auto&o:ops) if(o.slot==62){ okop = o.kind==LookSlots::kPack && o.argb==0xFF102030u && o.rel=="hair/5/2.png"; printf("  rel=%s\n",o.rel.c_str()); } chk(okop,"Plan hair5 pack+argb");
  // sem trailer (servidor antigo)
  BS c; c.W<uint16_t>(7); c.W<uint8_t>(1); c.W<uint8_t>(0); c.W<uint8_t>(1); c.W<uint8_t>(62); c.W<uint8_t>(2); c.W<uint16_t>(1);
  chk(LookSlots::Parse(c,pid,st,err)&&st.size()==1&&st[0].argb==0,"sem trailer = sem cor");
  LookSlots::Plan(st,ops); chk(ops.size()==1&&ops[0].argb==h.tint,"Plan usa tinta padrao");
  // cor com est!=2 ignorada
  BS d; d.W<uint16_t>(7); d.W<uint8_t>(1); d.W<uint8_t>(0); d.W<uint8_t>(1); d.W<uint8_t>(62); d.W<uint8_t>(1);
  d.W<uint8_t>(1); d.W<uint8_t>(62); d.W<uint32_t>(0xFF102030u);
  chk(LookSlots::Parse(d,pid,st,err)&&st[0].argb==0,"cor ignorada com est=1");
  // trailer truncado
  BS e; e.W<uint16_t>(7); e.W<uint8_t>(1); e.W<uint8_t>(0); e.W<uint8_t>(1); e.W<uint8_t>(62); e.W<uint8_t>(2); e.W<uint16_t>(1); e.W<uint8_t>(1); e.W<uint8_t>(62);
  chk(LookSlots::Parse(e,pid,st,err)&&st.size()==1&&st[0].argb==0,"trailer truncado tolerado");
  // slot desconhecido id 200 ignorado, cor id 200 ignorada
  BS f; f.W<uint16_t>(7); f.W<uint8_t>(1); f.W<uint8_t>(0); f.W<uint8_t>(1); f.W<uint8_t>(200); f.W<uint8_t>(1);
  f.W<uint8_t>(1); f.W<uint8_t>(200); f.W<uint32_t>(1);
  chk(LookSlots::Parse(f,pid,st,err)&&st.empty(),"id desconhecido ignorado");
  // exclusao de grupo cabelo
  // ProvadorModel
  using namespace ProvadorModel;
  SrvTable t=NewTable(); Put(t,62,2,1,0xFF102030u);
  chk(t[62].argb==0xFF102030u&&t[62].est==2,"Put guarda argb");
  Put(t,62,1,0,0xFF102030u); chk(t[62].argb==0,"est1 limpa argb");
  Put(t,1,2,1,0xFF102030u); chk(t[1].argb==0,"slot nao colorivel sem argb");
  std::vector<LookSlots::St> sn; sn.push_back(LookSlots::St{62,2,1,0xFF102030u}); sn.push_back(LookSlots::St{1,1,0,0});
  SrvTable t2=FromSnapshot(sn); chk(t2[62].argb==0xFF102030u&&t2[1].argb==0,"FromSnapshot argb");
  // ---- hair6 (cabelo de tecido, 2 cores) ----
  { const Slot& h6=kSlots[62];
    chk(std::string(h6.name)=="hair6" && std::string(h6.texture)=="fantasy_m_hair6" && std::string(h6.folder)=="hair/6","id63 = hair6 (material/pasta)");
    chk(IsColorable(h6)&&IsDualColor(h6)&&!IsDualColor(h),"hair6 2 cores; hair5 so 1");
    chk(h6.tint==0xFF1E1A1Au && h6.tint2==0xFF5A3A2Cu,"hair6 tintas padrao raiz/ponta");
    chk(GroupOf("hair6")==GroupOf("hair5") && GroupOf("hair6")!=0,"hair6 no mesmo grupo exclusivo do cabelo");
    chk(EffectiveArgb2(h6,0)==h6.tint2 && EffectiveArgb2(h6,0x80112233u)==CleanArgb(0x80112233u) && EffectiveArgb2(h,0x80112233u)==0,"EffectiveArgb2");
    BS g; g.W<uint16_t>(7); g.W<uint8_t>(1); g.W<uint8_t>(0); g.W<uint8_t>(1);
    g.W<uint8_t>(63); g.W<uint8_t>(2); g.W<uint16_t>(3);
    g.W<uint8_t>(2); g.W<uint8_t>(63); g.W<uint32_t>(0xFFE95151u); g.W<uint8_t>(63|0x80); g.W<uint32_t>(0xFF2ECC40u);
    uint16_t pid2; std::vector<LookSlots::St> s6; std::string er2;
    chk(LookSlots::Parse(g,pid2,s6,er2)&&s6.size()==1&&s6[0].id==63&&s6[0].var==3,"Parse hair6");
    chk(s6[0].argb==0xFFE95151u && s6[0].argb2==0xFF2ECC40u,"hair6 raiz + ponta (id|0x80)");
    std::vector<LookSlots::Op> o6; LookSlots::Plan(s6,o6); bool ok6=false; for(auto&o:o6) if(o.slot==63) ok6=(o.kind==LookSlots::kPack&&o.rel=="hair/6/3.png"&&o.argb==0xFFE95151u); chk(ok6,"Plan hair6 pack hair/6/3 + cor raiz");
    BS g2; g2.W<uint16_t>(7); g2.W<uint8_t>(1); g2.W<uint8_t>(0); g2.W<uint8_t>(1); g2.W<uint8_t>(62); g2.W<uint8_t>(2); g2.W<uint16_t>(1);
    g2.W<uint8_t>(1); g2.W<uint8_t>(62|0x80); g2.W<uint32_t>(0xFF2ECC40u);
    chk(LookSlots::Parse(g2,pid2,s6,er2)&&s6.size()==1&&s6[0].argb2==0,"ponta ignorada em slot de 1 cor (hair5)");
    BS g3; g3.W<uint16_t>(7); g3.W<uint8_t>(1); g3.W<uint8_t>(0); g3.W<uint8_t>(1); g3.W<uint8_t>(63); g3.W<uint8_t>(1);
    g3.W<uint8_t>(2); g3.W<uint8_t>(63); g3.W<uint32_t>(0xFFE95151u); g3.W<uint8_t>(63|0x80); g3.W<uint32_t>(0xFF2ECC40u);
    chk(LookSlots::Parse(g3,pid2,s6,er2)&&s6.size()==1&&s6[0].argb==0&&s6[0].argb2==0,"cores ignoradas com est=1 (hair6)");
    ProvadorModel::SrvTable t6=ProvadorModel::NewTable(); ProvadorModel::Put(t6,63,2,2,0xFFE95151u,0xFF2ECC40u);
    chk(t6[63].argb==0xFFE95151u&&t6[63].argb2==0xFF2ECC40u&&t6[63].est==2,"Put hair6 guarda as 2 cores");
    ProvadorModel::Put(t6,62,2,1,0xFF102030u,0xFF2ECC40u); chk(t6[62].argb2==0,"hair5 nao guarda 2a cor");
    ProvadorModel::Put(t6,63,1,0,0xFFE95151u,0xFF2ECC40u); chk(t6[63].argb==0&&t6[63].argb2==0,"est1 limpa as 2 cores");
    std::vector<LookSlots::St> sn6; LookSlots::St q{}; q.id=63;q.est=2;q.var=1;q.argb=0xFFE95151u;q.argb2=0xFF2ECC40u; sn6.push_back(q);
    ProvadorModel::SrvTable t7=ProvadorModel::FromSnapshot(sn6); chk(t7[63].argb2==0xFF2ECC40u,"FromSnapshot hair6 2a cor");
  }
  // ---- hair7 (disfarcado carioca, 2 cascas no skin.dff, sem cor) ----
  { const Slot& h7=kSlots[63];
    chk(std::string(h7.name)=="hair7" && std::string(h7.texture)=="fantasy_m_hair7" && std::string(h7.folder)=="hair/07","id64 = hair7 (material/pasta hair/07)");
    chk(!IsColorable(h7)&&!IsDualColor(h7)&&h7.tint==0,"hair7 sem cor RGBA (preto natural na textura)");
    chk(GroupOf("hair7")==GroupOf("hair5") && GroupOf("hair7")==GroupOf("hair6") && GroupOf("hair7")>=0,"hair7 no mesmo grupo exclusivo do cabelo");
    chk(Find("hair7")==&kSlots[63],"Find(hair7)");
    BS g; g.W<uint16_t>(7); g.W<uint8_t>(1); g.W<uint8_t>(0); g.W<uint8_t>(1);
    g.W<uint8_t>(64); g.W<uint8_t>(2); g.W<uint16_t>(3);
    g.W<uint8_t>(1); g.W<uint8_t>(64); g.W<uint32_t>(0xFFE95151u);        // cor para slot sem cor: ignorada
    uint16_t pid2; std::vector<LookSlots::St> s7; std::string er2;
    chk(LookSlots::Parse(g,pid2,s7,er2)&&s7.size()==1&&s7[0].id==64&&s7[0].est==2&&s7[0].var==3&&s7[0].argb==0,"Parse hair7 var3, cor ignorada");
    std::vector<LookSlots::Op> o7; LookSlots::Plan(s7,o7); bool ok7=false; for(auto&o:o7) if(o.slot==64) ok7=(o.kind==LookSlots::kPack&&o.rel=="hair/07/3.png"&&o.argb==0); chk(ok7,"Plan hair7 pack hair/07/3");
    // id|0x80 de slot de 1 cor/sem cor tambem ignorado
    BS g2; g2.W<uint16_t>(7); g2.W<uint8_t>(1); g2.W<uint8_t>(0); g2.W<uint8_t>(1); g2.W<uint8_t>(64); g2.W<uint8_t>(2); g2.W<uint16_t>(1);
    g2.W<uint8_t>(1); g2.W<uint8_t>(64|0x80); g2.W<uint32_t>(0xFF2ECC40u);
    chk(LookSlots::Parse(g2,pid2,s7,er2)&&s7.size()==1&&s7[0].argb2==0,"2a cor ignorada em hair7");
    ProvadorModel::SrvTable t8=ProvadorModel::NewTable(); ProvadorModel::Put(t8,64,2,2,0xFFE95151u,0xFF2ECC40u);
    chk(t8[64].est==2&&t8[64].var==2&&t8[64].argb==0&&t8[64].argb2==0,"Put hair7: sem cores");
    // exclusividade real do servidor/cliente: ligar hair7 nao pode ficar junto de hair5/hair6 (mesmo grupo)
    int g5=GroupOf("hair5"); int n=0; for(int i=0;i<kCount;++i) if(GroupOf(kSlots[i].name)==g5) ++n; chk(n==7,"grupo cabelo = hair1..hair7 (7 slots)");
  }
  printf(fails?"FALHAS %d\n":"TUDO OK\n",fails); return fails; }
