#pragma once

// ---------------------------------------------------------------------------
// COLISAO PARA MODELO NOVO (IDE/IPL)   -   SAMP/extra_cols.txt
//
// Por que existe: um modelo novo declarado so no IDE nasce com CBaseModelInfo::m_pColModel == nullptr. O jogo cai nisso em
// CEntity::GetBoundRect (CEntity::Add, ao colocar a instancia do IPL). O COLFILE do gta.dat tambem cai: o carregador nativo
// (CFileLoader::LoadCollisionModelVer3) recebe um CColModel nulo (falha em +0x18).
//
// O que faz (uma vez, no fim de CGame::Initialize, depois de TODOS os IDEs): para cada linha de SAMP/extra_cols.txt
// (um nome de arquivo .col em SAMP/ por linha, # = comentario), le o arquivo (COL2/COL3, varias entradas) e, para cada entrada
// cujo NOME tem ModelInfo SEM colisao, cria um CColModel e chama o carregador NATIVO Ver2/Ver3 para preenche-lo.
// Nao sobrescreve modelo que ja tem colisao. Se algo falhar, o ID entra em Failed() e o CustomIpl IGNORA a instancia desse
// modelo (log) em vez de derrubar o jogo.
// Log: prefixo [COLNEW]. A linha "carregando" sai ANTES da chamada nativa (se o jogo cair, a ultima linha mostra onde).
// ---------------------------------------------------------------------------

#include <string>
#include <vector>
#include <set>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <strings.h>

#include "../main.h"          // FLog, g_pszStorage
#include "../vendor/armhook/patch.h"
#include "game/Models/ModelInfo.h"
#include "game/Collision/ColModel.h"
#include "Streaming.h"
#include "constants.h"

namespace CustomColNew
{
    inline std::set<int>& Failed() { static std::set<int> s; return s; }
    inline bool IsFailed(int id) { return Failed().count(id) != 0; }
    inline std::vector<int>& Attached() { static std::vector<int> v; return v; }   // ids cuja colisao foi ligada aqui

    // Sonda de diagnostico (1x por frame, ver CGame::Process): a cada ~300 frames loga o estado de streaming/RW dos modelos novos.
    // Serve para ver, no samp_log.txt, se o modelo chegou a ser pedido, carregado e se o objeto RW existe (= pode ser desenhado).
    inline void Probe()
    {
        static unsigned frame = 0, logged = 0;
        if (Attached().empty() || (++frame % 300) != 0 || logged >= 40) return;
        ++logged;
        for (int id : Attached())
        {
            CBaseModelInfo* mi = CModelInfo::ms_modelInfoPtrs[id];
            if (!mi) continue;
            const CStreamingInfo& si = CStreaming::GetInfo(id);
            const int txd = mi->m_nTxdIndex;
            const int txdLoad = (txd >= 0) ? (int)CStreaming::GetInfo((int)RESOURCE_ID_TXD + txd).m_nLoadState : -1;
            FLog("[LOJA] #%u id=%d '%s' stream=%d(1=ok) sec=%u rw=%p tris=%u txd=%d/%d refs=%d",
                 logged, id, mi->m_modelName, (int)si.m_nLoadState, (unsigned)si.m_nCdSize, (void*)mi->m_pRwObject,
                 mi->m_pColModel ? (unsigned)mi->m_pColModel->GetTriCount() : 0u, txd, txdLoad, (int)mi->m_nRefCount);
        }
    }

    struct Entry { std::string name; uint32_t ver = 0; size_t dataOff = 0; size_t dataLen = 0; };

