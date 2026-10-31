# O ponteiro do botão de correr ainda enchia? (análise do assembly)
ANTES desta correção: NÃO. 
- CWidgetButtonSprint::Update (libGTASA 0x37620c) calcula: enchimento = clamp(1 - (m_fTimeCanRun + c)/(max + c), 0, 1) e grava em widget+0xb8. m_fTimeCanRun = playerData(ped+0x540)+0x24; max = CStats::GetFatAndMuscleModifier(7).
- Quem mexia no m_fTimeCanRun era CPlayerPed::HandleSprintEnergy, que eu tinha desligado (retorna sempre true sem gastar). Resultado: m_fTimeCanRun ficava parado no máximo => enchimento 0 => o ponteiro nunca enchia.
AGORA: o hook espelha a energia da HUD no m_fTimeCanRun (100% = max => ponteiro vazio; 0% = 0 => ponteiro cheio), a cada tick, então o ponteiro volta a encher conforme a energia cai.
NÃO verificado: compilação e visual no aparelho (o ponteiro e o círculo da HUD devem andar juntos); se outras rotinas do jogo leem m_fTimeCanRun (não analisei todas).

# Arquivos incluídos
- game/hooks.cpp (acumulado: stamina->HUD+ponteiro, botão de pulo, toque no minimapa, RETs de roupa) — se o seu hooks.cpp tiver outras mudanças, aplique só a função CPlayerPed__HandleSprintEnergy_hook e o #include <dlfcn.h>.
- gui/gui.h e gui/gui.cpp (registro do JumpButton; corrige o erro "no member named 'jumpbutton' in 'UI'").
- gui/samp_widgets/hud.cpp e hudstate.h (StaminaTick agora devolve a energia).
