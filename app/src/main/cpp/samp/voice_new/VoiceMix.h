#pragma once
// Mixagem de volume do VoIP (cliente): volume geral + volume por jogador.
// Valores em porcentagem 0..200 (100 = normal, 0 = mudo). Aplicado nas amostras PCM antes de ir ao BASS
// (Channel::Push), entao nao mexe no volume do jogo nem no do radio/musica.
#include <atomic>
#include <algorithm>
#include <cstdint>
#include <cstddef>

namespace VoiceMix
{
    constexpr int kMaxPlayers = 1004;
    constexpr int kMaxPct = 200;
    constexpr int kLevels = 6;
    constexpr int kLevelPct[kLevels] = { 0, 35, 65, 100, 150, 200 };
    constexpr const char* kLevelName[kLevels] = { "Mudo", "Baixo", "Médio", "Padrão", "Alto", "Máximo" };

    inline std::atomic<int> master{100};
    inline std::atomic<int> playerDelta[kMaxPlayers];   // guarda (pct - 100): zero-inicializado = 100% (padrao)

    inline int GetMaster() { return master.load(); }
    inline void SetMaster(int pct) { master.store(std::clamp(pct, 0, kMaxPct)); }
    inline int GetPlayer(int id) { return (id >= 0 && id < kMaxPlayers) ? 100 + playerDelta[id].load() : 100; }
    inline void SetPlayer(int id, int pct) { if (id >= 0 && id < kMaxPlayers) playerDelta[id].store(std::clamp(pct, 0, kMaxPct) - 100); }
    inline void ResetPlayers() { for (int i = 0; i < kMaxPlayers; i++) playerDelta[i].store(0); }

    // indice do preset exato (ou kLevels = personalizado)
    inline int LevelOf(int pct) { for (int i = 0; i < kLevels; i++) if (kLevelPct[i] == pct) return i; return kLevels; }

    inline float Gain(int speaker) { return (float)GetMaster() / 100.0f * (float)GetPlayer(speaker) / 100.0f; }

    inline void Apply(int16_t* samples, size_t count, int speaker)
    {
        const float g = Gain(speaker);
        if (g > 0.995f && g < 1.005f) return;
        for (size_t i = 0; i < count; i++)
        {
            const float v = (float)samples[i] * g;
            samples[i] = (int16_t)std::clamp(v, -32768.0f, 32767.0f);
        }
    }
}
