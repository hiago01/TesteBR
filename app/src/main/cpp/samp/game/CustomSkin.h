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
// PACK DE TEXTURAS POR SLOT (variantes de roupa; veja SkinSlots.h e tools_skin_pack.py)
//   pasta no aparelho:  <raiz>/SAMP/skins/pack/<pasta>/<n>.png   (o cliente só lê PNG: converta os .jpeg)
//   pack                    confere o pack: quantos PNG e quantos JPEG (JPEG não carrega) por pasta
//   slots                   slot -> material(is) do DFF -> pasta do pack; grava SAMP/skins/slots.csv
//   slot <nome> <n>         aplica a variante n (pasta do slot) ao vivo e deixa o slot visível
//   slot <nome> + | -       próxima / anterior variante existente (percorre o pack)
//   slot <nome> on | off    mostra / oculta o slot
//   slot <nome> pasta/arq   caminho explícito (marcas: versace/tshirts/2_1)
//   save [default] [wear]   grava o estado atual (texturas + ocultos) em SAMP/skins/autoexec.txt; "default" grava o homem base (o jogo cria o arquivo)
//   autoexec                roda o autoexec.txt agora (também roda sozinho após o auto-apply)
//   set <materiais> <pasta/arq>   teste livre por índice de material (ex.: set 25 tshirts/1/34)
//   Slots com evidência 0 em SkinSlots.h são HIPÓTESES (pants/shorts, heads, sobrancelha, barba, pele).
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

#include <cstdint>
#include <string>
#include <vector>

class CCustomSkin
{
public:
    static constexpr int  kTargetModelId = 7;      // MODEL_MALE01
    static constexpr bool kAutoApply     = false;   // false: o visual por jogador vem do CorpTex (render). true = aplica o molde global /skin7 sozinho (1x por sessão) + autoexec.txt

    static void RequestLoad();     // qualquer thread
    static void RequestUnload();
    static void RequestApply();
    static void RequestWear(bool on);

    static void Process();         // thread do jogo, 1x por frame

    static std::string Info();     // última mensagem
    static std::string DebugCommand(const std::string& args);   // "/skin7 ..."

    // ---- API da interface /provador (gui/samp_widgets/provador.cpp) -------------------------------------
    // Tudo thread-safe: a UI só ENFILEIRA pedidos (UiRequest) e LÊ uma cópia do estado real (UiGetState);
    // quem mexe nos materiais é sempre a thread do jogo (Process). Os pedidos reaproveitam o "/skin7 slot"
    // (DoSlot: texturas, exclusividade por categoria) e acrescentam só prévia + confirmar + cancelar:
    //   UiRequest("prev <slot> <n|on|off>")  aplica ao vivo como PRÉVIA (guarda antes um backup com refs de textura)
    //   UiRequest("commit")                  confirma a prévia (solta o backup)
    //   UiRequest("cancel")                  desfaz tudo o que foi feito desde o início da prévia
    //   UiRequest("refresh")                 só republica o estado
    struct UiSlot
    {
        int              index       = -1;     // posição em SkinSlots::kSlots
        bool             hasMaterial = false;  // o DFF carregado tem o(s) material(is) do slot
        bool             visible     = false;  // todos os materiais do slot visíveis (estado real, s_hidden)
        bool             partial     = false;  // só parte dos materiais visível (ex.: pele das pernas sob uma bermuda)
        int              variant     = -1;     // variante do pack aplicada (-1 = textura original do DFF / caminho avulso)
        std::vector<int> variants;             // variantes existentes em pack/<pasta>/
    };
    struct UiState
    {
        bool                skinReady = false; // molde instalado (/skin7 apply): sem isso nada é aplicado
        bool                pending   = false; // há prévia não confirmada
        uint32_t            reqId     = 0;     // último pedido da UI já processado
        bool                reqOk     = true;  // ... e se funcionou (verificado no estado real, não pelo texto)
        std::string         msg;               // motivo quando reqOk == false
        std::vector<UiSlot> slots;             // um por SkinSlots::kSlots, na mesma ordem
    };
    static void     UiSetActive(bool on);               // UI aberta: o estado passa a ser republicado a cada comando
    static uint32_t UiRequest(const std::string& op);   // devolve o id do pedido (aparece em UiState::reqId)
    static uint32_t UiVersion();                        // muda a cada republicação (evita copiar sem necessidade)
    static bool     UiGetState(UiState& out);           // false enquanto nunca foi publicado
};
