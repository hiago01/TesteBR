# Configurações do launcher: sem "Modified data" + preset gráfico (padrão Mínima)
- Removido "Modified data" da tela (pref MODIFIED_DATA fica sempre false).
- Novo controle "Qualidade gráfica" (Mínima/Baixa/Média/Alta/Ultra), padrão Mínima.
- Fluxo: o launcher grava em SAMP/settings.ini [maximo] preset_request e preset_request_id (KeyboardGuard.check, chamado antes de TODO início de jogo). O cliente (maximomenu.cpp) aplica o preset UMA vez por id (ApplyPreset), salva gta_sa.set e anota preset_done_id.
- Quem já tinha o jogo também recebe Mínima uma vez (id 1). Mudar o controle incrementa o id e reaplica na próxima abertura.
- Copie os arquivos mantendo os caminhos (sobrescreva). Teste PackTest atualizado (svconfig.ini não é mais protegido, como pedido antes).
