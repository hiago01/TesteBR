// CorpTex.h - aparencia POR JOGADOR da skin FemininaCorp (DFF do modelo kHostModel): corporacao, rosto, tom de pele, cabelo
//
//   PNGs:   <raiz>/SAMP/skins/textures/272/<corp>/<NomeDaTextura>.png   (ex.: 272/pmerj/fantasy_f_cop_torso4.png)
//   Regra:  materiais cujo nome tem PNG naquela pasta recebem o PNG; pecas de roupa da Corp (fantasy_f_cop_*)
//           SEM PNG naquela pasta ficam ocultas (textura invisivel "male01" do texdb); corpo/cabelo/tatuagem nao mudam.
//   Como:   a troca e feita SO durante o desenho do ped (CEntity::Render): poe a textura da corp nos materiais,
//           desenha, devolve a original. O molde do modelo e compartilhado, entao nenhum outro ped muda.
//   PNGs de aparencia: textures/272/aparencia/rosto/<tom 1-6>/<variante>.png | cabelo/<estilo 1-5>/<cor>.png | pele/{torso,leg,feet}/<tom>.png
//   Teste:  /corp <nome|limpar|lista> | /rosto <tom> <variante> | /pele <tom> | /cabelo <estilo> <cor> | /visual [limpar]   (so no seu ator)
//   Rede:   RPC 231 servidor -> cliente: uint16 playerid | uint8 len | char corp[len] | uint8 tom | uint8 rosto | uint8 estilo | uint8 cor
//           (len 0 e tudo 0 = limpar)
//
// Thread: RPC/chat so enfileiram. Tudo que toca o RenderWare roda na thread do jogo (Process + hook de render).
#pragma once
#include <atomic>
#include <cstdint>
#include <string>

typedef unsigned short PLAYERID;   // igual ao typedef de net/netgame.h
class RakClientInterface;

namespace CorpTex {

extern int RPC_CorpTex;                    // = 231 (TODO: confirme que nao colide com RPC do seu servidor)
extern std::atomic<int> g_active;          // quantos peds estao com corp ativa (fast path do render)

void Register(RakClientInterface* rak);    // RegisterScriptRPCs()
void Unregister(RakClientInterface* rak);
void ClearAll();                           // nova conexao

void OnPlayerStreamIn(PLAYERID id);        // depois de WorldPlayerAdd (o Process tambem reaplica sozinho)
void OnPlayerStreamOut(PLAYERID id);       // ANTES de destruir o ped: solta a corp e o cache do slot

void Process();                            // 1x por frame, thread do jogo (ClothesFrameTick)
std::string DebugCommand(const std::string& args);   // "/corp ..."
std::string VisualCommand(const std::string& cmd, const std::string& args);   // "/rosto", "/cabelo", "/pele", "/visual"

bool BeginRender(void* entity);            // thread do jogo, dentro de CEntity::Render
void EndRender(void* entity);

// Uso no hook: CorpTex::RenderScope scope(pEntity); CEntity_Render(pEntity);
struct RenderScope {
    void* e; bool on;
    explicit RenderScope(void* ent) : e(ent), on(false) {
        if (g_active.load(std::memory_order_relaxed) > 0) on = BeginRender(ent);
    }
    ~RenderScope() { if (on) EndRender(e); }
};

} // namespace CorpTex
