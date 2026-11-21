# Avatar por jogador - CLIENTE (Maximo RP)

Toque no circulo do personagem (tela principal do inventario) -> Galeria do Android -> foto recortada em quadrado
128x128 JPEG -> enviada ao servidor em pedacos (RPC 252) -> servidor grava <ID da conta>.jpg e devolve (RPC 251).
O ID mostrado abaixo do nome agora e o ID DA CONTA (campo id do .ini), vindo do servidor (antes era o slot local).

Arquivos novos : gui/samp_widgets/avatar.h, avatar.cpp, avatar_logic.h
Arquivos alterados (a partir dos originais): SAMP.java, jniutil.h/.cpp, main.cpp, SAMPRPC.h/.cpp, net/scriptrpc.cpp,
gui/samp_widgets/inventory.cpp (base = Cliente-Inventario-Telas.zip, que ja deve estar aplicado).
Precisa do servidor com maximo_avatar.inc (zip Servidor-Avatar). CMake usa GLOB_RECURSE: rode build limpo.
NAO testado em aparelho nem compilado com o NDK (so verificacao de sintaxe com clang e teste da logica no PC).
