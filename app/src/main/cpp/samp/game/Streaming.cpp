//
// Created by x1y2z on 11.01.2023.
//

#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#include <strings.h>
#include <string>
#include <cstdio>
#include "Diag.h"
#include "../crashreport.h"
#include "Streaming.h"
#include "../vendor/armhook/patch.h"
#include "StreamingInfo.h"
#include "game/Models/ModelInfo.h"
#include "game/Animation/AnimManager.h"
#include "TxdStore.h"
#include "game.h"
#include "net/netgame.h"
#include "game/Collision/ColStore.h"
#include "IplStore.h"
#include "Camera.h"
#include "Renderer.h"
#include "CustomImg.h"
#include "CustomCol.h"
#include "CustomIpl.h"
#include <algorithm>
#include <functional>
#include <vector>
#include <iostream>

extern UI *pUI;

bool CStreaming::TryLoadModel(int modelId) {
    FLog("TryLoadModel %d", modelId);
    if (modelId >= 612) CustomImg::LogModelState(modelId, "TryLoadModel");
    if(!CStreaming::GetInfo(modelId).IsLoaded()) {
        if (!CStreaming::GetInfo(modelId).HasCdPosnAndSize()) {
            // O LoadCdDirectory nao ligou este ID a nenhum IMG. Plano B: se o nome do modelo existe no touch.img,
            // liga na mao (causa tipica: cache posicional MODELS\MINFO.BIN velho, ja contornado em InitImageList).
            if (modelId >= 612 && Diag::On("touch") && CustomImg::AttachFromTouch(modelId))
                CustomImg::LogModelState(modelId, "TryLoadModel/apos-plano-B");
            else {
                // ID sem entrada em nenhum IMG: nada sera lido do disco, nao adianta esperar
                FLog("[TOUCH] modelo %d sem entrada em nenhum IMG (m_nCdSize=0) e o nome tambem nao existe no touch.img: confira o nome do .dff no touch.img e o ID/nome no vehicles.ide (veja as linhas [TOUCH][AUDIT])", modelId);
                return false;
            }
        }
        FLog("TryLoadModel 1");
        CStreaming::RequestModel(modelId, STREAMING_GAME_REQUIRED | STREAMING_KEEP_IN_MEMORY);
        FLog("TryLoadModel 11");
        CStreaming::LoadAllRequestedModels(false);
        FLog("TryLoadModel 2");
        uint32 count = 0;
        while (!CStreaming::GetInfo(modelId).IsLoaded()) {
            count++;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            if (count > 30) {
                pUI->chat()->addDebugMessage("{ff0000} Erro ao carregar o modelo %d", modelId);
                return false;
            }
        }
        FLog("TryLoadModel 3");
    }
    FLog("TryLoadModel 4");
    return true;
}

// Finishes loading all channels. (So both channels will be `IDLE` after it returns)
// Blocking. (Calls `CdStreamSync`)
void CStreaming::FlushChannels()
{
    CHook::CallFunction<void>(g_libGTASA + 0x396D1C);
}

void CStreaming::ClearFlagForAll(uint32 streamingFlag) {
    for (auto i = 0; i < RESOURCE_ID_TOTAL; i++) {
        GetInfo(i).ClearFlags(streamingFlag);
    }
}

bool CStreaming::AreAnimsUsedByRequestedModels(int32 animModelId) {
    for (auto info = ms_pStartRequestedList->GetNext(); info != ms_pEndRequestedList; info = info->GetNext()) {
        const auto modelId = GetModelFromInfo(info);
        if (IsModelDFF(modelId) && CModelInfo::GetModelInfo(modelId)->GetAnimFileIndex() == animModelId)
            return true;
    }

    for (auto & channel : ms_channel) {
        for (const auto& modelId : channel.modelIds) {
            if (modelId != MODEL_INVALID && IsModelDFF(modelId) &&
                CModelInfo::GetModelInfo(modelId)->GetAnimFileIndex() == animModelId
                    ) {
                return true;
            }
        }
    }

    return false;
}

void CStreaming::RemoveTxdModel(int32 modelId) {
    RemoveModel(TXDToModelId(modelId));
}

void CStreaming::RemoveModelIfNoRefs(int32 modelId) {
    CStreamingInfo& streamingInfo = GetInfo(modelId);
    if(streamingInfo.IsLoaded() && !CModelInfo::GetModelInfo(modelId)->m_nRefCount) {
        RemoveModel(modelId);

        streamingInfo.ClearAllFlags();
    }
}

void CStreaming::SetModelIsDeletable(int32 modelId) {
    CHook::CallFunction<void>(g_libGTASA + 0x399090, modelId);
}

void CStreaming::RemoveModel(int32 modelId) {
    if(modelId == MODEL_MALE01)
        return;

    CStreamingInfo& streamingInfo = GetInfo(modelId);
    if (streamingInfo.m_nLoadState == LOADSTATE_NOT_LOADED)
        return;

    if (streamingInfo.IsLoaded()) {
        switch (GetModelType(modelId)) {
            case eModelType::DFF: {
                CBaseModelInfo* modelInfo = CModelInfo::GetModelInfo(modelId);
                modelInfo->DeleteRwObject();
                break;
            }
            case eModelType::TXD: {
                CTxdStore::RemoveTxd(ModelIdToTXD(modelId));
                break;
            }
            case eModelType::COL: {
                CColStore::RemoveCol(ModelIdToCOL(modelId));
                break;
            }
            case eModelType::IPL: {
                CIplStore::RemoveIpl(ModelIdToIPL(modelId));
                break;
            }
            case eModelType::DAT: {
              //  ThePaths.UnLoadPathFindData(ModelIdToDAT(modelId));
                break;
            }
            case eModelType::IFP: {
                CAnimManager::RemoveAnimBlock(ModelIdToIFP(modelId));
                break;
            }
            case eModelType::SCM: {
             //   CTheScripts::StreamedScripts.RemoveStreamedScriptFromMemory(ModelIdToSCM(modelId));
                break;
            }
        }
        ms_memoryUsed -= STREAMING_SECTOR_SIZE * streamingInfo.GetCdSize();
    }

    if (streamingInfo.InList()) {
        if (streamingInfo.IsRequested()) {
            ms_numModelsRequested--;
            if (streamingInfo.IsPriorityRequest()) {
                streamingInfo.ClearFlags(STREAMING_PRIORITY_REQUEST);
                ms_numPriorityRequests--;
            }
        }
        streamingInfo.RemoveFromList();
    } else if (streamingInfo.IsBeingRead()) {
        for (auto& ch : ms_channel) {
            for (auto& mId : ch.modelIds) {
                if (mId == modelId) {
                    mId = MODEL_INVALID;
                }
            }
        }
    }

    if (streamingInfo.IsLoadingFinishing()) {
        switch (GetModelType(modelId)) {
            case eModelType::DFF:
                RpClumpGtaCancelStream();
                break;
            case eModelType::TXD:
                CTxdStore::RemoveTxd(ModelIdToTXD(modelId));
                break;
            case eModelType::COL:
                CColStore::RemoveCol(ModelIdToCOL(modelId));
                break;
            case eModelType::IPL:
                CIplStore::RemoveIpl(ModelIdToIPL(modelId));
                break;
            case eModelType::IFP:
                CAnimManager::RemoveAnimBlock(ModelIdToIFP(modelId));
                break;
            case eModelType::SCM:
             //   CTheScripts::StreamedScripts.RemoveStreamedScriptFromMemory(ModelIdToSCM(modelId));
                break;
        }
    }

    streamingInfo.m_nLoadState = LOADSTATE_NOT_LOADED;
}

