//-------------------------------------------------
//
//          AudioManager.h
//
//  WAV の読み込みと XAudio2 による音声再生を管理する。
//-------------------------------------------------

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <wrl/client.h>
#include <xaudio2.h>

// BGM と効果音を固定 ID で参照し、パスをゲーム処理から分離する。
enum class AudioId : std::uint8_t
{
    TitleMusic,
    BattleMusic,
    Footstep1,
    Footstep2,
    Footstep3,
    Footstep4,
    SwordImpact,
    SpecialAttack,
    EnemyLightning,
    BossFlame,
    PlayerDeath,
    Victory,
    Count
};

// XAudio2 の音声資源と再生中 Voice を所有する。
class AudioManager
{
public:
    AudioManager() = default;
    ~AudioManager();
    AudioManager(const AudioManager&) = delete;
    AudioManager& operator=(const AudioManager&) = delete;

    bool Initialize();
    // 再生を終えた効果音 Voice を回収する。
    void Update();
    void PlayMusic(AudioId id, float volume = 0.38f);
    void StopMusic();
    void PlaySound(AudioId id, float volume = 1.0f, float pitch = 1.0f);
    bool IsAvailable() const { return m_available; }

private:
    struct SoundData
    {
        WAVEFORMATEXTENSIBLE format {};
        std::vector<std::uint8_t> samples;
        std::uint32_t playLength = 0;
        bool loaded = false;
    };

    bool LoadWave(AudioId id, const std::string& path, float maxSeconds = 0.0f);
    const SoundData* Find(AudioId id) const;
    void Shutdown();

    Microsoft::WRL::ComPtr<IXAudio2> m_engine;
    IXAudio2MasteringVoice* m_masterVoice = nullptr;
    IXAudio2SourceVoice* m_musicVoice = nullptr;
    // 同時再生中の一時 Voice。BGM は専用 Voice で切り替える。
    std::vector<IXAudio2SourceVoice*> m_effectVoices;
    std::array<SoundData, static_cast<size_t>(AudioId::Count)> m_sounds;
    AudioId m_currentMusic = AudioId::Count;
    bool m_available = false;
    bool m_comInitialized = false;
};
