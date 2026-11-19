// g++ -std=c++20 -O2 -fsanitize=address,undefined -I app/src/main/cpp/samp/game testes/host_test_hairphysics.cpp app/src/main/cpp/samp/game/HairPhysics.cpp -o t && ./t
#include "HairPhysics.h"
#include <cstdio>
#include <cmath>
#include <chrono>
using namespace MxHair;
static int fails=0; static void chk(bool c,const char*m){ if(!c){printf("FALHOU: %s\n",m);fails++;} else printf("ok: %s\n",m); }
static Frame MkFrame(V3 head,float yaw=0,bool body=true){ Frame f; f.head=head; f.up={0,0,1}; f.fwd={-std::sin(yaw),std::cos(yaw),0}; f.right={std::cos(yaw),std::sin(yaw),0}; f.body=body; return f; }
static bool Collides(const Sim& s,const Frame& f,float* worst){
  const Tune&T=GlobalTune(); V3 S=f.head+f.up*T.skullUp; float R=T.skullR+T.skin; V3 A0=f.head-f.fwd*T.bodyBack; float w=1e9f;
  for(int r=1;r<s.Rows();++r) for(int c=0;c<s.Cols();++c){ V3 p=s.Positions()[r*s.Cols()+c]; V3 d=p-S; float dist=std::sqrt(Dot(d,d)); w=std::min(w,dist-R);
    V3 dv=p-A0; float hz=Dot(dv,f.up); if(f.body&&hz<-T.bodyTop&&hz>-T.bodyBottom){ float x=Dot(dv,f.right)/(T.bodyA+T.skin), y=Dot(dv,f.fwd)/(T.bodyB+T.skin); w=std::min(w,(std::sqrt(x*x+y*y)-1.0f)*T.bodyB);} }
  *worst=w; return w<-0.004f; }