void CStreaming::RemoveBigBuildings() {
    /*for (auto i = GetBuildingPool()->GetSize() - 1; i >= 0; i--) {
        CBuilding* building = GetBuildingPool()->GetAt(i);
        if (building && building->m_bIsBIGBuilding && !building->m_bImBeingRendered) {
            building->DeleteRwObject();
            if (!CModelInfo::GetModelInfo(building->m_nModelIndex)->m_nRefCount)
                RemoveModel(building->m_nModelIndex);
        }
    }*/
}

void CStreaming::RemoveBuildingsNotInArea(eAreaCodes areaCode) {
    CHook::CallFunction<void>(g_libGTASA + 0x3979AC, areaCode);

}


// ---------------------------------------------------------------------------------------------------------------------------
// GUARDA do CdStreamRead (diagnostico + protecao). A CdStreamThread le o IMG por gCdFiles[offset>>24]; quando esse arquivo nao
// esta aberto (ponteiro NULL) ela chama OS_FileSetPosition(NULL) e o jogo cai com SIGSEGV "Fault address 0x8" (samp_log de
// 2026-10-31: libGTASA+0x31BF44 <- CdStreamThread+0x...). Aqui a leitura e recusada do jeito que o proprio jogo trata uma
// falha de leitura (status 0xFE, 0 setores) e o log diz QUAL IMG/posicao pediu. Enderecos conferidos no desassembly do
// libGTASA 2.10 arm64: gCdFiles = +0x8EAA40 (32 ponteiros), ponteiro dos canais = +0x8EAA18 (0x30 bytes cada; +4 setores, +0x14 status).
// ---------------------------------------------------------------------------------------------------------------------------

// DIAGNOSTICO (samp_log 2026-10-31): modelos 7 (male01), 280 (lapd1) e 596 (copcarla) pedidos SEM entrada no IMG. Le o diretorio do
// texdb/gta3.img direto do disco e diz se esses nomes existem, quantas entradas ha e o tamanho de cada IMG da pasta texdb/.
static void DumpImgDirectoryDiag()
{
    if (!g_pszStorage || !g_pszStorage[0]) return;
    const std::string root(g_pszStorage);
    if (DIR* d = opendir((root + "texdb").c_str())) {
        while (dirent* e = readdir(d)) {
            if (e->d_name[0] == '.') continue;
            struct stat st {};
            const std::string fp = root + "texdb/" + e->d_name;
            if (stat(fp.c_str(), &st) == 0 && S_ISREG(st.st_mode))
                FLog("[IMGDIAG] texdb/%s = %lld bytes", e->d_name, (long long)st.st_size);
        }
        closedir(d);
    } else FLog("[IMGDIAG] pasta texdb/ nao abre (errno=%d)", errno);
    const char* names[] = { "texdb/gta3.img", "texdb/GTA3.IMG", "texdb/Gta3.img" };
    for (const char* rel : names) {
        FILE* f = fopen((root + rel).c_str(), "rb");
        if (!f) continue;
        char magic[4]; uint32_t cnt = 0;
        if (fread(magic, 1, 4, f) != 4 || fread(&cnt, 4, 1, f) != 1) { FLog("[IMGDIAG] %s: cabecalho ilegivel", rel); fclose(f); return; }
        FLog("[IMGDIAG] %s: magic=%.4s entradas=%u", rel, magic, cnt);
        if (memcmp(magic, "VER2", 4) != 0 || cnt == 0 || cnt > 200000) { fclose(f); return; }
        const char* want[] = { "male01", "lapd1", "copcarla", "arrow", "colt45", "nitestick", "teargas", "infernus", "sfpd1" };
        const int NW = (int)(sizeof(want) / sizeof(want[0]));
        bool found[16] = {};
        for (uint32_t i = 0; i < cnt; i++) {
            struct { uint32_t off; uint16_t sz1; uint16_t sz2; char name[24]; } en;
            if (fread(&en, 1, sizeof(en), f) != sizeof(en)) { FLog("[IMGDIAG] diretorio truncado na entrada %u", i); break; }
            en.name[23] = 0;
            for (int k = 0; k < NW; k++) {
                if (strncasecmp(en.name, want[k], strlen(want[k])) == 0 && (en.name[strlen(want[k])] == '.' || en.name[strlen(want[k])] == 0)) {
                    found[k] = true;
                    FLog("[IMGDIAG] achou '%s' offset=%u setores=%u/%u", en.name, en.off, (unsigned)en.sz1, (unsigned)en.sz2);
                }
            }
        }
        for (int k = 0; k < NW; k++) if (!found[k]) FLog("[IMGDIAG] '%s' NAO existe no diretorio de %s", want[k], rel);
        fclose(f);
        return;
    }
    FLog("[IMGDIAG] nenhum texdb/gta3.img encontrado");
}

static int (*CdStreamRead_orig)(int, void*, uint32_t, uint32_t) = nullptr;
static int CdStreamRead_guard(int channel, void* buf, uint32_t offset, uint32_t sectors)
{
    const uint32_t arch = offset >> 24;
    void* file = (arch < 32) ? *reinterpret_cast<void**>(g_libGTASA + 0x8EAA40 + arch * 8) : nullptr;
    if (!file)
    {
        static int s_logged = 0;
        if (s_logged++ < 30)
        {
            const char* nm = (arch < TOTAL_IMG_ARCHIVES && CStreaming::ms_files[arch].m_szName[0]) ? CStreaming::ms_files[arch].m_szName : "?";
            FLog("[CDSTREAM] LEITURA SEM ARQUIVO (so registra): IMG #%u ('%s') sem arquivo aberto | canal=%d posn=0x%X setores=%u",
                 arch, nm, channel, offset & 0xFFFFFF, sectors);
        }
        // NAO recusa mais a leitura: devolver erro fazia o jogo repetir para sempre (travava no Splash, samp_log 2026-10-31).
        // Agora so registra (log + migalha do relatorio de crash) e deixa seguir; a protecao de verdade e StreamInfoBad na origem.
        {   // identifica QUEM tem info corrompida: varre a tabela e lista os modelos com IMG invalido / tamanho absurdo
            static int s_scans = 0;
            if (s_scans++ < 2) {
                int bad = 0;
                for (int32 m = 0; m < (int32)RESOURCE_ID_TOTAL; m++) {
                    const CStreamingInfo& q = CStreaming::ms_aInfoForModel[m];
                    // CAUSA PROVAVEL (logs 3x de 2026-10-31): modelo PEDIDO/lendo mas SEM entrada no IMG (cdSize==0) -> o nativo
                    // usa posn/setores nao inicializados da pilha (varia a cada execucao = ponteiro). Lista esses modelos.
                    const bool noImgEntry = q.m_nCdSize == 0 && (q.m_nLoadState == LOADSTATE_REQUESTED || q.m_nLoadState == LOADSTATE_READING);
                    if (noImgEntry || (q.m_nCdSize != 0 && (q.m_nImgId >= TOTAL_IMG_ARCHIVES || q.m_nCdSize > 0x40000 || q.m_nLoadState == LOADSTATE_READING))) {
                        if (bad++ < 40)
                            FLog("[STREAM][BAD] varredura: modelId=%d imgId=%u cdPosn=0x%X cdSize=%u loadState=%d", (int)m,
                                 (unsigned)q.m_nImgId, (unsigned)q.m_nCdPosn, (unsigned)q.m_nCdSize, (int)q.m_nLoadState);
                    }
                }
                if (bad > 0) DumpImgDirectoryDiag();
                FLog("[STREAM][BAD] varredura: %d modelos suspeitos (pedido SEM entrada no IMG, imgId>=%d, tamanho>0x40000 ou READING)", bad, (int)TOTAL_IMG_ARCHIVES);
                CrashReport::Mark("scan-bad", bad, (int)arch);
            }
        }
        FLog("[CDSTREAM] chamador nativo: libGTASA+0x%llX", (unsigned long long)((uintptr_t)__builtin_return_address(0) - g_libGTASA));
        CrashReport::Mark("cdread-nofile", (int)arch, (int)(offset & 0xFFFFFF));
    }
    return CdStreamRead_orig(channel, buf, offset, sectors);
}


