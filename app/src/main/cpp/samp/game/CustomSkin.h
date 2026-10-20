#pragma once

// ---------------------------------------------------------------------------
// Skin customizada (DFF) para o modelo 7 + inventário/texturização/ocultação de materiais
//
//   arquivo:   <raiz>/SAMP/skins/skin.dff          (raiz = Android/data/com.samp.mobile/files/)
//   PNGs:      <raiz>/SAMP/skins/textures/mat_NN.png     (identificação por índice, NN = 00..41)
//              <raiz>/SAMP/skins/textures/<Textura>.png  (textura real, pelo nome gravado no DFF)
//   inventário:<raiz>/SAMP/skins/materials.csv           (gerado por /skin7 mats)
//
// FLUXO DE TEXTURAS NO ANDROID (correção das partes brancas)
//   * O nome do PNG é achado SEM diferenciar maiúscula/minúscula (DFF "Jaqueta" -> jaqueta.png).
//   * O PNG vira RwRaster potência de 2 (lado máx. 1024), SEM flags de mipmap, filtro LINEAR e
//     endereçamento WRAP gravados na textura (antes: NPOT/mip vazio => branco/preto no GLES).
//   * Material texturizado sem PNG e sem textura válida vinda do DFF: tenta o banco de texturas do
//     jogo pelo nome; se não achar, o log mostra "AVISO PNG: ... NAO ENCONTRADA ... ficara branco".
//   * tools/skin_to_android.py converte TXD de PC (D3D8/D3D9) em textures/<Nome>.png e audita o DFF.
//
// COMANDOS (/skin7 ...). Tudo roda na thread do jogo, no próximo frame; veja o resultado com /skin7 info.
//   load | apply | wear | unwear | unload | info
//   mats                    inventário dos materiais: log [SKIN7-MAT] + materials.csv
//   mat list                mostra quais estão ocultos
//   mat off <lista>         oculta materiais     (lista: 5 | 3-7 | 1,4,9-12 | all)
//   mat on  <lista>         mostra de novo
//   mat only <lista>        mostra só estes e oculta o resto
//   mat reset               mostra todos
//   tex auto|idx|none       modo dos PNGs (vale no próximo apply/png):
//                             auto = <Textura>.png se existir, senão mat_NN.png
//                             idx  = só mat_NN.png (identificar partes)   none = não aplica PNG
//   png                     recarrega os PNGs ao vivo no molde instalado (sem reler o DFF)
//
// COMO OS MATERIAIS SÃO OCULTADOS
//   O DFF tem 1 geometria com 42 materiais; os triângulos gravam material 0 e a divisão real
//   está só no BinMesh PLG. Por isso NUNCA se faz RpGeometryLock/Unlock (reconstruiria a malha
//   por material 0). Ocultar = trocar a textura do material por uma textura 8888 transparente
//   (e alfa 0 na cor do material); mostrar = devolver textura e cor originais. O molde é
//   compartilhado pelos clones (geometria/material por referência), então vale ao vivo para
//   quem já está usando o modelo 7. Mantemos uma referência nossa de cada textura em uso, então
//   a troca nunca derruba a contagem a zero (sem textura destruída enquanto o render a usa).
//   Se as partes ocultas aparecerem rosa (magenta), o pipeline ignora o alfa da textura.
//
// Thread: tudo que toca o RenderWare roda na thread do jogo (CGame::Process ->
// ClothesFrameTick -> CCustomSkin::Process). O chat só enfileira comandos.
// (As notas de como a troca do modelo 7 funciona estão em CustomSkin.cpp.)
// ---------------------------------------------------------------------------

#include <string>

class CCustomSkin
{
public:
    static constexpr int  kTargetModelId = 7;      // MODEL_MALE01
    static constexpr bool kAutoApply     = false;   // aplica sozinho (1x por sessão) quando o modelo 7 estiver pronto

    static void RequestLoad();     // qualquer thread
    static void RequestUnload();
    static void RequestApply();
    static void RequestWear(bool on);

    static void Process();         // thread do jogo, 1x por frame

    static std::string Info();     // última mensagem
    static std::string DebugCommand(const std::string& args);   // "/skin7 ..."
};
