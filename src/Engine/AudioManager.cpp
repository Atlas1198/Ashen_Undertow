//-------------------------------------------------
//
//          AudioManager.cpp
//
//  WAV を読み込み、XAudio2 で BGM と効果音を再生する。
//-------------------------------------------------

#include "AudioManager.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <objbase.h>

#pragma comment(lib, "xaudio2.lib")
#pragma comment(lib, "ole32.lib")

namespace
{
std::uint32_t ReadU32(const std::uint8_t* data)
{
    std::uint32_t value = 0;
    std::memcpy(&value, data, sizeof(value));
    return value;
}

bool IsChunk(const std::uint8_t* data, const char (&name)[5])
{
    return std::memcmp(data, name, 4) == 0;
}
}

AudioManager::~AudioManager()
{
    Shutdown();
}

// 音声デバイスを初期化し、使用する WAV を ID ごとに先読みする。
bool AudioManager::Initialize()
{
    Shutdown();
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(comResult) && comResult != RPC_E_CHANGED_MODE) return false;
    m_comInitialized = SUCCEEDED(comResult);
    if (FAILED(XAudio2Create(m_engine.GetAddressOf())))
    {
        Shutdown();
        return false;
    }
    if (FAILED(m_engine->CreateMasteringVoice(&m_masterVoice)))
    {
        Shutdown();
        return false;
    }
    m_available = true;

    bool loaded = true;
    loaded &= LoadWave(AudioId::TitleMusic,
        "assets/audio/music/title_dark_piano.wav");
    loaded &= LoadWave(AudioId::BattleMusic,
        "assets/audio/music/battle_orchestra.wav");
    loaded &= LoadWave(AudioId::Footstep1,
        "assets/audio/sfx/footstep_1.wav");
    loaded &= LoadWave(AudioId::Footstep2,
        "assets/audio/sfx/footstep_2.wav");
    loaded &= LoadWave(AudioId::Footstep3,
        "assets/audio/sfx/footstep_3.wav");
    loaded &= LoadWave(AudioId::Footstep4,
        "assets/audio/sfx/footstep_4.wav");
    loaded &= LoadWave(AudioId::SwordImpact,
        "assets/audio/sfx/sword_impact.wav");
    loaded &= LoadWave(AudioId::SpecialAttack,
        "assets/audio/sfx/special_attack.wav");
    loaded &= LoadWave(AudioId::EnemyLightning,
        "assets/audio/sfx/enemy_lightning.wav", 1.35f);
    loaded &= LoadWave(AudioId::BossFlame,
        "assets/audio/sfx/boss_flame.wav");
    loaded &= LoadWave(AudioId::PlayerDeath,
        "assets/audio/sfx/player_death.wav");
    loaded &= LoadWave(AudioId::Victory,
        "assets/audio/sfx/victory.wav");
    return loaded;
}

// 終了した効果音 Voice を解放して同時再生数の増え続けを防ぐ。
void AudioManager::Update()
{
    for (size_t i = m_effectVoices.size(); i-- > 0;)
    {
        XAUDIO2_VOICE_STATE state {};
        m_effectVoices[i]->GetState(&state);
        if (state.BuffersQueued != 0) continue;
        m_effectVoices[i]->DestroyVoice();
        m_effectVoices.erase(m_effectVoices.begin() + i);
    }
}

// 専用 Voice を入れ替え、BGM を重複再生しない。
void AudioManager::PlayMusic(AudioId id, float volume)
{
    if (!m_available || m_currentMusic == id) return;
    StopMusic();
    const SoundData* sound = Find(id);
    if (!sound) return;
    if (FAILED(m_engine->CreateSourceVoice(
        &m_musicVoice, &sound->format.Format))) return;

    XAUDIO2_BUFFER buffer {};
    buffer.AudioBytes = static_cast<UINT32>(sound->samples.size());
    buffer.pAudioData = sound->samples.data();
    buffer.LoopCount = XAUDIO2_LOOP_INFINITE;
    if (FAILED(m_musicVoice->SubmitSourceBuffer(&buffer)) ||
        FAILED(m_musicVoice->SetVolume((std::clamp)(volume, 0.0f, 1.0f))) ||
        FAILED(m_musicVoice->Start()))
    {
        m_musicVoice->DestroyVoice();
        m_musicVoice = nullptr;
        return;
    }
    m_currentMusic = id;
}

