// CorpTex.h - texturas por corporacao, POR JOGADOR, para a skin FemininaCorp (DFF do modelo kHostModel)
//
//   PNGs:   <raiz>/SAMP/skins/textures/272/<corp>/<NomeDaTextura>.png   (ex.: 272/pmerj/fantasy_f_cop_torso4.png)
//   Regra:  so os materiais cujo nome tem PNG naquela pasta sao trocados; o resto fica com a textura padrao.
//   Como:   a troca e feita SO durante o desenho do ped (CEntity::Render): poe a textura da corp nos materiais,
//           desenha, devolve a original. O molde do modelo e compartilhado, entao nenhum outro ped muda.
//   Teste:  /corp <nome> | /corp limpar | /corp lista | /corp info   (so no seu proprio ator)
//   Rede:   RPC 231 servidor -> cliente: uint16 playerid | uint8 len | char corp[len]   (len 0 = limpar)
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
