#pragma once
// ---------------------------------------------------------------------------
// voicebutton_logic.h - regra PURA do botao de microfone (compila e e testada no host).
//   Enquanto o dedo ESTA apertando o icone o microfone fica ativo. Desativa assim que:
//     - o dedo e levantado (em qualquer lugar da tela, inclusive FORA do icone), ou
//     - o dedo e arrastado para FORA do icone (mesmo continuando apertado).
//   Voltar com o dedo para dentro do icone NAO reativa: so um novo toque.
// ---------------------------------------------------------------------------
struct VoiceHold
{
    bool holding = false;

    void OnPush()                  { holding = true; }
    // retorna true se acabou de soltar (o chamador desliga o microfone)
    bool OnMove(bool inside)       { if (holding && !inside) { holding = false; return true; } return false; }
    bool OnPop()                   { if (holding) { holding = false; return true; } return false; }
    void Reset()                   { holding = false; }
};
