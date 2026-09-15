#include "Scene_Title.h"

#include "Input.h"
#include "AudioManager.h"

#include <algorithm>
#include <array>
#include <cmath>

bool Scene_Title::Initialize()
{
    m_audio.PlayMusic(AudioId::TitleMusic, 0.34f);
    return true;
}

bool Scene_Title::InitializeUI(UIManager& ui)
{
    m_backgroundTexture = ui.LoadImageFile(
        "assets/texture/ui/title_background.png");
    m_titleTexture = ui.LoadImageFile(
        "assets/texture/ui/ashen_undertow.png");
    m_tutorialTexture = ui.LoadImageFile(
        "assets/texture/ui/tutorial.png");
    m_gameTexture = ui.LoadImageFile(
        "assets/texture/ui/game.png");
    return m_backgroundTexture.IsValid() && m_titleTexture.IsValid() &&
        m_tutorialTexture.IsValid() && m_gameTexture.IsValid();
}

void Scene_Title::OnUpdate(float deltaTime)
{
    m_time += deltaTime;
    if (!m_starting)
    {
        if (m_input.MenuUpPressed() && m_selectedItem != 0)
            m_selectedItem = 0;
        if (m_input.MenuDownPressed() && m_selectedItem != 1)
            m_selectedItem = 1;
        if (m_input.ConfirmPressed())
        {
            m_startRequest = m_selectedItem == 0
                ? SceneRequest::Tutorial : SceneRequest::Game;
            m_starting = true;
        }
        return;
    }

    m_startFade = (std::min)(1.0f, m_startFade + deltaTime / 1.0f);
    if (m_startFade >= 1.0f)
        RequestScene(m_startRequest);
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
    const float pulse = 0.55f + 0.45f *
        (0.5f + 0.5f * std::sin(m_time * 2.35f));
    constexpr float ItemY[] = { 610.0f, 735.0f };
    const UIImageHandle labels[] = { m_tutorialTexture, m_gameTexture };
    for (int i = 0; i < 2; ++i)
    {
        const bool selected = i == m_selectedItem;
        const float width = selected ? 380.0f : 350.0f;
        const float height = width / 3.0f;
        if (selected)
        {
            constexpr float GlowSize = 12.0f;
            ui.DrawImage(labels[i],
                { (ScreenWidth - width) * 0.5f - GlowSize * 0.5f,
                    ItemY[i] - GlowSize * 0.5f,
                    width + GlowSize, height + GlowSize },
                { 0.05f, 0.75f, 1.0f, titleAlpha * (0.20f + 0.16f * pulse) });
        }
        const float alpha = selected ? 0.90f + 0.10f * pulse : 0.70f;
        ui.DrawImage(labels[i],
            { (ScreenWidth - width) * 0.5f, ItemY[i], width, height },
            { selected ? 0.88f : 0.78f, selected ? 1.0f : 0.84f,
                1.0f, titleAlpha * alpha });
    }

    if (m_startFade > 0.0f)
        ui.DrawQuad(screen, { 0.0f, 0.0f, 0.0f, m_startFade });
}
