#include "GameHud.h"

#include "Boss.h"
#include "Camera.h"
#include "MiniCopy.h"
#include "Player.h"
#include "UIManager.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr float ScreenWidth = 1600.0f;
constexpr float ScreenHeight = 900.0f;

struct BarStyle
{
    float x;
    float y;
    float width;
    float height;
    float padding;
    DirectX::XMFLOAT4 fillColor;
};

void DrawBar(UIManager& ui, const BarStyle& style, float ratio)
{
    ratio = std::clamp(ratio, 0.0f, 1.0f);
    ui.DrawQuad({
        DirectX::XMFLOAT2 { style.x, style.y },
        DirectX::XMFLOAT2 { style.x + style.width, style.y },
        DirectX::XMFLOAT2 { style.x + style.width, style.y + style.height },
        DirectX::XMFLOAT2 { style.x, style.y + style.height }
    }, { 0.0f, 0.0f, 0.0f, 1.0f });

    const float fillX = style.x + style.padding;
    const float fillY = style.y + style.padding;
    const float fillHeight = style.height - style.padding * 2.0f;
    const float innerWidth = style.width - style.padding * 2.0f;
    ui.DrawQuad({
        DirectX::XMFLOAT2 { fillX, fillY },
        DirectX::XMFLOAT2 { fillX + innerWidth, fillY },
        DirectX::XMFLOAT2 { fillX + innerWidth, fillY + fillHeight },
        DirectX::XMFLOAT2 { fillX, fillY + fillHeight }
    }, { 0.035f, 0.045f, 0.055f, 0.92f });

    const float fillWidth = innerWidth * ratio;
    if (fillWidth <= 0.0f) return;
    ui.DrawQuad({
        DirectX::XMFLOAT2 { fillX, fillY },
        DirectX::XMFLOAT2 { fillX + fillWidth, fillY },
        DirectX::XMFLOAT2 { fillX + fillWidth, fillY + fillHeight },
        DirectX::XMFLOAT2 { fillX, fillY + fillHeight }
    }, style.fillColor);
    const float highlightHeight = (std::max)(1.0f, fillHeight * 0.22f);
    ui.DrawQuad({
        DirectX::XMFLOAT2 { fillX, fillY },
        DirectX::XMFLOAT2 { fillX + fillWidth, fillY },
        DirectX::XMFLOAT2 { fillX + fillWidth, fillY + highlightHeight },
        DirectX::XMFLOAT2 { fillX, fillY + highlightHeight }
    }, { (std::min)(1.0f, style.fillColor.x + 0.25f),
        (std::min)(1.0f, style.fillColor.y + 0.25f),
        (std::min)(1.0f, style.fillColor.z + 0.25f), 0.75f });
}

bool WorldToScreen(const DirectX::XMFLOAT3& world, const Camera& camera,
    DirectX::XMFLOAT2& screen)
{
    using namespace DirectX;
    const auto cameraPosition = camera.GetPosition();
    const auto cameraTarget = camera.GetTarget();
    const XMVECTOR eye = XMLoadFloat3(&cameraPosition);
    const XMVECTOR target = XMLoadFloat3(&cameraTarget);
    const XMVECTOR point = XMLoadFloat3(&world);
    const XMVECTOR forward = XMVector3Normalize(XMVectorSubtract(target, eye));
    if (XMVectorGetX(XMVector3Dot(XMVectorSubtract(point, eye), forward)) <= 0.0f)
        return false;

    const XMMATRIX view = XMMatrixLookAtLH(
        eye, target, XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
    const XMMATRIX projection = XMMatrixPerspectiveFovLH(
        XMConvertToRadians(60.0f), ScreenWidth / ScreenHeight, 0.5f, 100.0f);
    XMFLOAT3 projected;
    XMStoreFloat3(&projected, XMVector3Project(point, 0.0f, 0.0f,
        ScreenWidth, ScreenHeight, 0.0f, 1.0f,
        projection, view, XMMatrixIdentity()));
    if (projected.z < 0.0f || projected.z > 1.0f) return false;
    screen = { projected.x, projected.y };
    return projected.x >= 0.0f && projected.x <= ScreenWidth &&
        projected.y >= 0.0f && projected.y <= ScreenHeight;
}

float Ratio(int value, int maximum)
{
    return maximum > 0 ? static_cast<float>(value) / maximum : 0.0f;
}

void DrawSpecialGauge(UIManager& ui, float ratio, float elapsedTime)
{
    ratio = std::clamp(ratio, 0.0f, 1.0f);
    constexpr DirectX::XMFLOAT2 center { 1480.0f, 785.0f };
    constexpr float innerRadius = 34.0f;
    constexpr float outerRadius = 61.0f;
    constexpr int segments = 12;
    const bool full = ratio >= 1.0f;
    const float pulse = full
        ? 0.78f + 0.22f * std::sin(elapsedTime * 5.0f) : 1.0f;
    for (int i = 0; i < segments; ++i)
    {
        constexpr float Gap = 0.045f;
        const float a0 = -DirectX::XM_PIDIV2 + DirectX::XM_2PI *
            static_cast<float>(i) / segments + Gap;
        const float a1 = -DirectX::XM_PIDIV2 + DirectX::XM_2PI *
            static_cast<float>(i + 1) / segments - Gap;
        const auto point = [&](float angle, float radius)
        {
            return DirectX::XMFLOAT2 { center.x + std::cos(angle) * radius,
                center.y + std::sin(angle) * radius };
        };
        const bool active = static_cast<float>(i) / segments < ratio;
        const DirectX::XMFLOAT4 color = active
            ? DirectX::XMFLOAT4 { 0.05f, 0.72f * pulse, 1.0f * pulse, 1.0f }
            : DirectX::XMFLOAT4 { 0.025f, 0.08f, 0.12f, 0.82f };
        ui.DrawQuad({ point(a0, outerRadius), point(a1, outerRadius),
            point(a1, innerRadius), point(a0, innerRadius) }, color);
    }

}
}