static float TipDefl(const Sim& s){ float m=0; for(int c=0;c<s.Cols();++c){ V3 a=s.Positions()[(s.Rows()-1)*s.Cols()+c]-s.Rest()[(s.Rows()-1)*s.Cols()+c]; m+=std::sqrt(a.x*a.x+a.y*a.y);} return m/s.Cols(); }
int main(){
  Shape sh; Sim s;
  chk(!s.Init(Shape{1,9,.3f,250,.1f},1),"Init rejeita cols=1");
  chk(!s.Init(Shape{16,2,.3f,250,.1f},1),"Init rejeita rows=2");
  chk(!s.Init(Shape{40,9,.3f,250,.1f},1),"Init rejeita cols>max");
  chk(!s.Init(Shape{16,9,NAN,250,.1f},1),"Init rejeita comprimento NaN");
  chk(s.Init(sh,7),"Init ok");
  Frame f=MkFrame({100,200,31});
  s.SetFrame(f); s.SetWind(0,0);
  // 1) repouso sem vento
  for(int i=0;i<600;++i){ s.SetFrame(f); s.Update(1/60.f); }
  float w; chk(!Collides(s,f,&w),"sem vento: sem penetracao cabeca/tronco"); printf("   folga min %.1f mm\n",w*1000);
  bool pinned=true; for(int c=0;c<s.Cols();++c){ V3 d=s.Positions()[c]-s.Rest()[c]; if(Dot(d,d)>1e-12f) pinned=false; } chk(pinned,"raiz presa exatamente");
  chk(s.stats.resets==0,"sem vento: 0 resets"); chk(s.stats.maxStretchSeen<1.07f,"estiramento <7%"); printf("   estiramento max %.3f\n",s.stats.maxStretchSeen);
  float tipLow=0; for(int c=0;c<s.Cols();++c) tipLow=std::min(tipLow,s.Positions()[(s.Rows()-1)*s.Cols()+c].z-f.head.z); printf("   ponta mais baixa %.2f m abaixo do osso\n",-tipLow);
  chk(-tipLow>0.15f&&-tipLow<0.95f,"mechas penduradas em altura plausivel");
  // 2) vento
  float d0=TipDefl(s);
  s.SetWind(0.0f,1.0f); float acc=0; int n=0; float maxd=0; for(int i=0;i<1200;++i){ s.SetFrame(f); s.Update(1/60.f); if(i>300){ acc+=TipDefl(s); ++n; maxd=std::max(maxd,TipDefl(s)); } }
  printf("   deflexao media da ponta: sem vento %.3f m | vento 1: media %.3f max %.3f\n",d0,acc/n,maxd);
  chk(acc/n>0.02f&&maxd<0.60f,"vento 1: balanca (2..60 cm) sem explodir"); chk(!Collides(s,f,&w),"vento 1: sem penetracao"); 
  s.SetWind(0.0f,4.0f); for(int i=0;i<1200;++i){ s.SetFrame(f); s.Update(1/60.f); } chk(s.stats.resets==0&&!Collides(s,f,&w),"tempestade (4): estavel"); printf("   estiramento max %.3f\n",s.stats.maxStretchSeen);
  s.SetWind(0,0); for(int i=0;i<900;++i){ s.SetFrame(f); s.Update(1/60.f); } chk(TipDefl(s)<0.08f,"vento volta a 0: assenta"); printf("   defl %.3f\n",TipDefl(s));
  // 3) cabeca andando/balancando + giro
  Sim s2; s2.Init(sh,3); s2.SetWind(1.0f,0.8f); float maxStr=0;
  for(int i=0;i<1800;++i){ float t=i/60.f; Frame g=MkFrame({100+0.35f*std::sin(2*3.14159f*1.6f*t),200+4.0f*t*0.0f+0.1f*t,31+0.03f*std::sin(2*3.14159f*3.2f*t)},0.8f*std::sin(t*1.7f)); s2.SetFrame(g); s2.Update(1/60.f); maxStr=std::max(maxStr,s2.stats.maxStretchSeen); if(i%60==0&&i>120){ float ww; if(Collides(s2,g,&ww)){ chk(false,"penetracao em movimento"); break; } } }
  chk(s2.stats.resets==0,"movimento: 0 resets"); printf("   estiramento max em movimento %.3f\n",maxStr); chk(maxStr<1.25f,"estiramento sob teste extremo (3,5 m/s lateral a 1,6 Hz) <25%");
  { Sim q; q.Init(sh,4); q.SetWind(0.5f,0.6f); float ms=0; for(int i=0;i<1200;++i){ float t=i/60.f; Frame g=MkFrame({6.0f*t,0.0f,31+0.02f*std::sin(2*3.14159f*2.4f*t)},1.5707963f); q.SetFrame(g); q.Update(1/60.f); if(i>60) ms=std::max(ms,q.stats.maxStretchSeen);} printf("   correndo 6 m/s: estiramento max %.3f\n",ms); chk(ms<1.10f&&q.stats.resets==0,"correndo: estiramento <10%, 0 resets"); }
  // teleporte
  Frame far=MkFrame({5000,-3000,20}); s2.SetFrame(far); bool atRest=true; for(int c=0;c<s2.Cols();++c){ V3 d=s2.Positions()[(s2.Rows()-1)*s2.Cols()+c]-s2.Rest()[(s2.Rows()-1)*s2.Cols()+c]; if(Dot(d,d)>1e-8f) atRest=false; } chk(atRest,"teleporte: recomeca do repouso");
  // veiculo (sem tronco)
  Sim s3; s3.Init(sh,5); Frame v=MkFrame({0,0,1},0,false); for(int i=0;i<300;++i){ s3.SetFrame(v); s3.Update(1/60.f);} chk(s3.stats.resets==0,"veiculo (sem tronco): estavel");
  // 4) FPS
  float defl[3]; int fp[3]={20,60,144};
  for(int k=0;k<3;++k){ Sim q; q.Init(sh,7); Frame g=MkFrame({0,0,1}); q.SetFrame(g); q.SetWind(0,1.0f); float a=0; int m=0; for(int i=0;i<fp[k]*20;++i){ q.SetFrame(g); q.Update(1.0f/fp[k]); if(i>fp[k]*5){ a+=TipDefl(q); ++m; } } defl[k]=a/m; }
  printf("   deflexao media 20/60/144 fps: %.3f %.3f %.3f\n",defl[0],defl[1],defl[2]); chk(std::fabs(defl[0]-defl[1])<0.05f&&std::fabs(defl[2]-defl[1])<0.05f,"resultado parecido de 20 a 144 FPS");
  // 5) entradas ruins
  Sim s4; s4.Init(sh,1); s4.SetFrame(f); Frame bad=f; bad.head.x=NAN; s4.SetFrame(bad); s4.Update(NAN); s4.Update(-1); s4.Update(5.0f); s4.SetWind(NAN,INFINITY); s4.SetWind(0,50); for(int i=0;i<200;++i){ s4.SetFrame(f); s4.Update(1/60.f);} 
  bool fin=true; for(int i=0;i<s4.Count();++i){ V3 p=s4.Positions()[i]; if(!std::isfinite(p.x+p.y+p.z)) fin=false; } chk(fin,"NaN/inf/dt ruim: sem NaN nas posicoes");
  // 6) custo
  Sim q; q.Init(Shape{18,10,.4f,250,.18f},9); q.SetFrame(f); q.SetWind(0,1.0f); auto t0=std::chrono::steady_clock::now(); int N=20000; for(int i=0;i<N;++i){ q.SetFrame(f); q.Update(1/60.f);} double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t0).count()/N; printf("   custo %.4f ms/quadro (PC)\n",ms);
   { // dentes (hair6): comprimentos alternam (ponta/vale) e a simulacao continua estavel
    Shape sp; sp.cols=30; sp.rows=10; sp.length=0.40f; sp.spikePeriod=3; sp.spikeDepth=0.5f; Sim q; chk(q.Init(sp,3),"Init dentes");
    Frame f2=MkFrame({10,20,31}); q.SetFrame(f2); q.SetWind(0.7f,1.0f);
    for(int i=0;i<900;++i){ f2.head.x+=0.02f*std::sin(i*0.05f); q.SetFrame(f2); q.Update(1/60.f); }
    float mn=9,mx=0; for(int c=0;c<q.Cols();++c){ V3 a=q.Positions()[(q.Rows()-1)*q.Cols()+c]-q.Positions()[c]; float l=std::sqrt(Dot(a,a)); mn=std::min(mn,l); mx=std::max(mx,l); }
    printf("   comprimento ponta/vale: %.2f .. %.2f m\n",mn,mx);
    chk(mx>1.35f*mn,"dentes: ponta bem mais longa que o vale"); chk(q.stats.resets==0,"dentes: 0 resets"); chk(q.stats.maxStretchSeen<1.08f,"dentes: estiramento <8%");
    { // dentes + corrida (6 m/s) e movimento forte: continua estavel
      Frame f3=MkFrame({0,0,31}); Sim w; w.Init(sp,5); w.SetFrame(f3); w.SetWind(0,0.5f);
      for(int i=0;i<1200;++i){ f3.head.y+=6.0f/60.f; w.SetFrame(f3); w.Update(1/60.f); }
      printf("   dentes correndo: estiramento max %.3f\n",w.stats.maxStretchSeen); chk(w.stats.resets==0&&w.stats.maxStretchSeen<1.12f,"dentes correndo: <12%, 0 resets");
      float mn2=9,mx2=0; for(int c=0;c<w.Cols();++c){ V3 a=w.Positions()[c]-w.Positions()[(w.Rows()-1)*w.Cols()+c]; float l=std::sqrt(Dot(a,a)); mn2=std::min(mn2,l); mx2=std::max(mx2,l);} 
      Sim r0; r0.Init(sp,5); Frame f4=MkFrame({0,0,31}); r0.SetFrame(f4); r0.SetWind(0,0); for(int i=0;i<900;++i){ r0.SetFrame(f4); r0.Update(1/60.f);} 
      float dz=0,mxz=0; for(int c=0;c<r0.Cols();++c){ float d=r0.Positions()[c].z-r0.Positions()[(r0.Rows()-1)*r0.Cols()+c].z; dz+=d; mxz=std::max(mxz,d);} dz/=r0.Cols();
      printf("   dentes parado: queda media %.3f max %.3f (comprimento %.2f)\n",dz,mxz,sp.length); float rmx=0; for(int c=0;c<r0.Cols();++c) rmx=std::max(rmx,r0.Rest()[c].z-r0.Rest()[(r0.Rows()-1)*r0.Cols()+c].z); chk(mxz>0.92f*rmx,"dentes: ponta mais longa cai quase o comprimento todo (laterais nao puxam)");
    }
  }
  printf(fails?"FALHAS %d\n":"TUDO OK\n",fails);
  return fails; }