    // Lista as entradas do arquivo (COL2/COL3). Cabecalho: "COLx"(4) tamanho(4) nome(22) id(2); dados em +32, tamanho-24 bytes. Funcao pura (testada no PC).
    inline bool ParseEntries(const uint8_t* b, size_t n, std::vector<Entry>& out, std::string* why = nullptr)
    {
        size_t off = 0;
        while (off + 32 <= n)
        {
            const char* m = reinterpret_cast<const char*>(b + off);
            uint32_t ver = 0;
            if (std::memcmp(m, "COL3", 4) == 0) ver = 3;
            else if (std::memcmp(m, "COL2", 4) == 0) ver = 2;
            else { if (why) *why = "assinatura desconhecida (so COL2/COL3)"; return !out.empty(); }
            uint32_t sz; std::memcpy(&sz, b + off + 4, 4);
            if (sz < 24 + 40 || off + 8 + (size_t)sz > n) { if (why) *why = "tamanho de entrada invalido/truncado"; return !out.empty(); }
            char nm[23]; std::memcpy(nm, b + off + 8, 22); nm[22] = 0;
            Entry e; e.name = nm; e.ver = ver; e.dataOff = off + 32; e.dataLen = (size_t)sz - 24;
            out.push_back(e);
            off += 8 + (size_t)sz;
        }
        return !out.empty();
    }

    using FindFn = CBaseModelInfo* (*)(const char*, int*);
    using LoadFn = void (*)(uint8_t*, uint32_t, CColModel&, const char*);
    using CtorFn = void (*)(CColModel*);


    // Colisao "vazia" (1 caixa de 1 m na origem) compartilhada: o jogo cai em CEntity::GetBoundRect quando uma instancia (ex.: os LODs
    // LAn 3999..4049 quando o CINFO e' usado) chega ao CWorld::Add com m_pColModel nulo. LOD nao colide (usa-colisao desligado pelo jogo),
    // so precisa de um CColModel valido. Quando o COL real carregar, o jogo troca o ponteiro normalmente.
    inline CColModel* Dummy()
    {
        // UM CColModel NOVO por chamada (um por modelo): compartilhar o mesmo objeto entre modelos corrompe a memoria quando o jogo
        // carrega/libera a colisao real dentro do ponteiro de cada modelo (varios SIGSEGV com enderecos aleatorios).
        static int made = 0;
        static LoadFn ver3 = reinterpret_cast<LoadFn>(CHook::getSym("_ZN11CFileLoader22LoadCollisionModelVer3EPhjR9CColModelPKc"));
        static CtorFn ctor = reinterpret_cast<CtorFn>(CHook::getSym("_ZN9CColModelC1Ev"));
        if (!ver3) { FLog("[COLNEW] colisao vazia: carregador Ver3 indisponivel"); return nullptr; }
        // dados COL3 (sem o cabecalho de 32 bytes): 88 bytes de cabecalho + 1 caixa (28 bytes); offsets relativos ao inicio+4... iguais aos de pref.col
        uint8_t file[148]; std::memset(file, 0, sizeof(file));
        std::memcpy(file, "COL3", 4); { const uint32_t sz = 140; std::memcpy(file + 4, &sz, 4); } std::memcpy(file + 8, "dummy", 5);
        uint8_t* blob = file + 32;   // o carregador nativo calcula os offsets a partir do inicio do arquivo (+4): precisa do arquivo inteiro na memoria
        const float hdr[10] = { -0.5f,-0.5f,-0.5f, 0.5f,0.5f,0.5f, 0,0,0, 0.9f };
        std::memcpy(blob, hdr, sizeof(hdr));   // blob = 88 bytes de cabecalho + 28 da caixa = 116
        const uint16_t ns = 0, nb = 1, nc = 0; std::memcpy(blob + 40, &ns, 2); std::memcpy(blob + 42, &nb, 2); std::memcpy(blob + 44, &nc, 2);
        const uint32_t fl = 2; std::memcpy(blob + 48, &fl, 4);
        const uint32_t hdrLen = 120, off = hdrLen - 4, bodyEnd = hdrLen + 28 - 4;
        const uint32_t offs[6] = { 0, off, 0, bodyEnd, bodyEnd, 0 }; std::memcpy(blob + 52, offs, sizeof(offs));
        const float box[6] = { -0.5f,-0.5f,-0.5f, 0.5f,0.5f,0.5f }; std::memcpy(blob + 88, box, sizeof(box));
        // memoria do PROPRIO jogo (CColModel::operator new): se o jogo chamar delete (CBaseModelInfo::DeleteCollisionModel) nao corrompe o heap
        using NewFn = void* (*)(size_t);
        static NewFn gnew = reinterpret_cast<NewFn>(CHook::getSym("_ZN9CColModelnwEm"));
        CColModel* cm = gnew ? static_cast<CColModel*>(gnew(sizeof(CColModel))) : nullptr;
        if (cm) std::memset(static_cast<void*>(cm), 0, sizeof(CColModel));
        else cm = static_cast<CColModel*>(std::calloc(1, sizeof(CColModel)));
        if (!cm) return nullptr;
        if (ctor) ctor(cm);
        cm->m_nColSlot = 0;
        ver3(blob, 116u, *cm, "dummy");
        if (!cm->m_pColData || cm->m_pColData->m_nNumBoxes == 0) { FLog("[COLNEW] colisao vazia: carregador nao produziu dados"); return nullptr; }
        if (made++ == 0) FLog("[COLNEW] colisao vazia (uma por modelo) criada para modelos sem colisao (LODs)");
        return cm;
    }