bool GameHud::InitializeUI(UIManager& ui)
{
    m_vitalityIcon = ui.LoadImageFile("assets/texture/ui/hud_vitality_icon.png");
    m_staminaIcon = ui.LoadImageFile("assets/texture/ui/hud_stamina_icon.png");
    m_ultimateIcon = ui.LoadImageFile("assets/texture/ui/hud_ultimate_icon.png");
    m_bossIcon = ui.LoadImageFile("assets/texture/ui/hud_boss_icon.png");
    return m_vitalityIcon.IsValid() && m_staminaIcon.IsValid() &&
        m_ultimateIcon.IsValid() && m_bossIcon.IsValid();
}

void GameHud::Update(const Player& player, float deltaTime)
{
    m_elapsedTime += deltaTime;
    const int currentHp = player.GetHP();
    if (m_previousPlayerHp >= 0 && currentHp < m_previousPlayerHp)
        m_playerDamageEffectTimer = 0.38f;
    m_previousPlayerHp = currentHp;
    m_playerDamageEffectTimer = (std::max)(
        0.0f, m_playerDamageEffectTimer - deltaTime);
}

void GameHud::Draw(UIManager& ui, const Player& player, const Boss& boss,
    const std::vector<MiniCopy*>& miniCopies, const Camera& camera) const
{
    ui.DrawImage(m_bossIcon, { 774.0f, 4.0f, 52.0f, 52.0f });
    DrawBar(ui, { 500.0f, 58.0f, 600.0f, 22.0f, 3.0f,
        { 0.78f, 0.025f, 0.04f, 1.0f } }, Ratio(boss.GetHP(), boss.GetMaxHP()));

    for (const MiniCopy* miniCopy : miniCopies)
    {
        if (miniCopy->IsDead()) continue;
        DirectX::XMFLOAT2 center;
        if (!WorldToScreen(miniCopy->GetHPBarPosition(), camera, center)) continue;
        DrawBar(ui, { center.x - 48.0f, center.y - 9.0f, 96.0f, 9.0f, 1.5f,
            { 0.9f, 0.1f, 0.1f, 1.0f } },
            Ratio(miniCopy->GetHP(), miniCopy->GetMaxHP()));
    }

    const float specialRatio = player.GetMaxSpecialGauge() > 0.0f
        ? player.GetSpecialGauge() / player.GetMaxSpecialGauge() : 0.0f;
    DrawSpecialGauge(ui, specialRatio, m_elapsedTime);
    ui.DrawImage(m_ultimateIcon, { 1455.0f, 760.0f, 50.0f, 50.0f },
        { 1.0f, 1.0f, 1.0f, 0.38f + specialRatio * 0.62f });

    constexpr float DamageEffectDuration = 0.38f;
    const float damageEffect = std::clamp(
        m_playerDamageEffectTimer / DamageEffectDuration, 0.0f, 1.0f);
    const float shake = std::sin(m_playerDamageEffectTimer * 115.0f) *
        7.0f * damageEffect;
    const float pulse = std::sin(damageEffect * DirectX::XM_PI) * 3.0f;
    const DirectX::XMFLOAT4 playerHpColor{
        0.6f + 0.4f * damageEffect,
        1.0f - 0.92f * damageEffect,
        0.05f - 0.03f * damageEffect,
        1.0f
    };
    ui.DrawImage(m_vitalityIcon, { 20.0f + shake, 784.0f, 54.0f, 54.0f });
    DrawBar(ui, { 84.0f + shake, 800.0f - pulse,
        400.0f, 24.0f + pulse * 2.0f, 3.0f, playerHpColor },
        Ratio(player.GetHP(), player.GetMaxHP()));
    const float staminaRatio = player.GetMaxStamina() > 0.0f
        ? player.GetStamina() / player.GetMaxStamina() : 0.0f;
    ui.DrawImage(m_staminaIcon, { 25.0f, 840.0f, 44.0f, 44.0f });
    DrawBar(ui, { 84.0f, 854.0f, 340.0f, 14.0f, 2.0f,
        { 1.0f, 0.82f, 0.05f, 1.0f } }, staminaRatio);
}
