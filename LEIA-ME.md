# Crash ao spawnar: CPad::DuckJustDown (SIGSEGV)

Copie `app/` por cima do projeto. Original em `backup_originais/pad.cpp`.

Causa (confirmada no desassembly do libGTASA e no relatorio do jogador, realme RMX3834 / Android 15):
- A falha esta em libGTASA+0x4dece4 = `ldr x8, [x1, #0x150]` dentro de CPad::DuckJustDown(CPed*).
- Endereco da falha 0x82bee5d0 = x1 + 0x150, ou seja x1 = 0x82bee480: ponteiro de 32 bits (TRUNCADO).
- O hook declarava o 2o parametro (CPed*) como `int`. Ao inserir `ModernInput::Active()` antes de chamar o original
  (pacote de controles), o compilador guardou o valor num registrador de 32 bits e o ponteiro chegou cortado.

Correcao (pad.cpp): 2o parametro `int unk` -> `uintptr_t ped` (hook e ponteiro da funcao original).

NAO verificado: compilacao NDK e o teste no aparelho. Latente, NAO alterado: CPad__GetCarGunUpDown_hook usa
`unsigned int a1` para o `this` (mesmo tipo de risco, so em veiculo com arma).
