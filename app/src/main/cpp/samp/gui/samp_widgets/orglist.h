#pragma once
// ---------------------------------------------------------------------------
//  /orglista (Maximo RP): lista de TODAS as organizacoes ativas, em paginas, com brasao (texdb/brasao, textura org_<tag>) e o nome dos lideres.
//   servidor -> cliente (RPC 245) op 56: u8 pagina | u8 paginas | u16 total | u8 n | n x { str tag | str nome | str local | str lideres | u16 membros }
//   cliente -> servidor (RPC 246) op 56 + u8 pagina (pede outra pagina) | op 57 (fechou)      (org.pwn: OrgL_Request)
// ---------------------------------------------------------------------------
#include <cstdint>

class ImGuiRenderer;
struct ImVec2;
namespace RakNet { class BitStream; }

namespace OrgList
{
	bool OnPacket(RakNet::BitStream& bs);        // op 56 (apos ler o op): abre/atualiza
	void Reset();
	bool Active();
	void Close(bool notifyServer);
	void Draw(ImGuiRenderer* r, const ImVec2& scr);
	void OnTouch(int type, int x, int y);        // 2 = baixou, 3 = moveu, 1 = soltou
}
