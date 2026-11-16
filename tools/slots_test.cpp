#include "../app/src/main/cpp/samp/gui/samp_widgets/controlmode_logic.h"
#include <cstdio>
using namespace CtlLogic;
int fails=0; void chk(bool c,const char*m){ if(!c){printf("FALHOU: %s\n",m);fails++;} else printf("ok: %s\n",m); }
int main(){
  ModernController c; c.SetScreen(1920,1080); c.ApplyConfig(c.cfg);
  const CtlCircle& b=c.lay.btn[B_SLOTS];
  float sx=b.cx, sy=b.cy;                       // meio da barra
  float ax=b.cx, ay=SlotsArrowCy(b);
  // modo 2 (aberta): toque no slot 3 (indice 2)
  c.slotsMode=2; c.OnTouch(2,1,sx,sy); 
  chk(c.slotPressed==2,"aberta: toque no centro = slot 3"); c.OnTouch(1,1,sx,sy);
  // modo 0: sem toque
  c.slotsMode=0; c.slotPressed=-1; c.OnTouch(2,2,sx,sy); chk(c.slotPressed==-1,"oculta: barra nao recebe toque");
  c.OnTouch(1,2,sx,sy);
  // modo 1: toque no meio da barra (fora da seta) nao pega; na seta -> -2
  c.slotsMode=1; c.slotPressed=-1; c.OnTouch(2,3,b.cx-b.r*4,sy); chk(c.slotPressed==-1,"seta: toque longe da seta nao pega"); c.OnTouch(1,3,b.cx-b.r*4,sy);
  c.OnTouch(2,4,ax,ay); chk(c.slotPressed==-2,"seta: toque na seta = pedido de abrir (-2)"); c.OnTouch(1,4,ax,ay);
  printf(fails?"FALHAS\n":"TUDO OK\n"); return fails;
}