// ---------------------------------------------------------------------------------------------------------------------------
// CONTORNO "SEMPRE ABRE": modelo PEDIDO sem entrada em nenhum IMG (cdSize==0) fazia o RequestModelStream NATIVO ler posn/setores
// nao inicializados e derrubar o jogo (SIGSEGV 0x8; logs de 2026-10-31: modelos 7 male01, 280 lapd1, 596 copcarla).
// Antes de cada RequestModelStream, os modelos pedidos sem entrada recebem os dados de um modelo "doador" valido do mesmo tipo
// (skin -> outra skin, veiculo -> outro veiculo): o jogo abre e o modelo aparece com a aparencia do doador. Outros tipos sao
// retirados da fila (ficam sem o modelo, mas sem derrubar). Cada caso vai para o samp_log como [FALLBACK].
// ---------------------------------------------------------------------------------------------------------------------------
static void (*RequestModelStream_orig)(int) = nullptr;

static int32 FindDonor(ModelInfoType wanted, int32 from, int32 to)
{
    for (int32 id = from; id <= to; id++) {
        CBaseModelInfo* mi = CModelInfo::GetModelInfo(id);
        if (!mi || mi->GetModelType() != wanted) continue;
        const CStreamingInfo& d = CStreaming::ms_aInfoForModel[id];
        if (d.m_nCdSize == 0 || d.m_nCdSize > 0x40000 || d.m_nImgId >= TOTAL_IMG_ARCHIVES) continue;
        return id;
    }
    return -1;
}

static void SanitizeRequested()
{
    if (!CStreaming::ms_pStartRequestedList || !CStreaming::ms_pEndRequestedList) return;
    int32 bad[32]; int nBad = 0;
    for (auto info = CStreaming::ms_pStartRequestedList->GetNext(); info && info != CStreaming::ms_pEndRequestedList; info = info->GetNext()) {
        if (info->m_nCdSize != 0) continue;
        const int32 id = (int32)(info - CStreaming::ms_aInfoForModel);
        if (id < 0 || id >= (int32)RESOURCE_ID_TOTAL) continue;
        if (nBad < 32) bad[nBad++] = id;
    }
    static int32 s_donor[16];
    static bool s_donorInit = false;
    if (!s_donorInit) { for (auto& v : s_donor) v = -2; s_donorInit = true; }
    static bool s_dumped = false;
    if (nBad > 0 && !s_dumped) { s_dumped = true; DumpImgDirectoryDiag(); }
    static int s_logged = 0;
    for (int n = 0; n < nBad; n++) {
        const int32 id = bad[n];
        CStreamingInfo& inf = CStreaming::ms_aInfoForModel[id];
        CBaseModelInfo* mi = (id < 20000) ? CModelInfo::GetModelInfo(id) : nullptr;
        int32 donor = -1;
        if (mi) {
            // doador do MESMO tipo (skin, veiculo, arma, objeto...). Skin/veiculo procuram na faixa propria; os demais em toda a faixa de DFF.
            const int t = (int)mi->GetModelType() & 15;
            if (s_donor[t] == -2) {
                const ModelInfoType mt = mi->GetModelType();
                s_donor[t] = (mt == ModelInfoType::MODEL_INFO_PED)     ? FindDonor(mt, 1, 311)
                           : (mt == ModelInfoType::MODEL_INFO_VEHICLE) ? FindDonor(mt, 400, 611)
                           : FindDonor(mt, 0, 19999);
            }
            donor = s_donor[t];
        }
        if (donor >= 0) {
            const CStreamingInfo& d = CStreaming::ms_aInfoForModel[donor];
            inf.m_nImgId = d.m_nImgId; inf.m_nCdPosn = d.m_nCdPosn; inf.m_nCdSize = d.m_nCdSize; inf.m_nNextIndexOnCd = -1;
            CrashReport::Mark("fallback", id, donor);
            if (s_logged++ < 40) FLog("[FALLBACK] modelo %d sem entrada no IMG -> usando os dados do modelo %d (mesmo tipo); jogo segue", (int)id, (int)donor);
        } else {
            CrashReport::Mark("fallback-rm", id, 0);
            if (s_logged++ < 40) FLog("[FALLBACK] modelo %d sem entrada no IMG e sem doador do mesmo tipo (ou nao e DFF) -> retirado da fila", (int)id);
            CStreaming::RemoveModel(id);
        }
    }
}

static void RequestModelStream_safe(int chIdx)
{
    SanitizeRequested();
    RequestModelStream_orig(chIdx);
}

void CStreaming::InjectHooks() {

    CHook::Write(g_libGTASA + 0x84DA38, &ms_pEndRequestedList);
    CHook::Write(g_libGTASA + 0x84ADF0, &ms_pStartRequestedList);
    CHook::Write(g_libGTASA + 0x84E8D8, &ms_pEndLoadedList);
    CHook::Write(g_libGTASA + 0x84C4B8, &ms_startLoadedList);

    CHook::Write(g_libGTASA + 0x84A938, &ms_bEnableRequestListPurge);
    CHook::Write(g_libGTASA + 0x84B028, &ms_disableStreaming);
    CHook::Write(g_libGTASA + 0x84E680, &ms_channel);
    CHook::Write(g_libGTASA + 0x84A580, &ms_numModelsRequested);
    CHook::Write(g_libGTASA + 0x84FCB0, &ms_numPriorityRequests);
    CHook::Write(g_libGTASA + 0x84DAD0, &ms_bLoadingBigModel);
    CHook::Write(g_libGTASA + 0x84BA20, &ms_channelError);
    CHook::Write(g_libGTASA + 0x84A4A8, &ms_pStreamingBuffer);
    CHook::Write(g_libGTASA + 0x850C78, &ms_streamingBufferSize);

    CHook::Write(g_libGTASA + 0x851D80, &CStreaming::ms_memoryUsed);
    CHook::Write(g_libGTASA + 0x850408, &CStreaming::ms_memoryAvailable);
    CHook::Write(g_libGTASA + 0x850B68, &CStreaming::desiredNumVehiclesLoaded);
    CHook::Write(g_libGTASA + 0x84B5D0, &CStreaming::ms_files);

    CHook::Write(g_libGTASA + 0x84CB10, &CStreaming::ms_rwObjectInstances);
    CHook::Write(g_libGTASA + 0x84DBD0, &CStreaming::ms_aInfoForModel);

    CHook::Redirect("_ZN10CStreaming13InitImageListEv", &CStreaming::InitImageList);
    CHook::InlineHook("_Z12CdStreamReadiPvjj", &CdStreamRead_guard, &CdStreamRead_orig);
    CHook::InlineHook("_ZN10CStreaming18RequestModelStreamEi", &RequestModelStream_safe, &RequestModelStream_orig);   // contorno: modelo sem entrada no IMG nao derruba   // guarda contra IMG nao aberto (ver acima)
    // touch.img: ignora o cache posicional MODELS\MINFO.BIN (ver CustomImg.h) para o diretorio dos IMGs ser ligado pelo NOME
    // SEMPRE: ignora o cache posicional MINFO.BIN (evita travar com cache velho mesmo sem touch.img) - nao e funcionalidade, e protecao
    // CINFO.BIN (cache de colisao): invalida o cache velho antes do CColAccel::startCache (ver CustomCol.h)
    // SEMPRE: guarda do CINFO.BIN (invalida cache velho) - protecao, nao e desligavel pelo diag (para testar col: tire os arquivos de SAMP/col)
    CustomCol::InstallCinfoGuard();
    // IPL por instancia (texdb/ipl/*.ipl): mistura no CIplStore::LoadIpl (ver CustomIpl.h / LEIA-ME_IPL.md)
    if (Diag::On("ipl")) CustomIpl::Install();
    CHook::Redirect("_ZN10CStreaming12MakeSpaceForEi", &CStreaming::MakeSpaceFor);
    //CHook::Redirect("_ZN10CStreaming22LoadAllRequestedModelsEb", &CStreaming::LoadAllRequestedModels);
}

