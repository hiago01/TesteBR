// TargetMenu.h - toque curto num JOGADOR ou ACTOR (boneco do servidor) abre um menu de acoes. 1a opcao: "Carregar".
// Carregar envia o comando do chat: jogador -> "/carregar <id>"; actor -> "/carregar <1004 + id do actor>" (mesmo protocolo do servidor).
// Se o jogador local ja esta carregando, a unica opcao e "Soltar" (/soltar). Novas opcoes: acrescentar em BuildOptions() (TargetMenu.cpp).
// Toque: ObserveTouch() so observa (nao consome); HandleRawTouch() captura o dedo que COMECA dentro do menu aberto.
#pragma once
namespace TargetMenu {
void ObserveTouch(int type, int num, int x, int y);   // hooks.cpp, depois dos controles do HUD (type: 2 push, 3 move, 1 pop)
bool HandleRawTouch(int type, int num, int x, int y); // hooks.cpp, cedo (junto dos outros paineis). true = consumido
void Draw();                                          // dentro do frame do ImGui (ImGuiWrapper::render)
bool IsOpen();
void Close();
}