void AudioManager::StopMusic()
{
    if (m_musicVoice)
    {
        m_musicVoice->Stop();
        m_musicVoice->DestroyVoice();
        m_musicVoice = nullptr;
    }
    m_currentMusic = AudioId::Count;
}

// 効果音ごとに Voice を作るため、同じ音も重ねて鳴らせる。
void AudioManager::PlaySound(AudioId id, float volume, float pitch)
{
    const SoundData* sound = Find(id);
    if (!m_available || !sound) return;
    IXAudio2SourceVoice* voice = nullptr;
    if (FAILED(m_engine->CreateSourceVoice(&voice, &sound->format.Format))) return;

    XAUDIO2_BUFFER buffer {};
    buffer.Flags = XAUDIO2_END_OF_STREAM;
    buffer.AudioBytes = static_cast<UINT32>(sound->samples.size());
    buffer.pAudioData = sound->samples.data();
    buffer.PlayLength = sound->playLength;
    const float safeVolume = (std::clamp)(volume, 0.0f, 2.0f);
    const float safePitch = (std::clamp)(pitch, 0.5f, 2.0f);
    if (FAILED(voice->SubmitSourceBuffer(&buffer)) ||
        FAILED(voice->SetVolume(safeVolume)) ||
        FAILED(voice->SetFrequencyRatio(safePitch)) || FAILED(voice->Start()))
    {
        voice->DestroyVoice();
        return;
    }
    m_effectVoices.push_back(voice);
}

// RIFF チャンクからフォーマットと PCM データを取り出す。
bool AudioManager::LoadWave(AudioId id, const std::string& path, float maxSeconds)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    std::vector<std::uint8_t> fileBytes(
        (std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (fileBytes.size() < 12 || !IsChunk(fileBytes.data(), "RIFF") ||
        !IsChunk(fileBytes.data() + 8, "WAVE")) return false;

    SoundData& sound = m_sounds[static_cast<size_t>(id)];
    size_t offset = 12;
    bool hasFormat = false;
    while (offset + 8 <= fileBytes.size())
    {
        const std::uint32_t chunkSize = ReadU32(fileBytes.data() + offset + 4);
        const size_t chunkData = offset + 8;
        if (chunkData + chunkSize > fileBytes.size()) return false;
        if (IsChunk(fileBytes.data() + offset, "fmt "))
        {
            if (chunkSize < sizeof(WAVEFORMATEX) - sizeof(WORD)) return false;
            std::memset(&sound.format, 0, sizeof(sound.format));
            std::memcpy(&sound.format, fileBytes.data() + chunkData,
                (std::min)(static_cast<size_t>(chunkSize), sizeof(sound.format)));
            hasFormat = true;
        }
        else if (IsChunk(fileBytes.data() + offset, "data"))
        {
            sound.samples.assign(fileBytes.begin() + chunkData,
                fileBytes.begin() + chunkData + chunkSize);
        }
        offset = chunkData + chunkSize + (chunkSize & 1u);
    }
    sound.loaded = hasFormat && !sound.samples.empty();
    if (sound.loaded && maxSeconds > 0.0f)
    {
        sound.playLength = static_cast<std::uint32_t>(
            sound.format.Format.nSamplesPerSec * maxSeconds);
        const std::uint32_t availableFrames = static_cast<std::uint32_t>(
            sound.samples.size() / sound.format.Format.nBlockAlign);
        sound.playLength = (std::min)(sound.playLength, availableFrames);
    }
    return sound.loaded;
}

const AudioManager::SoundData* AudioManager::Find(AudioId id) const
{
    const size_t index = static_cast<size_t>(id);
    if (index >= m_sounds.size() || !m_sounds[index].loaded) return nullptr;
    return &m_sounds[index];
}

void AudioManager::Shutdown()
{
    StopMusic();
    for (IXAudio2SourceVoice* voice : m_effectVoices) voice->DestroyVoice();
    m_effectVoices.clear();
    if (m_masterVoice)
    {
        m_masterVoice->DestroyVoice();
        m_masterVoice = nullptr;
    }
    m_engine.Reset();
    m_available = false;
    if (m_comInitialized)
    {
        CoUninitialize();
        m_comInitialized = false;
    }
}