int CStreaming::AddImageToList(char const* pFileName, bool bNotPlayerImg) {
    // find a free slot
    std::int32_t fileIndex = 0;
    for (; fileIndex < TOTAL_IMG_ARCHIVES; fileIndex++) {
        if (!ms_files[fileIndex].m_szName[0])
            break;
    }

    if (fileIndex >= TOTAL_IMG_ARCHIVES) {
        assert("AddImageToList");
    }

    // free slot found, load the IMG file
    strcpy(ms_files[fileIndex].m_szName, pFileName);
    ms_files[fileIndex].m_StreamHandle = CdStreamOpen(pFileName);
    ms_files[fileIndex].m_bNotPlayerImg = bNotPlayerImg;
    return fileIndex;
}

void CStreaming::InitImageList() {
    for (auto & ms_file : ms_files) {
        ms_file.m_szName[0] = 0;
        ms_file.m_StreamHandle = 0;
    }

#if VER_SAMP
    CStreaming::AddImageToList("TEXDB\\GTA3.IMG", true);
    CStreaming::AddImageToList("TEXDB\\GTA_INT.IMG", true);
//    CStreaming::AddImageToList("TEXDB\\SAMP.IMG", true);
//    CStreaming::AddImageToList("TEXDB\\SAMPCOL.IMG", true);
#else
    CStreaming::AddImageToList("TEXDB\\GTA3.IMG", true);
    CStreaming::AddImageToList("TEXDB\\GTA_INT.IMG", true);
    CStreaming::AddImageToList("TEXDB\\SAMP.IMG", true);
    CStreaming::AddImageToList("TEXDB\\SAMPCOL.IMG", true);
#endif

    // touch.img: veiculos/skins customizados (ids no samp.ide / vehicles.ide). So o diretorio e' lido aqui;
    // o DFF so vem do disco quando o modelo e' requisitado (criar veiculo / setar skin com o ID).
    // O IMG e SEMPRE registrado (se existir): sem ele o jogo fecha logo depois de "Loading script.img.." (logs de 2026-10-30, 4 execucoes)
    if (CustomImg::ImgPresent())
    {
        const int idx = CStreaming::AddImageToList(CustomImg::kImgListName, true);
        FLog("[TOUCH] %s registrado no slot %d (handle=%d)", CustomImg::kImgListName, idx,
             (idx >= 0 && idx < (int)TOTAL_IMG_ARCHIVES) ? ms_files[idx].m_StreamHandle : -1);
        FLog("[TOUCH] cache MODELS\\MINFO.BIN ignorado: IMG -> modelo e' ligado pelo nome (CModelInfoAccelerator::GetEntry redirecionado)");
    }
    else
    {
        FLog("[TOUCH] texdb/touch.img nao encontrado: ignorado");
    }
}

// Request a given model to be loaded.
// Can be called on an already requested model to add `PRIORITY_REQUEST` flag
void CStreaming::RequestModel(int32 modelId, int32 streamingFlags) {
    CStreamingInfo& info = GetInfo(modelId);

    switch (info.m_nLoadState) {
        case eStreamingLoadState::LOADSTATE_NOT_LOADED:
            break;

        case eStreamingLoadState::LOADSTATE_REQUESTED: {
            // Model already requested, just set priority request flag if not set already
            if ((streamingFlags & STREAMING_PRIORITY_REQUEST) && !info.IsPriorityRequest())
            {
                ++ms_numPriorityRequests;
                info.SetFlags(STREAMING_PRIORITY_REQUEST);
            }
            break;
        }

        default: {
            streamingFlags &= ~STREAMING_PRIORITY_REQUEST; // Remove flag otherwise
            break;
        }
    }
    info.SetFlags(streamingFlags);

    // BUG: Possibly? If the model was requested once with `PRIORITY_REQUEST` set
    //      and later is requested without it `ms_numPriorityRequests` won't be decreased.

    switch (info.m_nLoadState) {
        case eStreamingLoadState::LOADSTATE_LOADED: {
            if (info.InList()) {
                info.RemoveFromList();
                if (IsModelDFF(modelId)) {
                    switch (CModelInfo::GetModelInfo(modelId)->GetModelType()) {
                        case MODEL_INFO_PED:
                        case MODEL_INFO_VEHICLE: {
                            return;
                        }
                    }
                }

                if (!info.IsMissionOrGameRequired())
                    info.AddToList(ms_startLoadedList);
            }
            break;
        }
        case eStreamingLoadState::LOADSTATE_READING:
        case eStreamingLoadState::LOADSTATE_REQUESTED:
        case eStreamingLoadState::LOADSTATE_FINISHING:
            break;

        case eStreamingLoadState::LOADSTATE_NOT_LOADED: {
            switch (GetModelType(modelId)) {
                case eModelType::TXD: { // Request parent (if any) TXD
                    info.m_nLoadState = LOADSTATE_LOADED;
                    return;
                }
                case eModelType::DFF: { // Request TXD and (if any) IFP
                    CBaseModelInfo* modelInfo = CModelInfo::GetModelInfo(modelId);
                  //  RequestTxdModel(modelInfo->m_nTxdIndex, streamingFlags);

                    const int32 animFileIndex = modelInfo->GetAnimFileIndex();
                    if (animFileIndex != -1)
                        RequestModel(IFPToModelId(animFileIndex), STREAMING_KEEP_IN_MEMORY);
                    break;
                }
            }
            info.AddToList(ms_pStartRequestedList);

            ++ms_numModelsRequested;
            if (streamingFlags & STREAMING_PRIORITY_REQUEST)
                ++ms_numPriorityRequests;

            info.SetFlags(streamingFlags);
            info.m_nLoadState = LOADSTATE_REQUESTED;
            break;
        }
    }
}

void CStreaming::AddLodsToRequestList(const CVector* point, int32 streamingFlags) {
    CHook::CallFunction<void>(g_libGTASA + 0x392B6C, point, streamingFlags);
}

void CStreaming::AddModelsToRequestList(const CVector* point, int32 streamingFlags) {
    CHook::CallFunction<void>(g_libGTASA + 0x392874, point, streamingFlags);
}

