#include "GameHud.h"

#include "Boss.h"
#include "Camera.h"
#include "MiniCopy.h"
#include "Player.h"
#include "UIManager.h"

#include <algorithm>

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

    DrawBar(ui, { 24.0f, 860.0f, 360.0f, 16.0f, 2.0f,
        { 0.6f, 1.0f, 0.05f, 1.0f } }, Ratio(player.GetHP(), player.GetMaxHP()));
    const float staminaRatio = player.GetMaxStamina() > 0.0f
        ? player.GetStamina() / player.GetMaxStamina() : 0.0f;
    DrawBar(ui, { 24.0f, 881.0f, 360.0f, 8.0f, 1.0f,
        { 1.0f, 0.82f, 0.05f, 1.0f } }, staminaRatio);
}
