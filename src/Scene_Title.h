//-------------------------------------------------
//
//          Scene_Title.h
//
//  タイトル画面の選択状態と遷移を管理する。
//-------------------------------------------------

#pragma once

#include "SceneManager.h"
#include "UIManager.h"

class Input;
class AudioManager;

// タイトルの選択状態と、フェード後の遷移要求を管理する。
class Scene_Title final : public SceneManager
{
public:
    Scene_Title(ModelManager& modelManager, const Input& input, AudioManager& audio)
        : SceneManager(modelManager), m_input(input), m_audio(audio) {}

    bool Initialize() override;
    bool InitializeUI(UIManager& ui) override;

private:
    void OnUpdate(float deltaTime) override;
    void OnDrawUI(UIManager& ui) override;

    const Input& m_input;
    AudioManager& m_audio;
    UIImageHandle m_backgroundTexture;
    UIImageHandle m_titleTexture;
    UIImageHandle m_tutorialTexture;
    UIImageHandle m_gameTexture;
    float m_time = 0.0f;
    float m_startFade = 0.0f;
    int m_selectedItem = 0;
    // 決定時の遷移先を保存し、フェード完了後に Manager へ通知する。
    SceneRequest m_startRequest = SceneRequest::None;
    bool m_starting = false;
};
