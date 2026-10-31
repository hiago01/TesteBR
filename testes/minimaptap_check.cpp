#include <cstdio>
#include "../app/src/main/cpp/samp/gui/samp_widgets/minimaptap_logic.h"
int main(){int f=0;auto T=[&](const char*n,bool c){printf("%s %s\n",c?"PASS":"FAIL",n);if(!c)f++;};
 using namespace MiniMapTap; Rect r{20,20,160,160}; bool open;
 { Tracker t; bool a=t.OnTouch(2,0,80,80,true,r,1000,false,open); bool b=t.OnTouch(1,0,82,81,true,r,1150,false,open); T("toque curto dentro abre",a&&b&&open); }
 { Tracker t; bool a=t.OnTouch(2,0,400,400,true,r,1000,false,open); T("toque fora nao e do minimapa",!a); }
 { Tracker t; t.OnTouch(2,0,80,80,true,r,1000,false,open); t.OnTouch(3,0,90,80,true,r,1100,false,open); t.OnTouch(1,0,200,200,true,r,1200,false,open); T("arrasto nao abre",!open); }
 { Tracker t; t.OnTouch(2,0,80,80,true,r,1000,false,open); t.OnTouch(1,0,80,80,true,r,2000,false,open); T("toque longo nao abre",!open); }
 { Tracker t; t.OnTouch(2,0,80,80,true,r,1000,false,open); t.OnTouch(1,0,80,80,true,r,1100,true,open); T("cancelado nao abre",!open); }
 { Tracker t; bool a=t.OnTouch(2,0,80,80,false,r,1000,false,open); T("retangulo invalido ignora",!a); }
 { Tracker t; t.OnTouch(2,0,80,80,true,r,1000,false,open); bool o=t.OnTouch(2,1,81,81,true,r,1010,false,open); T("2o dedo nao e do minimapa",!o); }
 T("RectOk",RectOk(r,1600,720)&&!RectOk(Rect{5,5,5,5},1600,720)&&!RectOk(Rect{0,0,1500,700},1600,720));
 printf(f?"FALHOU\n":"OK\n");return f;}
