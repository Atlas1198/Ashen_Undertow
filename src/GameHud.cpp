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

    const float fillWidth = (style.width - style.padding * 2.0f) * ratio;
    if (fillWidth <= 0.0f) return;
    const float fillX = style.x + style.padding;
    const float fillY = style.y + style.padding;
    const float fillHeight = style.height - style.padding * 2.0f;
    ui.DrawQuad({
        DirectX::XMFLOAT2 { fillX, fillY },
        DirectX::XMFLOAT2 { fillX + fillWidth, fillY },
        DirectX::XMFLOAT2 { fillX + fillWidth, fillY + fillHeight },
        DirectX::XMFLOAT2 { fillX, fillY + fillHeight }
    }, style.fillColor);
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

DirectX::XMFLOAT4 RainbowColor(float hue)
{
    const float r = std::clamp(std::abs(hue * 6.0f - 3.0f) - 1.0f, 0.0f, 1.0f);
    const float g = std::clamp(2.0f - std::abs(hue * 6.0f - 2.0f), 0.0f, 1.0f);
    const float b = std::clamp(2.0f - std::abs(hue * 6.0f - 4.0f), 0.0f, 1.0f);
    constexpr float pastelAmount = 0.48f;
    return {
        r * (1.0f - pastelAmount) + pastelAmount,
        g * (1.0f - pastelAmount) + pastelAmount,
        b * (1.0f - pastelAmount) + pastelAmount,
        1.0f
    };
}

void DrawSpecialGauge(UIManager& ui, float ratio)
{
    constexpr float x = 24.0f;
    constexpr float y = 844.0f;
    constexpr float width = 360.0f;
    constexpr float height = 10.0f;
    constexpr float padding = 1.0f;
    constexpr int segments = 24;
    ui.DrawQuad({
        DirectX::XMFLOAT2 { x, y }, DirectX::XMFLOAT2 { x + width, y },
        DirectX::XMFLOAT2 { x + width, y + height }, DirectX::XMFLOAT2 { x, y + height }
    }, { 0.0f, 0.0f, 0.0f, 1.0f });

    ratio = std::clamp(ratio, 0.0f, 1.0f);
    const float innerWidth = width - padding * 2.0f;
    for (int i = 0; i < segments; ++i)
    {
        const float start = static_cast<float>(i) / segments;
        if (start >= ratio) break;
        const float end = (std::min)(
            static_cast<float>(i + 1) / segments, ratio);
        const float left = x + padding + innerWidth * start;
        const float right = x + padding + innerWidth * end;
        const auto leftColor = RainbowColor(start);
        const auto rightColor = RainbowColor(end);
        ui.DrawGradientQuad({
            DirectX::XMFLOAT2 { left, y + padding },
            DirectX::XMFLOAT2 { right, y + padding },
            DirectX::XMFLOAT2 { right, y + height - padding },
            DirectX::XMFLOAT2 { left, y + height - padding }
        }, { leftColor, rightColor, rightColor, leftColor });
    }
}
}

void GameHud::Update(const Player& player, float deltaTime)
{
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
    DrawBar(ui, { 550.0f, 24.0f, 500.0f, 16.0f, 2.0f,
        { 0.9f, 0.05f, 0.05f, 1.0f } }, Ratio(boss.GetHP(), boss.GetMaxHP()));

    for (const MiniCopy* miniCopy : miniCopies)
    {
        if (miniCopy->IsDead()) continue;
        DirectX::XMFLOAT2 center;
        if (!WorldToScreen(miniCopy->GetHPBarPosition(), camera, center)) continue;
        DrawBar(ui, { center.x - 45.0f, center.y - 8.0f, 90.0f, 8.0f, 1.0f,
            { 0.9f, 0.1f, 0.1f, 1.0f } },
            Ratio(miniCopy->GetHP(), miniCopy->GetMaxHP()));
    }

    const float specialRatio = player.GetMaxSpecialGauge() > 0.0f
        ? player.GetSpecialGauge() / player.GetMaxSpecialGauge() : 0.0f;
    DrawSpecialGauge(ui, specialRatio);

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
    DrawBar(ui, { 24.0f + shake, 860.0f - pulse,
        360.0f, 16.0f + pulse * 2.0f, 2.0f, playerHpColor },
        Ratio(player.GetHP(), player.GetMaxHP()));
    const float staminaRatio = player.GetMaxStamina() > 0.0f
        ? player.GetStamina() / player.GetMaxStamina() : 0.0f;
    DrawBar(ui, { 24.0f, 881.0f, 360.0f, 8.0f, 1.0f,
        { 1.0f, 0.82f, 0.05f, 1.0f } }, staminaRatio);
}
