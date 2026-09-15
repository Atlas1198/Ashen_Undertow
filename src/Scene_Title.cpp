#include "Scene_Title.h"

#include "Input.h"

#include <algorithm>
#include <array>
#include <cmath>

bool Scene_Title::Initialize()
{
    return true;
}

bool Scene_Title::InitializeUI(UIManager& ui)
{
    m_backgroundTexture = ui.LoadImageFile(
        "assets/texture/ui/title_background.png");
    m_titleTexture = ui.LoadImageFile(
        "assets/texture/ui/ashen_undertow.png");
    m_pressEnterTexture = ui.LoadImageFile(
        "assets/texture/ui/press_enter.png");
    return m_backgroundTexture.IsValid() && m_titleTexture.IsValid() &&
        m_pressEnterTexture.IsValid();
}

void Scene_Title::OnUpdate(float deltaTime)
{
    m_time += deltaTime;
    if (!m_starting)
    {
        if (m_input.ConfirmPressed())
            m_starting = true;
        return;
    }

    m_startFade = (std::min)(1.0f, m_startFade + deltaTime / 1.0f);
    if (m_startFade >= 1.0f)
        RequestScene(SceneRequest::Game);
}

void Scene_Title::OnDrawUI(UIManager& ui)
{
    constexpr float ScreenWidth = 1600.0f;
    constexpr float ScreenHeight = 900.0f;
    const std::array<DirectX::XMFLOAT2, 4> screen {
        DirectX::XMFLOAT2 { 0.0f, 0.0f },
        DirectX::XMFLOAT2 { ScreenWidth, 0.0f },
        DirectX::XMFLOAT2 { ScreenWidth, ScreenHeight },
        DirectX::XMFLOAT2 { 0.0f, ScreenHeight }
    };
    ui.DrawImage(m_backgroundTexture,
        { 0.0f, 0.0f, ScreenWidth, ScreenHeight });
    ui.DrawGradientQuad(screen, {
        DirectX::XMFLOAT4 { 0.0f, 0.0f, 0.0f, 0.16f },
        DirectX::XMFLOAT4 { 0.0f, 0.0f, 0.0f, 0.16f },
        DirectX::XMFLOAT4 { 0.0f, 0.0f, 0.0f, 0.30f },
        DirectX::XMFLOAT4 { 0.0f, 0.0f, 0.0f, 0.30f }
    });

    const float titleAlpha = std::clamp(m_time / 1.0f, 0.0f, 1.0f);
    ui.DrawImage(m_titleTexture, { 250.0f, 120.0f, 1100.0f, 367.0f },
        { 1.0f, 1.0f, 1.0f, titleAlpha });
    const float pulse = 0.18f + 0.82f *
        (0.5f + 0.5f * std::sin(m_time * 2.35f));
    ui.DrawImage(m_pressEnterTexture, { 565.0f, 655.0f, 470.0f, 157.0f },
        { 1.0f, 1.0f, 1.0f, titleAlpha * pulse * (1.0f - m_startFade) });

    if (m_startFade > 0.0f)
        ui.DrawQuad(screen, { 0.0f, 0.0f, 0.0f, m_startFade });
}