#include "Textures/TextureDatabaseRuntime.h"
extern CNetGame *pNetGame;
void CStreaming::Update() {

    if (CTimer::GetIsPaused())
        return;

    if(!CStreaming::GetInfo(MODEL_MALE01).IsLoaded()) {
        RequestModel(MODEL_MALE01, STREAMING_KEEP_IN_MEMORY);
        CStreaming::LoadAllRequestedModels(false);
    }
    CModelInfo::GetModelInfo(MODEL_MALE01)->m_nRefCount = 999;

    if(CTimer::m_snTimeInMillisecondsNonClipped % 100 == 0)
        RemoveLeastUsedModel(STREAMING_KEEP_IN_MEMORY);


    static double previousTime{};
    const double currentTimeInSeconds = CTimer::m_snTimeInMillisecondsNonClipped / 1000.0;
    const double deltaTime = currentTimeInSeconds - previousTime;
    previousTime = currentTimeInSeconds;
    const double clampedDeltaTime = std::min(0.1, deltaTime);
    TextureDatabaseRuntime::UpdateStreaming(clampedDeltaTime, true);

    CCamera& TheCamera = *reinterpret_cast<CCamera*>(g_libGTASA + 0xBBA8D0);

    const auto& camPos = TheCamera.GetPosition();
    const float fCamDistanceToGroundZ = camPos.z - TheCamera.CalculateGroundHeight(eGroundHeightType::ENTITY_BB_BOTTOM);
    if (!ms_disableStreaming && !CRenderer::m_loadingPriority) {
        if (fCamDistanceToGroundZ >= 50.0f) {
            if (CGame::CanSeeOutSideFromCurrArea()) {
                AddLodsToRequestList(&camPos, 0);
            }
        }
        else if (CRenderer::ms_bRenderOutsideTunnels) {
            AddModelsToRequestList(&camPos, 0);
        }
    }

//    if (CTimer::GetFrameCounter() % 128 == 106) {
//        m_bBoatsNeeded = false;
//        if (camPos.z < 500.0f) {
//            m_bBoatsNeeded = ThePaths.IsWaterNodeNearby(camPos, 80.0f);
//        }
//    }
    if(!pNetGame || !pNetGame->GetPlayerPool()->GetLocalPlayer())
        return;

    auto pLocalPed = pNetGame->GetPlayerPool()->GetLocalPlayer()->GetPlayerPed()->m_pPed;
    const CVector& playerPos = pLocalPed->GetPosition();
//    if (!ms_disableStreaming
//        && !CCutsceneMgr::IsCutsceneProcessing()
//        && CGame::CanSeeOutSideFromCurrArea()
//        && CReplay::Mode != MODE_PLAYBACK
//        && fCamDistanceToGroundZ < 50.0f
//            ) {
//        StreamVehiclesAndPeds_Always(playerPos);
//        if (!IsVeryBusy()) {
//            StreamVehiclesAndPeds();
//            StreamZoneModels(playerPos);
//        }
//    }
    LoadRequestedModels();

    if (pLocalPed->IsInVehicle()) {
        CVehicleGTA* remoteVehicle = pLocalPed->pVehicle;

        CColStore::AddCollisionNeededAtPosn(&playerPos);
        CIplStore::AddIplsNeededAtPosn(&playerPos);

        const auto& removeVehiclePos = remoteVehicle->GetPosition();
        CColStore::LoadCollision(removeVehiclePos, false);
        CColStore::EnsureCollisionIsInMemory(&removeVehiclePos);
        CIplStore::LoadIpls(removeVehiclePos, false);
        CIplStore::EnsureIplsAreInMemory(&removeVehiclePos);
    }
    else {
        CColStore::LoadCollision(playerPos, false);
        CColStore::EnsureCollisionIsInMemory(&playerPos);
        CIplStore::LoadIpls(playerPos, false);
        CIplStore::EnsureIplsAreInMemory(&playerPos);
    }

    if (ms_bEnableRequestListPurge) {
        PurgeRequestList();
    }
}

// Call `RemoveModel` on all models in the request list except
// those ones which have either `KEEP_IN_MEMORY` or `PRIORITY_REQUEST` flag(s) set.
void CStreaming::PurgeRequestList() {
    auto info = ms_pEndRequestedList->GetPrev();
    while (info != ms_pStartRequestedList) {
        auto prev = info->GetPrev();
        if (!info->IsRequiredToBeKept() && !info->IsPriorityRequest()) {
            RemoveModel(GetModelFromInfo(info));
        }
        info = prev;
    }
}

void CStreaming::LoadRequestedModels() {
    CHook::CallFunction<void>(g_libGTASA + 0x394008);
}

// There are only 2 streaming channels within CStreaming::ms_channel. In this function,
// if your current channelIndex is zero then "1 - channelIndex" will give you the other
// streaming channel within CStreaming::ms_channel which is 1 (second streaming channel).
void CStreaming::LoadAllRequestedModels(bool bOnlyPriorityRequests) {
   static bool m_bLoadingAllRequestedModels{};

    if (m_bLoadingAllRequestedModels) {
        return;
    }
    m_bLoadingAllRequestedModels = true;

    FlushChannels();

    auto numModelsToLoad = std::max(10, 2 * ms_numModelsRequested);
    int32 chIdx = 0;
    while (true) {
        const tStreamingChannel& ch1 = ms_channel[0];
        const tStreamingChannel& ch2 = ms_channel[1];

        if (IsRequestListEmpty()
            && ch1.IsIdle()
            && ch2.IsIdle()
            || numModelsToLoad <= 0
                ) {
            break;
        }

        if (ms_bLoadingBigModel) {
            chIdx = 0;
        }

        auto& currCh = ms_channel[chIdx];

        if (!currCh.IsIdle()) {
            // Finish loading whatever it was loading
            CdStreamSync(chIdx);
            currCh.iLoadingLevel = 100;
        }
        if (currCh.IsReading()) {
            ProcessLoadingChannel(chIdx);
            if (currCh.IsStarted())
                ProcessLoadingChannel(chIdx); // Finish loading big model
        }
        if (bOnlyPriorityRequests && ms_numPriorityRequests == 0)
            break;

        if (!ms_bLoadingBigModel) {
            const auto other = 1 - chIdx;
            if (ms_channel[other].IsIdle()) {
                RequestModelStream(other);
            }

            if (currCh.IsIdle() && !ms_bLoadingBigModel) {
                RequestModelStream(chIdx);
            }
        }

        if (ch1.IsIdle() && ch2.IsIdle())
            break;

        chIdx = 1 - chIdx; // Switch to other channel
        --numModelsToLoad;
    }
    FlushChannels();
    m_bLoadingAllRequestedModels = false;
}

int32 CStreaming::GetNextFileOnCd(uint32 streamLastPosn, bool bNotPriority) {
  //  ZoneScoped;

    uint32 nextRequestModelPos    = UINT32_MAX;
    uint32 firstRequestModelCdPos = UINT32_MAX;
    int32  firstRequestModelId    = MODEL_INVALID;
    int32  nextRequestModelId     = MODEL_INVALID;
    for (auto info = ms_pStartRequestedList->GetNext(); info != ms_pEndRequestedList; info = info->GetNext()) {
        const auto modelId = GetModelFromInfo(info);
        if (bNotPriority && ms_numPriorityRequests != 0 && !info->IsPriorityRequest())
            continue;

        // Additional conditions for some model types (DFF, TXD, IFP)
        switch (GetModelType(modelId)) {
            case eModelType::DFF: {
                CBaseModelInfo* modelInfo = CModelInfo::GetModelInfo(modelId);

//                // Make sure TXD will be loaded for this model
//                const auto txdModel = TXDToModelId(modelInfo->m_nTxdIndex);
//                if (!GetInfo(txdModel).IsLoadedOrBeingRead()) {
//                    RequestModel(txdModel, GetInfo(modelId).GetFlags()); // Request TXD for this DFF
//                    continue;
//                }

                // Check if it has an anim (IFP), if so, make sure it gets loaded
                if(modelInfo) {
                    const int32 animFileIndex = modelInfo->GetAnimFileIndex();
                    if (animFileIndex != -1) {
                        const int32 animModelId = IFPToModelId(animFileIndex);
                        if (!GetInfo(animModelId).IsLoadedOrBeingRead()) {
                            RequestModel(animModelId, STREAMING_KEEP_IN_MEMORY);
                            continue;
                        }
                    }
                }
                break;
            }
//            case eModelType::TXD: {
//                // Make sure parent is/will be loaded
//                TxdDef* texDictionary = CTxdStore::ms_pTxdPool->GetAt(ModelIdToTXD(modelId));
//                const int16 parentIndex = texDictionary->m_wParentIndex;
//                if (parentIndex != -1) {
//                    const int32 parentModelIdx = TXDToModelId(parentIndex);
//                    if (!GetInfo(parentModelIdx).IsLoadedOrBeingRead()) {
//                        RequestModel(parentModelIdx, STREAMING_KEEP_IN_MEMORY);
//                        continue;
//                    }
//                }
//                break;
//            }
//            case eModelType::IFP: {
////                if (CCutsceneMgr::IsCutsceneProcessing() || !GetInfo(MODEL_MALE01).IsLoaded()) {
////                    // Skip in this case
////                    continue;
////                }
//                break;
//            }
        }

        const uint32 modelCdPos = GetInfo(modelId).GetCdPosn();
        if (modelCdPos < firstRequestModelCdPos) {
            firstRequestModelCdPos = modelCdPos;
            firstRequestModelId = modelId;
        }

        if (modelCdPos < nextRequestModelPos && modelCdPos >= streamLastPosn) {
            nextRequestModelPos = modelCdPos;
            nextRequestModelId = modelId;
        }
    }

    const int32 nextModelId = nextRequestModelId == MODEL_INVALID ? firstRequestModelId : nextRequestModelId;
    if (nextModelId != MODEL_INVALID || ms_numPriorityRequests == 0)
        return nextModelId;

    ms_numPriorityRequests = 0;
    return MODEL_INVALID;
}


