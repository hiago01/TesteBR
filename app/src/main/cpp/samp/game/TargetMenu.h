// TargetMenu.h - toque curto num JOGADOR ou ACTOR (boneco do servidor) abre um menu de acoes (SO ADMIN, por enquanto).
// Cabecalho: "<id da conta> [<playerid>]" + "(jogador)"; actor: "NPC [<id do actor>]" + "(npc)". 1a opcao: "Carregar"; depois "Fechar".
// Carregar envia o comando do chat: jogador -> "/carregar <playerid>"; actor -> "/carregar <1004 + id do actor>".
// Se o jogador local ja esta carregando, a opcao e "Soltar" (/soltar). Novas opcoes: BuildOptions() em TargetMenu.cpp.
// Abre/fecha com animacao (escala + fade). RPC 234 (servidor -> cliente): u8 ver(1) | u8 op | ...
//    op 1: u8 admin (0/1)           -> libera/bloqueia o menu neste cliente
//    op 2: u16 playerid | i32 conta -> id da conta daquele jogador (0 = saiu/desconhecido); so e enviado a admins
// Toque: ObserveTouch() so observa (nao consome); HandleRawTouch() captura o dedo que COMECA dentro do menu aberto.
#pragma once
class RakClientInterface;
namespace TargetMenu {
void Register(RakClientInterface* rak);               // RegisterScriptRPCs()
void Unregister(RakClientInterface* rak);             // UnregisterScriptRPCs()
void ObserveTouch(int type, int num, int x, int y);   // hooks.cpp, depois dos controles do HUD (type: 2 push, 3 move, 1 pop)
bool HandleRawTouch(int type, int num, int x, int y); // hooks.cpp, cedo (junto dos outros paineis). true = consumido
void Draw();                                          // dentro do frame do ImGui (ImGuiWrapper::render)
bool IsOpen();
void Close();
}
