#include <cstdint>
#include <cstdio>
#include "../app/src/main/cpp/samp/game/movemode.h"
int main(){
 int fail=0; auto T=[&](const char*n,bool c){printf("%s %s\n",c?"PASS":"FAIL",n); if(!c)fail++;};
 T("walk->sprint",ToggledMode(eMoveMode::WALK)==eMoveMode::SPRINT);
 T("jog->sprint",ToggledMode(eMoveMode::JOG)==eMoveMode::SPRINT);
 T("sprint->walk",ToggledMode(eMoveMode::SPRINT)==eMoveMode::WALK);
 SprintButtonEdge e; long long t=1000; int n=0;
 // segurar 30 frames (16ms): 1 toque so
 for(int i=0;i<30;i++){ n+=e.OnSample(true,t); t+=16; }
 T("segurar = 1 toque",n==1);
 for(int i=0;i<5;i++){ e.OnSample(false,t); t+=16; }
 // chamadas repetidas no mesmo frame
 n=0; n+=e.OnSample(true,t); n+=e.OnSample(true,t); n+=e.OnSample(true,t);
 T("varias chamadas no frame = 1",n==1);
 // tremida: solta e aperta em 50ms -> ignorada (debounce)
 e.OnSample(false,t+20); T("tremida ignorada", e.OnSample(true,t+40)==false);
 e.OnSample(false,t+100); T("novo toque apos 200ms", e.OnSample(true,t+400)==true);
 printf(fail?"FALHOU\n":"OK\n"); return fail;}