// Valida o streaming info ANTES de usa-lo em RequestModelStream. Causa dos logs de 2026-10-31: GetCdPosn() faz
// ms_files[m_nImgId].m_StreamHandle com ms_files de TOTAL_IMG_ARCHIVES(=8) posicoes; um info com m_nImgId>=8 (ou modelId fora de
// RESOURCE_ID_TOTAL) le memoria alheia (valores 0x49xxxxxx = ponteiros) e o posn/tamanho enviados ao CdStreamRead saem lixo.
// Aqui o modelo problematico e IDENTIFICADO no log e removido da fila (nao entra em loop).
static bool StreamInfoBad(int32 modelId, const char* where)
{
    static int s_logged = 0;
    if (modelId < 0 || modelId >= (int32)RESOURCE_ID_TOTAL)
    {
        CrashReport::Mark("stream-bad", modelId, 0);
        if (s_logged++ < 40) FLog("[STREAM][BAD] %s: modelId=%d FORA do intervalo (0..%d)", where, modelId, (int)RESOURCE_ID_TOTAL - 1);
        return true;
    }
    const CStreamingInfo& inf = CStreaming::ms_aInfoForModel[modelId];
    if (inf.m_nCdSize == 0) return false;   // sem entrada em IMG: o proprio fluxo ja trata
    if (inf.m_nImgId >= TOTAL_IMG_ARCHIVES || inf.m_nCdSize > 0x40000)
    {
        CrashReport::Mark("stream-bad", modelId, (int)inf.m_nImgId);
        if (s_logged++ < 40)
        {
            FLog("[STREAM][BAD] %s: modelId=%d imgId=%u cdPosn=0x%X cdSize=%u loadState=%d nextOnCd=%d",
                 where, modelId, (unsigned)inf.m_nImgId, (unsigned)inf.m_nCdPosn, (unsigned)inf.m_nCdSize, (int)inf.m_nLoadState, (int)inf.m_nNextIndexOnCd);
        }
        return true;
    }
    return false;
}

