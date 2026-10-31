#include <cstdio>
#include "../app/src/main/cpp/samp/game/stamina.h"
int main(){ int f=0; auto T=[&](const char*n,bool c){printf("%s %s\n",c?"PASS":"FAIL",n); if(!c)f++;};
 Stamina s; for(int i=0;i<250;i++) s.Tick(true,0.1f); // 25 s correndo
 T("zera apos ~25s correndo", s.value<=0.01f && s.tired && !s.CanSprint());
 for(int i=0;i<40;i++) s.Tick(false,0.1f); // 4 s andando -> 12
 T("ainda cansado com 12", s.tired);
 for(int i=0;i<40;i++) s.Tick(false,0.1f); // +12 -> 24
 T("recupera >=20 pode correr", !s.tired && s.value>=20);
 for(int i=0;i<600;i++) s.Tick(false,0.1f); T("regen limita 100", s.value==100.0f);
 Stamina a; a.OnServer(100); for(int i=0;i<100;i++) a.Tick(true,0.1f); float v=a.value; a.OnServer(100);
 T("reenvio igual do servidor nao desfaz o gasto", a.value==v && v<70);
 a.OnServer(50); T("valor novo do servidor entra", a.value==50.0f);
 Stamina b; b.Tick(true,5.0f); T("dt limitado a 0.1s", b.value>99.5f);
 Stamina c; c.OnServer(0); T("servidor 0 = cansado", c.tired);
 printf(f?"FALHOU\n":"OK\n"); return f;}
