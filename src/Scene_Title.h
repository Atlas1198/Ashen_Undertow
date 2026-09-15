#pragma once

#include "SceneManager.h"
#include "UIManager.h"

class Input;

class Scene_Title final : public SceneManager
{
public:
    Scene_Title(ModelManager& modelManager, const Input& input)
        : SceneManager(modelManager), m_input(input) {}

    bool Initialize() override;
    bool InitializeUI(UIManager& ui) override;

private:
    void OnUpdate(float deltaTime) override;
    void OnDrawUI(UIManager& ui) override;

    const Input& m_input;
    UIImageHandle m_backgroundTexture;
    UIImageHandle m_titleTexture;
    UIImageHandle m_tutorialTexture;
    UIImageHandle m_gameTexture;
    float m_time = 0.0f;
    float m_startFade = 0.0f;
    int m_selectedItem = 0;
    SceneRequest m_startRequest = SceneRequest::None;
    bool m_starting = false;
};