// Starts reading at most 16 models at a time.
// Removes all unused (if not IsRequiredToBeKept()) IFP/TXDs models as well.
void CStreaming::RequestModelStream(int32 chIdx) {
    int32 modelId = GetNextFileOnCd(CdStreamGetLastPosn(), true);
    if (modelId == MODEL_INVALID)
        return;

    CrashReport::Mark("stream", modelId, (modelId >= 0 && modelId < (int32)RESOURCE_ID_TOTAL) ? (int)GetInfo(modelId).m_nImgId : -1);
    if (StreamInfoBad(modelId, "primeiro modelo")) { if (modelId >= 0 && modelId < (int32)RESOURCE_ID_TOTAL) RemoveModel(modelId); return; }

    tStreamingChannel& ch = ms_channel[chIdx];
    size_t posn = 0;
    size_t nThisModelSizeInSectors = 0;
    CStreamingInfo* streamingInfo = &GetInfo(modelId);

    // Find first model that has to be loaded
    while (!streamingInfo->IsRequiredToBeKept()) {
        // In case of TXD/IFP's check if they're used at all, if not remove them.
        if (IsModelIFP(modelId)) {
            if (AreAnimsUsedByRequestedModels(ModelIdToIFP(modelId)))
                break;
        } else /*model is neither TXD or IFP*/ {
            break; // No checks needed
        }

        // TXD/IFP unused, so remove it, and go on to the next file

        RemoveModel(modelId);

        streamingInfo->GetCdPosnAndSize(posn, nThisModelSizeInSectors);    // Grab pos and size of this model
        modelId = GetNextFileOnCd(posn + nThisModelSizeInSectors, true);   // Find where the next file is after it
        if (modelId == MODEL_INVALID)
            return; // No more models...
        streamingInfo = &GetInfo(modelId); // Grab next file's info
    }

    // Grab cd pos and size for this model
    streamingInfo->GetCdPosnAndSize(posn, nThisModelSizeInSectors);

    // Check if it's big 0x40CCD5
    if (nThisModelSizeInSectors > ms_streamingBufferSize) {
        // A model is considered "big" if it doesn't fit into a single channel's buffer
        // In which case it has to be loaded entirely by channel 0.
        if (chIdx == 1 || !ms_channel[1].IsIdle())
            return;
        ms_bLoadingBigModel = true;
    }

    // Find all (but at most 16) consecutive models starting at `posn` and load them in one go
    uint32 nSectorsToRead = 0; // The # of sectors to be loaded beginning at `posn`

    bool isPreviousLargeishBigOrVeh = false;
    bool isPreviousModelPed = false;

    // 0x40CD10
    uint32 i = 0;
    for (; i < std::size(ch.modelIds); i++) {
        if (modelId == MODEL_INVALID) {
            break;
        }
        if (StreamInfoBad(modelId, "cadeia do diretorio")) break;   // nao le nada alem do que ja estava valido
        streamingInfo = &GetInfo(modelId);

        if (!streamingInfo->IsRequested())
            break; // Model not requested, so no need to load it.

        if (streamingInfo->GetCdSize())
            nThisModelSizeInSectors = streamingInfo->GetCdSize();

        const bool isThisModelLargeish = nThisModelSizeInSectors > 200;

        if (ms_numPriorityRequests && !streamingInfo->IsPriorityRequest())
            break; // There are priority requests, but this isn't one of them

        CBaseModelInfo* mi = CModelInfo::GetModelInfo(modelId);
        if (IsModelDFF(modelId)) {
            if (isPreviousModelPed && mi->GetModelType() == MODEL_INFO_PED)
                break; // Don't load two peds after each other

            if (isPreviousLargeishBigOrVeh && mi->GetModelType() == MODEL_INFO_VEHICLE)
                break; // Don't load two vehicles / big model + vehicle after each other

            // Check if TXD and/or IFP is loaded for this model.
            // If not we can't load the model yet.

            // Check TXD
//            if (!GetInfo(TXDToModelId(mi->m_nTxdIndex)).IsLoadedOrBeingRead())
//                break;

            // Check IFP (if any)
            const int32 animFileIndex = mi->GetAnimFileIndex();
            if (animFileIndex != -1) {
                if (!GetInfo(IFPToModelId(animFileIndex)).IsLoadedOrBeingRead())
                    break;
            }
        } else {
            if (isPreviousLargeishBigOrVeh && isThisModelLargeish)
                break; // Do not load a big model/car and a big model after each other
        }

        // At this point we've made sure the model can be loaded
        // so let's add it to the channel.

        // Set offset where the model's data begins at
        ch.modelStreamingBufferOffsets[i] = nSectorsToRead;

        // Set the corresponding modelId
        ch.modelIds[i] = modelId;

        // `i == 0` is a special case:
        // If the 0th model doesn't fit into the buffer it's a `big` one
        // so `ms_bLoadingBigModel` is set already (before the `for` loop).
        // But we still need to continue to set the appropriate states for the
        // model, thus we can't just `break` (which would also cause the loop below setting modelId slots `-1`'s to override the modelId)
        if (i > 0) {
            // Check if this model + all the previous fits into one channel's buffer
            if (nSectorsToRead + nThisModelSizeInSectors > ms_streamingBufferSize) {
                // No, so stop at the previous model, and ignore this one
                break;
            }
        }
        nSectorsToRead += nThisModelSizeInSectors;

        if (IsModelDFF(modelId)) {
            switch (mi->GetModelType()) {
                case ModelInfoType::MODEL_INFO_PED:
                    isPreviousModelPed = true;
                    break;
                case ModelInfoType::MODEL_INFO_VEHICLE:
                    isPreviousLargeishBigOrVeh = true; // I guess all vehicles are considered big?
                    break;
            }
        } else {
            if (isThisModelLargeish)
                isPreviousLargeishBigOrVeh = true;
        }

        // Modify the state of models
        {
            streamingInfo->m_nLoadState = LOADSTATE_READING; // Set as being read
            streamingInfo->RemoveFromList(); // Remove from it's current list (That is the requested list)
            ms_numModelsRequested--;
            if (streamingInfo->IsPriorityRequest()) {
                streamingInfo->ClearFlags(STREAMING_PRIORITY_REQUEST); // Remove priority request flag, as its not a request anymore.
                ms_numPriorityRequests--;
            }
        }

        modelId = streamingInfo->m_nNextIndexOnCd; // Continue onto the next one in the directory
    }

    // Set remaining modelId slots to `-1`
    for (auto j = i; j < std::size(ch.modelIds); j++) {
        ch.modelIds[j] = MODEL_INVALID;
    }

    // PROTECAO NA ORIGEM (samp_log 2026-10-31: SIGSEGV 0x8 na CdStreamThread, IMG #87 e setores=1463484976 = lixo):
    // so le se o IMG do posn esta aberto e a quantidade de setores e' plausivel. Senao loga TODA a cadeia, devolve os modelos
    // ao estado "nao carregado" e sai SEM marcar o canal como READING (nada de loop no Splash).
    {
        const uint32_t arch = (uint32_t)posn >> 24;
        void* fileOpen = (arch < 32) ? *reinterpret_cast<void**>(g_libGTASA + 0x8EAA40 + (size_t)arch * 8) : nullptr;
        if (!fileOpen || nSectorsToRead > 0x40000) {
            CrashReport::Mark("read-bad", (int)arch, (int)nSectorsToRead);
            static int s_badLogged = 0;
            if (s_badLogged++ < 20) {
                FLog("[STREAM][BAD] leitura recusada: canal=%d posn=0x%X (IMG #%u %s) setores=%u modelos=%u firstModel=%d",
                     (int)chIdx, (unsigned)posn, arch, fileOpen ? "aberto" : "SEM ARQUIVO", (unsigned)nSectorsToRead, (unsigned)i, (int)ch.modelIds[0]);
                for (uint32 j = 0; j < i && j < std::size(ch.modelIds); j++) {
                    const int32 mid = ch.modelIds[j];
                    if (mid < 0 || mid >= (int32)RESOURCE_ID_TOTAL) { FLog("[STREAM][BAD]   [%u] modelId=%d FORA do intervalo", (unsigned)j, (int)mid); continue; }
                    const CStreamingInfo& q = ms_aInfoForModel[mid];
                    FLog("[STREAM][BAD]   [%u] modelId=%d imgId=%u cdPosn=0x%X cdSize=%u loadState=%d", (unsigned)j, (int)mid,
                         (unsigned)q.m_nImgId, (unsigned)q.m_nCdPosn, (unsigned)q.m_nCdSize, (int)q.m_nLoadState);
                }
            }
            for (uint32 j = 0; j < i && j < std::size(ch.modelIds); j++) {
                const int32 mid = ch.modelIds[j];
                if (mid >= 0 && mid < (int32)RESOURCE_ID_TOTAL) RemoveModel(mid);
                ch.modelIds[j] = MODEL_INVALID;
            }
            ms_bLoadingBigModel = false;
            return;
        }
    }
    CdStreamRead(chIdx, ms_pStreamingBuffer[chIdx], posn, nSectorsToRead); // Request models to be read
    ch.LoadStatus = eChannelState::READING;
    ch.iLoadingLevel = 0;
    ch.sectorCount = nSectorsToRead; // Set how many sectors to read
    ch.offsetAndHandle = posn;       // And from where to read
    ch.totalTries = 0;

    bool& m_bModelStreamNotLoaded = *reinterpret_cast<bool*>(g_libGTASA + 0x972E80);
    if (m_bModelStreamNotLoaded)
        m_bModelStreamNotLoaded = false;
}

