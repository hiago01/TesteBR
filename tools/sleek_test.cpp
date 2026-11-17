#include "../app/src/main/cpp/samp/game/sleek_pose_logic.h"
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

  // ===== pose =====
  { PoseConfig pc;
    // rotacao local-por-mundo: eixos continuam ortonormais; rotacao nula = identidade; pai girado
    Basis P; P.r={0,1,0}; P.u={-1,0,0}; P.a={0,0,1};   // pai girado 90 graus
    V3 cr{1,0,0}, cu{0,1,0}, ca{0,0,1}; RotateLocalByWorld(cr,cu,ca,P,V3{0,0,1},0.0f);
    CHECK(std::fabs(cr.x-1)<1e-5&&std::fabs(cu.y-1)<1e-5);
    RotateLocalByWorld(cr,cu,ca,P,V3{0,1,0},0.4f);
    CHECK(std::fabs(Dot(cr,cu))<1e-5 && std::fabs(Len(cr)-1)<1e-5 && std::fabs(Len(ca)-1)<1e-5);
    // a rotacao em torno do eixo MUNDO y inclina o 'at' local (mundo z) para +x do mundo (roll + = direita)
    Basis I; V3 r{1,0,0},u{0,1,0},a{0,0,1}; RotateLocalByWorld(r,u,a,I,V3{0,1,0},0.5f); CHECK(a.x>0.4f && a.z>0.8f);
    // arco
    V3 ax; float ang1; Arc({1,0,0},{0,1,0},ax,ang1); CHECK(std::fabs(ang1-kPi/2)<1e-4 && ax.z>0.99f);
    // lean: virar a esquerda inclina para a esquerda (roll<0), acelerar inclina para frente (pitch<0), tudo limitado; volta a zero
    Lean l; LeanIn in; in.active=true; in.fwdSpeed=1; in.angVel=3.0f; in.accel=1; LeanOut o; for(int i=0;i<200;i++) o=l.Update(pc,in,dt);
    CHECK(o.roll<-0.01f && std::fabs(o.roll)<=pc.maxLean*kDeg2Rad+1e-4 && o.pitch<-0.01f && std::fabs(o.pitch)<=pc.maxAccelPitch*kDeg2Rad+1e-4);
    in.active=false; for(int i=0;i<300;i++) o=l.Update(pc,in,dt); CHECK(std::fabs(o.roll)<1e-3&&std::fabs(o.pitch)<1e-3);
    // head: limita yaw, some ao olhar para tras, reparte entre torso/pescoco/cabeca
    HeadTrack h; HeadIn hi; hi.active=true; hi.yawRel=120*kDeg2Rad; HeadOut ho; for(int i=0;i<300;i++) ho=h.Update(pc,hi,dt);
    CHECK(std::fabs(ho.headYaw)+std::fabs(ho.neckYaw)+std::fabs(ho.torsoYaw)<0.01f);                // atras: sem efeito
    hi.yawRel=60*kDeg2Rad; for(int i=0;i<300;i++) ho=h.Update(pc,hi,dt);
    float tot=ho.torsoYaw+ho.neckYaw+ho.headYaw; CHECK(std::fabs(tot-60*kDeg2Rad)<0.02f && ho.headYaw>ho.neckYaw && ho.neckYaw>ho.torsoYaw);
    hi.yawRel=170*kDeg2Rad*0+80*kDeg2Rad; hi.moving=true; for(int i=0;i<300;i++) ho=h.Update(pc,hi,dt);
    CHECK(std::fabs(ho.torsoYaw+ho.neckYaw+ho.headYaw)<=pc.maxYaw*kDeg2Rad*pc.movingScale+0.02f);   // limite * escala ao andar
    // IK: perna esticada, joelho novo mantem comprimentos e o pe alcanca alvo alcancavel
    V3 T{0,0,1}, K{0,0.1f,0.55f}, F{0,0,0.1f}; V3 tg{0,0,0.2f}; V3 K2=TwoBoneKnee(T,K,F,tg);
    CHECK(std::fabs(Len(K2-T)-Len(K-T))<1e-3 && std::fabs(Len(F-K)-Len(tg-K2))<1e-2);
    V3 far{0,0,-5}; V3 K3=TwoBoneKnee(T,K,F,far); CHECK(std::fabs(Len(K3-T)-Len(K-T))<1e-3);        // alvo impossivel: sem NaN
    // foot raw: chao plano = sem correcao; degrau para cima = sobe; pe no ar = sem correcao; normal ingreme = ignora
    FootProbe pr; pr.hit=true; pr.groundZ=9.0f; pr.normal={0,0,1};
    FootTarget ft=FootRaw(pc,pr,10.0f-1.0f+0.11f,10.0f); CHECK(std::fabs(ft.dz+0.0f)<0.02f+1.0f && ft.w>0.9f);   // groundZ 9 = chao assumido
    pr.groundZ=9.2f; ft=FootRaw(pc,pr,9.11f,10.0f); CHECK(ft.dz>0.15f && ft.dz<=pc.maxStepUp+1e-4);
    ft=FootRaw(pc,pr,9.8f,10.0f); CHECK(ft.w<0.05f && std::fabs(ft.dz)<0.05f);
    pr.normal={0.8f,0,0.6f}; ft=FootRaw(pc,pr,9.11f,10.0f); CHECK(ft.dz==0 && ft.w==0);
    FootState fs; for(int i=0;i<120;i++) FootSmooth(pc,fs,FootTarget{0.2f,1,{0,0,1}},dt); CHECK(std::fabs(fs.dz-0.2f)<0.01f && fs.weight>0.99f);
    // camera de ombro: ancora segue com atraso maximo respeitado
    CamConfig cc; CamState cs; V3 R{1,0,0}, Fw{0,1,0}; V3 an=FollowAnchor(cc,cs,{0,0,0},R,Fw,dt); CHECK(Len(an)<1e-6);
    an=FollowAnchor(cc,cs,{3.0f,0,0},R,Fw,dt); CHECK(std::fabs(3.0f-an.x)<=cc.maxLatLag+1e-4);
    for(int i=0;i<200;i++) an=FollowAnchor(cc,cs,{3.0f,0,0},R,Fw,dt); CHECK(std::fabs(3.0f-an.x)<0.01f);
  }
  printf(fails? "%d FALHAS\n":"sleek_logic: TODOS OS TESTES OK\n",fails); return fails!=0; }
