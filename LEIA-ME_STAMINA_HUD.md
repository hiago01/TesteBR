# Stamina nativa desligada -> energia da HUD
- hooks.cpp: CPlayerPed::HandleSprintEnergy (jogador local) agora devolve sempre "pode correr" e NAO gasta m_fTimeCanRun; chama Hud::StaminaTick(correndo).
- game/stamina.h (puro): correndo -4/s (100->0 em 25 s); andando/parado +3/s; zerou = cansado ate voltar a 20.
- HUD (circulo energia/raio): Hud::g.energy agora segue essa energia local.
- Servidor (RPC 201): o valor so entra quando MUDA em relação ao último recebido; reenvio igual não desfaz o gasto local. (Se o servidor reenviar energia diferente a cada ciclo, ele vai "mandar" na energia.)
- movemode: sem energia o botao de correr nao entra em SPRINT; se zerar correndo, volta a WALK.
- Ajuste os números em game/stamina.h (kDrain, kRegen, kRecover). /hud energia <0-100> para testar.
- Teste PC: g++ -std=c++17 testes/stamina_check.cpp
NAO verificado: compilacao Android, comportamento no aparelho, nem o significado exato dos argumentos do HandleSprintEnergy (lido do assembly: bool=1 gasta, 0 recupera).