// If the channel is done reading (`CdStreamGetStatus(chIdx)` == READING_SUCCESS) then loads all the read models
// using either `ConvertBufferToObject` or `FinishLoadingLargeFile` (in case of big models)
bool CStreaming::ProcessLoadingChannel(int32 chIdx) {
    tStreamingChannel& ch = ms_channel[chIdx];

   // Log("ProcessLoadingChannel");
    const eCdStreamStatus streamStatus = CdStreamGetStatus(chIdx);
    switch (streamStatus) {
        case eCdStreamStatus::READING_SUCCESS:
            break;

        case eCdStreamStatus::READING:
        case eCdStreamStatus::WAITING_TO_READ:
            return false; // Not ready yet.

        case eCdStreamStatus::READING_FAILURE: {
            // Retry

            ch.m_nCdStreamStatus = streamStatus;
            ch.LoadStatus = eChannelState::ERR;

            if (ms_channelError != -1)
                return false;

            ms_channelError = chIdx;
            RetryLoadFile(chIdx);

            return true;
        }
    }
  //  Log("ProcessLoadingChannel1");
    const bool isStarted = ch.IsStarted();
    ch.LoadStatus = eChannelState::IDLE;
    if (isStarted) {
        // It's a large model so finish loading it
        auto bufferOffset = ch.modelStreamingBufferOffsets[0];
        auto* pFileContents = reinterpret_cast<uint8*>(&ms_pStreamingBuffer[chIdx][STREAMING_SECTOR_SIZE * bufferOffset]);
        FinishLoadingLargeFile(pFileContents, ch.modelIds[0]);
        ch.modelIds[0] = MODEL_INVALID;
    } else {
        // Load models individually
        for (uint32 i = 0u; i < std::size(ch.modelIds); i++) {
            const int32 modelId = ch.modelIds[i];
            if (modelId == MODEL_INVALID)
                continue;

            CBaseModelInfo* baseModelInfo = CModelInfo::GetModelInfo(modelId);
            CStreamingInfo& info = GetInfo(modelId);

            if (!IsModelDFF(modelId)
                || baseModelInfo->GetModelType() != MODEL_INFO_VEHICLE /* It's a DFF, check if its a vehicle */
              //  || ms_vehiclesLoaded.CountMembers() < desiredNumVehiclesLoaded /* It's a vehicle, so lets check if we can load more */
              //  || RemoveLoadedVehicle() /* no, so try to remove one, and load this in its place */
                || info.IsMissionOrGameRequired() /* failed, lets check if its absolutely mission critical */
                    ) {
                if (!IsModelIPL(modelId)) {
                    MakeSpaceFor(info.GetCdSize() * STREAMING_SECTOR_SIZE); // IPL's dont require any memory themselves
                }
                const auto bufferOffsetInSectors = ch.modelStreamingBufferOffsets[i];
                auto* fileBuffer = reinterpret_cast <uint8*> (&ms_pStreamingBuffer[chIdx][STREAMING_SECTOR_SIZE * bufferOffsetInSectors]);

                // Actually load the model into memory
                ConvertBufferToObject(fileBuffer, modelId);

                if (info.IsLoadingFinishing()) {
                    ch.LoadStatus = eChannelState::STARTED;
                    ch.modelStreamingBufferOffsets[i] = bufferOffsetInSectors;
                    ch.modelIds[i] = modelId;
                    if (i == 0)
                        continue;
                }
                ch.modelIds[i] = MODEL_INVALID;
            } else {
                // I think at this point it's guaranteed to be a vehicle (thus its a DFF),
                // with `STREAMING_MISSION_REQUIRED` `STREAMING_GAME_REQUIRED` flags unset.

                const int32 modelTxdIdx = baseModelInfo->m_nTxdIndex;
                RemoveModel(modelId);

                if (info.IsMissionOrGameRequired()) {
                    // Re-request it.
                    // I think this code is unreachable, because
                    // if any of the 2 flags (above) is set this code is never reached.
                    RequestModel(modelId, info.GetFlags());
                } else if (!CTxdStore::GetNumRefs(modelTxdIdx))
                    RemoveTxdModel(modelTxdIdx); // Unload TXD, as it has no refs
            }
        }
    }

    if (ms_bLoadingBigModel) {
        if (!ch.IsStarted()) {
            ms_bLoadingBigModel = false;
            for (auto& id : ms_channel[1].modelIds) {
                id = MODEL_INVALID;
            }
        }
    }

    return true;
}

bool CStreaming::ConvertBufferToObject(uint8* fileBuffer, int32 modelId) {
    return CHook::CallFunction<bool>(g_libGTASA + 0x395114, fileBuffer, modelId);
}

// Finishes loading a big model by loading the second half of the file
// residing at `pFileBuffer`.
void CStreaming::FinishLoadingLargeFile(uint8* pFileBuffer, int32 modelId) {
    CHook::CallFunction<void>(g_libGTASA + 0x395948, pFileBuffer, modelId);
}

void CStreaming::RetryLoadFile(int32 chIdx) {
    CHook::CallFunction<void>(g_libGTASA + 0x394220, chIdx);
}

void CStreaming::MakeSpaceFor(size_t memoryToCleanInBytes) {
    auto lastmemused = ms_memoryUsed;
    while (ms_memoryUsed >= ms_memoryAvailable - memoryToCleanInBytes) {
        lastmemused = ms_memoryUsed;
        if (!RemoveLeastUsedModel(0) || lastmemused == ms_memoryUsed) {
          //  DeleteRwObjectsBehindCamera(ms_memoryAvailable - memoryToCleanInBytes);
            return;
        }
    }
}

//
void CStreaming::DeleteRwObjectsBehindCamera(size_t memoryToCleanInBytes) {
    CHook::CallFunction<void>(g_libGTASA + 0x3984F0, memoryToCleanInBytes);
}

// Function name is a little misleading, as it deletes the first entity it can.
bool CStreaming::DeleteLeastUsedEntityRwObject(bool bNotOnScreen, int32 streamingFlags) {
    return CHook::CallFunction<bool>(g_libGTASA + 0x39825C, bNotOnScreen, streamingFlags);
}

// The name is misleading: It just removes the first model with no references.
bool CStreaming::RemoveLeastUsedModel(int32 streamingFlags) {

    auto streamingInfo = ms_pEndLoadedList->GetPrev();
    for (; streamingInfo != ms_startLoadedList; streamingInfo = streamingInfo->GetPrev()) {
        const auto modelId = GetModelFromInfo(streamingInfo);
        if (!streamingInfo->AreAnyFlagsSetOutOf(streamingFlags)) {
            switch (GetModelType(modelId)) {
                case eModelType::DFF: {
                    if (!CModelInfo::GetModelInfo(modelId)->m_nRefCount) {
                        RemoveModel(modelId);
                        return true;
                    }
                    break;
                }
//                case eModelType::TXD: {
//                    const auto txdId = ModelIdToTXD(modelId);
//                    if (!CTxdStore::GetNumRefs(txdId) && !AreTexturesUsedByRequestedModels(txdId)) {
//                        RemoveModel(modelId);
//                        return true;
//                    }
//                    break;
//                }
                case eModelType::IFP: {
                    const auto animBlockId = ModelIdToIFP(modelId);
                    if (!CAnimManager::GetNumRefsToAnimBlock(animBlockId) && !AreAnimsUsedByRequestedModels(animBlockId)) {
                        RemoveModel(modelId);
                        return true;
                    }
                    break;
                }
//                case eModelType::SCM: {
//                    const auto scmId = ModelIdToSCM(modelId);
//                    if (!CTheScripts::StreamedScripts.m_aScripts[scmId].m_nStatus) {
//                        RemoveModel(modelId);
//                        return true;
//                    }
//                    break;
//                }
            }
        }
    }

    CCamera& TheCamera = *reinterpret_cast<CCamera*>(g_libGTASA + 0xBBA8D0);
    if (TheCamera.GetPosition().z - TheCamera.CalculateGroundHeight(eGroundHeightType::ENTITY_BB_BOTTOM) <= 50.0f){
        return DeleteLeastUsedEntityRwObject(false, streamingFlags);
    }
    return false;
}

bool CStreaming::HasVehicleUpgradeLoaded(int32 modelId) {
    return CHook::CallFunction<bool>(g_libGTASA + 0x395098, modelId);
//    if (!GetInfo(modelId).IsLoaded())
//        return false;
//
//    assert(modelId <= INT16_MAX);
//    const int16 otherUpgradeModelId = CVehicleModelInfo::ms_linkedUpgrades.FindOtherUpgrade((int16)modelId);
//    return otherUpgradeModelId == -1 || GetInfo(otherUpgradeModelId).IsLoaded();
}

CLink<CEntityGTA*>* CStreaming::AddEntity(CEntityGTA* entity) {
    switch (entity->GetType()) {
        case ENTITY_TYPE_PED:
        case ENTITY_TYPE_VEHICLE:
            return nullptr;
    }

    auto l = ms_rwObjectInstances.Insert(entity);
    if (!l) { // No more entries left
        // Okay, try deleting something now
        for (auto it = ms_rwObjectInstances.GetTailLink().prev; it != &ms_rwObjectInstances.GetHeadLink(); it = it->prev) {
            const auto e = it->data;

            if (!e->m_bImBeingRendered && !e->m_bStreamingDontDelete) {
                e->DeleteRwObject();
                break;
            }
        }

        // Try inserting again, should succeed now because we've deleted something (Ideally anyways)
        VERIFY(l = ms_rwObjectInstances.Insert(entity));
    }
    return l;
}


char *CStreaming::GetModelCDName(int32 index) {
    return CHook::CallFunction<char*>(g_libGTASA + 0x391320, index);
}
