// CustomAnimTest.h - comando de chat /ifp para testar IFP customizado no proprio cliente (sem servidor/plugin).
#pragma once
#include <string>

namespace CustomAnimTest {

// args = tudo depois de "/ifp ". Retorna UMA linha para addDebugMessage.
//   /ifp ajuda
//   /ifp carregar [arquivo] [bloco]   (padrao: <files>/SAMP/anims/teste.ifp  bloco "catest")
//   /ifp lista
//   /ifp tocar <anim> [loop] [hold] [parcial]
//   /ifp parar
//   /ifp cruzar | /ifp apontar [esq]   (atalhos locais do bloco "poses": bracos_cruzados, apontar_dir, apontar_esq)
//   /ifp remoto <idDoJogador> <anim|limpar> [loop]   (simula o RPC do servidor num jogador em stream)
std::string Command(const std::string& args);

// Carrega <files>/SAMP/anims/poses.ifp como bloco "poses" (cruzar bracos / apontar) se ainda nao estiver carregado.
// Chamado automaticamente a cada conexao (scriptrpc.cpp). Retorna true se o bloco esta disponivel.
bool AutoLoad();

} // namespace CustomAnimTest
