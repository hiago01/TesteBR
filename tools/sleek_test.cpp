#include "../app/src/main/cpp/samp/game/sleek_logic.h"
#include <cstdio>
using namespace Sleek;
static int fails=0;
#define CHECK(c) do{ if(!(c)){ printf("FALHOU: %s (linha %d)\n",#c,__LINE__); fails++; } }while(0)
int main(){
  Config c; const float dt=1/60.f;
  // FOV: sprint sobe ate ~30, pitch para cima soma, total limitado, saida volta a 0
  { FovCtl f; FovIn in; in.active=true; in.stick=1; in.sprint=true; float d=0; for(int i=0;i<300;i++) d=f.Update(c,in,dt);
    CHECK(d>29.0f && d<=30.01f);
    in.pitchDeg=60; for(int i=0;i<300;i++) d=f.Update(c,in,dt); CHECK(d>44.0f && d<=45.0f);   // 30+15 = 45 = maximo
    in.active=false; for(int i=0;i<120;i++) d=f.Update(c,in,dt); CHECK(d<0.5f);
    FovCtl g; FovIn w; w.active=true; w.stick=0.25f; for(int i=0;i<200;i++) d=g.Update(c,w,dt); CHECK(d<0.5f); // andar devagar: sem FOV
    c.fovScale=0.5f; FovCtl h; FovIn s; s.active=true;s.stick=1;s.sprint=true; for(int i=0;i<300;i++) d=h.Update(c,s,dt); CHECK(d>14.0f&&d<15.1f); c.fovScale=1; }
  // matriz: ortonormal depois de girar
  { V3 r{1,0,0}, f{0,1,0}, u{0,0,1}; RotateBasis(r,f,u,0.1f,0.2f,0.3f);
    CHECK(std::fabs(Dot(r,f))<1e-5 && std::fabs(Dot(r,u))<1e-5 && std::fabs(Dot(f,u))<1e-5);
    CHECK(std::fabs(Dot(f,f)-1)<1e-5); V3 r2{1,0,0},f2{0,1,0},u2{0,0,1}; RotateBasis(r2,f2,u2,0,0,0); CHECK(f2.y>0.9999f); }
  // idle: amplitude limitada e some quando inativo
  { IdleShake s; float mx=0; for(int i=0;i<3000;i++){ auto o=s.Update(c,true,false,dt); mx=std::max(mx,std::fabs(o.pitch)/kDeg2Rad);} 
    CHECK(mx>0.3f && mx<=c.idlePitchDeg*c.idleIntensity*1.01f);
    float last=1; for(int i=0;i<300;i++){ auto o=s.Update(c,false,false,dt); last=std::fabs(o.yaw);} CHECK(last<1e-4f); }
  // impacto
  { CHECK(ShakeStrength(c,0,0,100)==0); CHECK(ShakeStrength(c,1.0f,1000,1000)>0.99f); CHECK(ShakeStrength(c,1.0f,1000,7000)==0);
    ImpactShake s; float mx=0; for(int i=0;i<120;i++){ auto o=s.Update(c,1.0f,dt); mx=std::max(mx,std::fabs(o.roll)/kDeg2Rad);} CHECK(mx>0.2f&&mx<c.shakeRotDeg*c.shakeIntensity*2);
    float e=1; for(int i=0;i<600;i++){ auto o=s.Update(c,0,dt); e=std::fabs(o.roll);} CHECK(e<1e-3f); }
  // momentum: acelera gradualmente, freia, inverte passando por baixo velocidade, nunca passa de 1
  { Momentum m; float peak=0; m.Update(c,0,1,true,dt); CHECK(m.Speed()<0.2f);                     // nao salta para o maximo
    for(int i=0;i<120;i++){ m.Update(c,0,1,true,dt); peak=std::max(peak,m.Speed()); } CHECK(m.Speed()>0.99f&&peak<=1.0001f);
    for(int i=0;i<6;i++) m.Update(c,0,0,false,dt); CHECK(m.Speed()>0.3f&&m.Speed()<0.95f);       // inercia ao soltar
    for(int i=0;i<30;i++) m.Update(c,0,0,false,dt); CHECK(m.Speed()==0);                         // para por completo
    Momentum r; for(int i=0;i<120;i++) r.Update(c,0,1,true,dt);                                   // inversao: vai para tras
    int steps=0; float minsp=9; while(steps<200){ r.Update(c,0,-1,true,dt); steps++; minsp=std::min(minsp,r.Speed()); if(r.vy<-0.5f) break; }
    CHECK(r.vy<-0.5f && minsp<=c.reversalSwitch+0.05f);
    Momentum t; for(int i=0;i<120;i++) t.Update(c,0,1,true,dt); t.Update(c,1,0,true,dt);          // giro de 90 graus: nao instantaneo em corrida
    CHECK(t.vy>0.3f); }
  printf(fails? "%d FALHAS\n":"sleek_logic: TODOS OS TESTES OK\n",fails); return fails!=0; }
