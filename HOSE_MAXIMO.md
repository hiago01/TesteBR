# Mangueira (visual, sem colisao) - Maximo RP

## 1. CRope na libGTASA.so deste projeto (arm64, `app/src/main/jniLibs/arm64-v8a/libGTASA.so`)
Enderecos = vaddr (somar a `g_libGTASA`). Achados com `nm -D` e leitura do desassembly (tools_mini_a64.py).

| Simbolo | Offset | Obs |
|---|---|---|
| `CRopes::RegisterRope(uint64 id, uint32 type, CVector pos, bool bExpires, uint8 segCount, bool, CPhysical* holder, int ms)` | 0x4F96D8 | 1232 bytes |
| `CRopes::Init` / `Shutdown` | 0x4F7C0C / 0x4F9564 | |
| `CRopes::Update` / `CRopes::Render` | 0x4F7C54 / 0x4F92F0 | |
| `CRopes::FindRope(uint64 id)` | 0x4FA410 | devolve indice 0..7 ou -1 |
| `CRopes::FindCoorsAlongRope`, `SetSpeedOfTopNode`, `IsCarriedByRope`, `CreateRopeForSwatPed` | 0x4FA0D4, 0x4F9FC4, 0x4FA270, 0x4FA678 | |
| `CRope::Update` / `Render` / `Remove` | 0x4F7D1C / 0x4F93B8 / 0x4F9644 | |
| `CRope::CreateHookObjectForRope` | 0x4F9BA8 | chamada no fim do RegisterRope (bl em 0x4F9848) |
| `CRopes::aRopes` | 0xBD3E50 | 6656 bytes = 8 cordas x 0x340 |
| `RopeIndices` | 0x886370 (arquivo 0x876370) | 64 x uint16: pares (0,1),(1,2)...(31,32) |

Layout de `CRope` (0x340 bytes; semantica inferida do codigo):
`+0x000` CVector no[32] | `+0x180` CVector no_anterior/vel[32] | `+0x300` uint64 id | `+0x30C/+0x310` float (por tipo) |
`+0x318` CPhysical* preso | `+0x320` CObject* gancho | `+0x330` float | `+0x334` uint32 tempo de expirar |
`+0x338` uint8 num. de segmentos | `+0x339` uint8 TIPO (0 = slot livre, 1..7 validos) | `+0x33B` flags.

### Por que NAO usei o CRope
1. **O projeto nao roda o CRope**: `CRopes::Update()` esta comentado em `game.cpp:936` e `CRopes::Render()` em `hooks.cpp:542`.
2. `RegisterRope` (tipos 1..7) **cria um CObject fisico e faz `CWorld::Add`** (CreateHookObjectForRope) -> viola "sem colisao".
3. `CRope::Update` faz `CWorld::FindGroundZFor3DCoord` (colisao com o chao), le CPad/CTouchInterface (controle de guindaste) e simula a corda;
   re-registrar o mesmo id **zera todos os nos** na posicao do topo. Nao da para impor P(t)=lerp-sag.
4. So prende o topo (posicao registrada) e, na outra ponta, um `CPhysical*` (+0x318): nao prende no osso da mao nem na tampa do tanque.
5. `CRope::Render` desenha **linha de 1px** (LINELIST com `RopeIndices`), nao um tubo.
-> Alternativa adotada (mesma logica): tubo continuo desenhado com RwIm3D entre A e B.

## 2. API RW usada (verificada contra a lib)
- `RwIm3DTransform(RxObjSpace3DVertex*, n, RwMatrix* ltm, flags)` = 0x275D54, `RwIm3DRenderIndexedPrimitive` = 0x275E70, `RwIm3DEnd` = 0x275E20 (batem com os simbolos).
- Vertice `RxObjSpace3DVertex` = 0x24 bytes: pos(12) normal(12) color ARGB(4) u v. Primitiva `rwPRIMTYPETRILIST` (3).
- **Atencao:** `RwImVertexIndex` esta como uint32 em `game/RW/rwplcore.h`, mas a propria lib usa **uint16** (RopeIndices). Hose.cpp usa uint16_t + cast.
- Estados: `DefinedState()` (0x6F4EC4, como o CRope::Render) + textura NULL, ZTEST/ZWRITE on, alpha off, cull none.
- Osso da mao: `CPlayerPed::GetBonePosition(24, &v)` (24 = mao direita). Tanque: dummy `DUMMY_GAS_CAP` do modelo, levado ao mundo pela matriz do veiculo.

## 3. Arquivos
Novos: `game/Hose.h`, `game/Hose.cpp`. Alterados: `game/hooks.cpp` (RenderEffects), `gui/samp_widgets/chat.cpp` (/mangueira), `net/scriptrpc.cpp` (solta ao reconectar).

## 4. Uso
- Ponto A (parede): fique junto a parede e digite `/mangueira a` (ou `/mangueira a x y z`). Aparece um marcador azul.
- `/mangueira pegar` liga A -> mao direita; `/mangueira soltar` desliga; `/mangueira limpar` apaga o ponto A.
- Ajustes: `/mangueira config segmentos 10 | sag 0.45 | alcance 12 | raio 0.022 | altura -0.3`, `/mangueira marcador on|off`.
- Solta sozinha: alcance excedido, morte, entrar em veiculo, ped/veiculo invalido.
- Codigo (bomba): `Hose_Start(pedId, bombaPos, alvo)` quando PEGAR o bico, `Hose_Stop()` ao SOLTAR; `Hose_Update()/Hose_Render()` ja rodam todo frame.
  Alvo tanque: `HoseTarget t; t.kind = HoseTarget::VehicleTank; t.vehicleId = idSampDoVeiculo;`

## 5. Compilar
O projeto e Gradle + CMake (GLOB_RECURSE: arquivos novos so entram apos re-sincronizar o CMake). Na pasta do projeto:
`./gradlew clean assembleDebug` (precisa de Android SDK/NDK configurados; no Termux isso depende de voce ter SDK+NDK arm64 ai - me diga como voce compila hoje que eu ajusto os passos).
NAO foi compilado para Android nem testado em aparelho (so checagem de sintaxe no PC).
