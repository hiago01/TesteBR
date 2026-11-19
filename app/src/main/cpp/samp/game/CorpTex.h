// CorpTex.h - aparencia POR JOGADOR do corpo MALE do pack: corporacao (roupas), rosto, tom de pele, cabelo e roupas
//   Modelo 7   = male normal (skin.dff):   rosto, tom de pele, cabelo e roupas (boot/pants/tshirts) do pack
//   Modelo 271 = male_police.dff (skin271.dff): pecas fantasy_m_cop_* da corp + a MESMA aparencia (corpo do 7; so troca a roupa)
//
//   PNGs:   <raiz>/SAMP/skins/textures/272/<corp>/<NomeDaTextura>.png   OU   <raiz>/SAMP/skins/pack/corp/<corp>/<NomeDaTextura>.png
//           (ex.: pack/corp/policia/fantasy_m_cop_torso4.png)
//   Regra:  materiais cujo nome tem PNG naquela pasta recebem o PNG; pecas de roupa da Corp (fantasy_m_cop_*)
//           SEM PNG naquela pasta ficam ocultas (textura invisivel "male01" do texdb); corpo/cabelo/tatuagem nao mudam.
//   Como:   a troca e feita SO durante o desenho do ped (CEntity::Render): poe a textura da corp nos materiais,
//           desenha, devolve a original. O molde do modelo e compartilhado, entao nenhum outro ped muda.
//   Aparencia: lida de textures/272/aparencia/... ou, se nao existir, DIRETO da pasta pack/ (mapa em CorpTexMap.h):
//           rosto/<tom 1-6>/<var> = pack/heads/<tom>/<var> | cabelo/<estilo 1-4>/<cor> = pack/hair/<estilo>/<cor> |
//           pele/{torso,leg,feet}/<tom> = pack/{torso,leg,feet}/1/<tom> | roupa/boot/1-4 = pack/boot, 5-7 = pack/slipper 1-3 |
//           roupa/pants/<1-7> = pack/pants | roupa/tshirts/<1-4> = pack/tshirts   (pasta -> material em textures/272/aparencia/roupa/mapa.txt, editavel)
//   Teste:  /corp <nome|limpar|lista> | /rosto <tom> <variante> | /pele <tom> | /cabelo <estilo> <cor> | /visual [limpar]
//           /bota /calca /camisa <pasta> <variante> | /intima <0-6> | /roupa lista|limpar|recarregar   (so no seu ator)
//   Rede:   RPC 231 servidor -> cliente: uint16 playerid | uint8 len | char corp[len] | uint8 tom | uint8 rosto | uint8 estilo | uint8 cor |
//           uint8 botaPasta | botaVar | calcaPasta | calcaVar | camisaPasta | camisaVar | intima
//   RPC 232 servidor -> cliente (slots do /skin7, ver LookSlots.h): uint16 playerid | uint8 version=1 | uint8 flags(bit0=limpar) | uint8 n |
//           n x [uint8 slotId | uint8 estado 0/1/2 | se 2: uint16 variante]. 231 e 232 sao guardados separados (um nao apaga o outro).
//           (len 0 e tudo 0 = limpar). Policial: o servidor manda SetPlayerSkin(271) + RPC 231 com corp="policia" e o MESMO
//           tom/rosto/estilo/cor do visual civil do jogador.
//
// Thread: RPC/chat so enfileiram. Tudo que toca o RenderWare roda na thread do jogo (Process + hook de render).
#pragma once
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

typedef unsigned short PLAYERID;   // igual ao typedef de net/netgame.h
class RakClientInterface;

namespace CorpTex {

extern int RPC_CorpTex;                    // = 231 (TODO: confirme que nao colide com RPC do seu servidor)
extern int RPC_CorpSlots;                  // = 232: snapshot dos slots do /skin7 (LookSlots.h); guardado SEPARADO da base 231
extern std::atomic<int> g_active;          // quantos peds estao com corp ativa (fast path do render)

void Register(RakClientInterface* rak);    // RegisterScriptRPCs()
void Unregister(RakClientInterface* rak);
void ClearAll();                           // nova conexao

// Espelho do ULTIMO snapshot (RPC 232) que o servidor mandou para o JOGADOR LOCAL: a autoridade do estado definitivo.
// Thread-safe (escrito pela thread de rede, lido pela thread do jogo). `ver` muda a cada snapshot recebido e na nova conexao.
// Retorna false enquanto o servidor nunca mandou o snapshot desta sessao (o provador nao pode afirmar o que esta salvo).
// out = (id, estado, variante) dos slots COM registro (slot ausente = padrao).
struct LocalSlot { uint8_t id; uint8_t est; uint16_t var; };
bool LocalSlots(std::vector<LocalSlot>& out, uint32_t& ver);
void SetLocalPreview(bool on);              // /provador: true enquanto há prévia (o ped local ignora os slots do servidor)

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
