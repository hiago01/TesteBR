# Aba CONTROLES do menu Máximo
Substitua app/src/main/cpp/samp/gui/samp_widgets/maximomenu.cpp (já inclui os presets gráficos e o draw distance).
Itens (mesma ordem da tela nativa): Layout dos botões, Direção por toque, Acelerômetro, Inverter olhar, Direção analógica da moto, Sensibilidade analógica (20..45), Subir automaticamente, Restaurar padrões.
Salvam em gta_sa.set (Menu_SaveSettings) ao fechar o menu.
Verificado: índices/faixas no assembly; CPad::GetAutoClimb e GetSteeringMode (moto) leem a tabela a cada chamada; CTouchInterface::Update reaplica a direção por toque a cada frame; layout chama SetupLayoutObjects como o menu nativo; lógica de faixa/restaurar em teste host.
NÃO verificado: compilação Android e funcionamento no aparelho (nomes dos valores de Acelerômetro/Direção por toque são suposição de ordem 0,1,2).