    inline void Run(const char* quando)
    {
        if (!g_pszStorage || !g_pszStorage[0]) return;
        const std::string root(g_pszStorage);
        FILE* lf = std::fopen((root + "SAMP/extra_cols.txt").c_str(), "r");
        if (!lf) { FLog("[COLNEW][%s] SAMP/extra_cols.txt nao existe: nada a fazer", quando); return; }

        static FindFn find = reinterpret_cast<FindFn>(CHook::getSym("_ZN10CModelInfo12GetModelInfoEPKcPi"));
        static LoadFn ver3 = reinterpret_cast<LoadFn>(CHook::getSym("_ZN11CFileLoader22LoadCollisionModelVer3EPhjR9CColModelPKc"));
        static LoadFn ver2 = reinterpret_cast<LoadFn>(CHook::getSym("_ZN11CFileLoader22LoadCollisionModelVer2EPhjR9CColModelPKc"));
        static CtorFn ctor = reinterpret_cast<CtorFn>(CHook::getSym("_ZN9CColModelC1Ev"));
        static CtorFn calcPlanes = reinterpret_cast<CtorFn>(CHook::getSym("_ZN10CCollision23CalculateTrianglePlanesEP9CColModel"));
        FLog("[COLNEW][%s] simbolos: GetModelInfo=%s Ver3=%s Ver2=%s CColModel::ctor=%s", quando, find ? "ok" : "FALTA", ver3 ? "ok" : "FALTA", ver2 ? "ok" : "FALTA", ctor ? "ok" : "FALTA(zero-init)");
        if (!find) { std::fclose(lf); return; }

        char line[256];
        while (std::fgets(line, sizeof(line), lf))
        {
            std::string n(line);
            while (!n.empty() && (n.back() == '\n' || n.back() == '\r' || n.back() == ' ' || n.back() == '\t')) n.pop_back();
            size_t s = 0; while (s < n.size() && (n[s] == ' ' || n[s] == '\t')) s++;
            n = n.substr(s);
            if (n.empty() || n[0] == '#') continue;
            const size_t sl = n.find_last_of("/\\");
            const std::string base = (sl == std::string::npos) ? n : n.substr(sl + 1);

            FILE* f = std::fopen((root + "SAMP/" + base).c_str(), "rb");
            if (!f) { FLog("[COLNEW] SAMP/%s nao existe: ignorado", base.c_str()); continue; }
            std::vector<uint8_t> buf;
            { uint8_t tmp[65536]; size_t r; while ((r = std::fread(tmp, 1, sizeof(tmp), f)) > 0) buf.insert(buf.end(), tmp, tmp + r); }
            std::fclose(f);

            std::vector<Entry> es; std::string why;
            if (!ParseEntries(buf.data(), buf.size(), es, &why)) { FLog("[COLNEW] %s: nenhuma entrada valida (%s)", base.c_str(), why.c_str()); continue; }
            FLog("[COLNEW] %s: %zu entrada(s) %s%s", base.c_str(), es.size(), why.empty() ? "" : "| aviso: ", why.c_str());

            for (const Entry& e : es)
            {
                int id = -1;
                CBaseModelInfo* mi = find(e.name.c_str(), &id);
                if (!mi || id < 0 || id >= (int)TOTAL_DFF_MODEL_IDS || CModelInfo::ms_modelInfoPtrs[id] != mi)
                { FLog("[COLNEW] '%s': sem ModelInfo (IDE nao declara esse nome): ignorado", e.name.c_str()); continue; }
                if (mi->m_pColModel)
                { FLog("[COLNEW] '%s' (id %d): ja tem colisao: mantida", e.name.c_str(), id); continue; }
                LoadFn fn = e.ver == 3 ? ver3 : ver2;
                if (!fn) { FLog("[COLNEW] '%s' (id %d): carregador nativo Ver%u indisponivel", e.name.c_str(), id, e.ver); Failed().insert(id); continue; }

                // CColModel e' 0x38 bytes. Memoria nossa (calloc), NUNCA marcada como "dona": o jogo nao a libera pelo pool.
                CColModel* cm = static_cast<CColModel*>(std::calloc(1, sizeof(CColModel)));
                if (!cm) { Failed().insert(id); continue; }
                if (ctor) ctor(cm);
                cm->m_nColSlot = 0;   // nivel 0 = fora do streaming de COL (igual a COLFILE 0)

                FLog("[COLNEW] carregando '%s' (id %d) COL%u: %zu bytes (se o jogo cair aqui, o log para nesta linha)", e.name.c_str(), id, e.ver, e.dataLen);
                fn(buf.data() + e.dataOff, (uint32_t)e.dataLen, *cm, e.name.c_str());

                if (!cm->m_pColData || (cm->GetTriCount() == 0 && cm->m_pColData->m_nNumBoxes == 0 && cm->m_pColData->m_nNumSpheres == 0))
                { FLog("[COLNEW] '%s' (id %d): carregador nao produziu dados (colData=%p): modelo fica SEM instancia", e.name.c_str(), id, (void*)cm->m_pColData); Failed().insert(id); continue; }
                // Planos dos triangulos: o COL em arquivo nao os traz; o jogo precisa deles em CCollision::ProcessColModels.
                FLog("[COLNEW] '%s' (id %d): planos antes=%p flags=0x%02x calcPlanes=%s", e.name.c_str(), id, (void*)cm->m_pColData->m_pTrianglePlanes, (unsigned)cm->m_nFlags, calcPlanes ? "ok" : "FALTA");
                if (cm->GetTriCount() > 0 && !cm->m_pColData->m_pTrianglePlanes && calcPlanes)
                {
                    calcPlanes(cm);
                    FLog("[COLNEW] '%s' (id %d): planos calculados -> %p", e.name.c_str(), id, (void*)cm->m_pColData->m_pTrianglePlanes);
                }
                mi->m_pColModel = cm;
                mi->bDoWeOwnTheColModel = 0;   // memoria nossa (calloc): DeleteCollisionModel nao pode liberar
                Attached().push_back(id);
                if (cm->GetTriCount() > 400)
                    FLog("[COLNEW][AVISO] '%s' (id %d): %u triangulos. O jogo guarda no maximo ~600 indices por consulta em CCollision::ProcessColModels: acima disso um veiculo/aviao perto corrompe a memoria e o jogo cai. Use caixas ou menos de ~300 triangulos.", e.name.c_str(), id, (unsigned)cm->GetTriCount());
                FLog("[COLNEW] '%s' (id %d): colisao ligada | triangulos=%u esferas=%u caixas=%u | raio=%.2f", e.name.c_str(), id,
                     (unsigned)cm->GetTriCount(), (unsigned)cm->m_pColData->m_nNumSpheres, (unsigned)cm->m_pColData->m_nNumBoxes, cm->GetBoundRadius());
            }
        }
        std::fclose(lf);
    }
}
