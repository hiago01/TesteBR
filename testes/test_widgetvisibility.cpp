#include "WidgetVisibility.h"
#include <cstdio>
static int fails=0;
#define CHECK(c) do{ if(!(c)){ printf("FALHOU: %s (linha %d)\n",#c,__LINE__); fails++; } }while(0)
int main(){
  using namespace WidgetVis;
  // a pe: botoes de veiculo somem, os de pe ficam
  CHECK(ShouldHide(ID_HORN,false,false)); CHECK(ShouldHide(ID_STEER_LEFT,false,false)); CHECK(ShouldHide(ID_ACCELERATE,false,false));
  CHECK(ShouldHide(ID_BRAKE,false,false)); CHECK(ShouldHide(ID_HANDBRAKE,false,false)); CHECK(ShouldHide(ID_CAM_TOGGLE,false,false));
  CHECK(!ShouldHide(ID_ATTACK,false,false)); CHECK(!ShouldHide(ID_SPRINT,false,false)); CHECK(!ShouldHide(ID_BUTTON_SPRINT,false,false));
  // em veiculo: botoes de pe somem, os de veiculo ficam
  CHECK(ShouldHide(ID_ATTACK,true,false)); CHECK(ShouldHide(ID_SPRINT,true,false)); CHECK(ShouldHide(ID_BUTTON_SPRINT,true,false));
  CHECK(ShouldHide(ID_BUTTON_CROUCH,true,false));
  CHECK(!ShouldHide(ID_HORN,true,false)); CHECK(!ShouldHide(ID_ACCELERATE,true,false)); CHECK(!ShouldHide(ID_STEER_RIGHT,true,false));
  // jetpack: pe-only some, veiculo-only tambem nao aparece... acelerar/frear do jetpack: nao escondidos
  CHECK(ShouldHide(ID_ATTACK,false,true)); CHECK(!ShouldHide(ID_ACCELERATE,false,true)); CHECK(!ShouldHide(ID_BRAKE,false,true));
  // ids desconhecidos / -1 nunca sao escondidos (minijogos, menus, radar, joystick...)
  for(int id=0; id<0xBE; id++){ if(FootOnly(id)&&VehicleOnly(id)){ printf("id %d nos dois\n",id); fails++; } }
  CHECK(!ShouldHide(-1,true,false)); CHECK(!ShouldHide(0x00 /*ENTER_CAR*/,false,false)); CHECK(!ShouldHide(0x71 /*slider*/,true,false));
  CHECK(!ShouldHide(0xA7 /*PED_MOVE*/,true,false)); CHECK(!ShouldHide(0xA1 /*RADAR*/,false,false));
  printf(fails?"%d FALHAS\n":"todos os testes passaram\n",fails); return fails?1:0; }
