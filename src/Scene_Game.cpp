#include "Scene_Game.h"

#include "Boss.h"
#include "MiniCopy.h"
#include "Player.h"
#include "Tree.h"
#include "Collider.h"
#include "Input.h"

#include "Renderer.h"
#include "Terrain.h"
#include <algorithm>
#include <cmath>

namespace
{
constexpr float ScreenWidth = 1600.0f;
constexpr float ScreenHeight = 900.0f;
constexpr float BossHPBarX = 550.0f;
constexpr float BossHPBarY = 24.0f;
constexpr float BossHPBarWidth = 500.0f;
constexpr float BossHPBarHeight = 16.0f;
constexpr float BossHPBarPadding = 2.0f;
constexpr float MiniCopyHPBarWidth = 90.0f;
constexpr float MiniCopyHPBarHeight = 8.0f;
constexpr float MiniCopyHPBarPadding = 1.0f;
constexpr int MaxMiniCopies = 4;
constexpr float PlayerHPBarX = 24.0f;
constexpr float PlayerHPBarY = 860.0f;
constexpr float PlayerHPBarWidth = 360.0f;
constexpr float PlayerHPBarHeight = 16.0f;
constexpr float PlayerHPBarPadding = 2.0f;
constexpr float PlayerStaminaBarX = PlayerHPBarX;
constexpr float PlayerStaminaBarY = PlayerHPBarY + PlayerHPBarHeight + 5.0f;
constexpr float PlayerStaminaBarWidth = PlayerHPBarWidth;
constexpr float PlayerStaminaBarHeight = 8.0f;
constexpr float PlayerStaminaBarPadding = 1.0f;

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
}

Scene_Game::Scene_Game(ModelManager& modelManager, const Input& input)
    : SceneManager(modelManager), m_input(input)
{
}

bool Scene_Game::Initialize()
{
    m_player = AddGameObject<Player>(m_input);
    if (!m_player) return false;

    m_boss = AddGameObject<Boss>();
    if (!m_boss) return false;
    const float bossScale = m_boss->GetVisualScale();
    const float bossZ = Terrain::HalfSize + bossScale * 0.55f;
    const float bossY = Terrain::HeightAt(0.0f, Terrain::HalfSize)
        - bossScale * 0.9f;
    m_boss->SetPosition(0.0f, bossY, bossZ);
    m_boss->SetRotation(0.0f, DirectX::XM_PI, 0.0f);

    constexpr int TreeCount = 36;
    constexpr int TreesPerRing = TreeCount / 2;
    constexpr float BossClearingHalfWidth = 18.0f;
    constexpr float BossClearingMinZ = 30.0f;
    m_trees.reserve(TreeCount);
    for (int i = 0; i < TreeCount; ++i)
    {
        const float index = static_cast<float>(i);
        const float radialNoise = std::sin(index * 12.9898f) * 43758.5453f;
        const float scaleNoise = std::sin(index * 31.4159f + 2.7f) * 15731.743f;
        const float radiusRandom = radialNoise - std::floor(radialNoise);
        const float scaleRandom = scaleNoise - std::floor(scaleNoise);
        const int ring = i % 2;
        const float slot = static_cast<float>(i / 2);
        const float angle = DirectX::XM_2PI
            * (slot + static_cast<float>(ring) * 0.5f)
            / static_cast<float>(TreesPerRing)
            + (radiusRandom - 0.5f) * 0.05f;
        const float radius = 40.5f + static_cast<float>(ring) * 4.0f
            + radiusRandom * 1.2f;
        const float x = std::cos(angle) * radius;
        const float z = std::sin(angle) * radius;
        if (z >= BossClearingMinZ && std::abs(x) <= BossClearingHalfWidth)
            continue;

        Tree* tree = AddGameObject<Tree>();
        if (!tree) return false;
        const float visualScale = 1.55f + scaleRandom * 0.35f;
        const float groundSink = visualScale * 0.3f;
        tree->SetPosition(x, Terrain::HeightAt(x, z) - groundSink, z);
        tree->SetVisualScale(visualScale);
        m_trees.push_back(tree);
    }
    m_previousPlayerPosition = m_player->GetPosition();
    return true;
}

bool Scene_Game::InitializeUI(UIManager& ui)
{
    m_wonheeImage = ui.LoadImageFile("assets/texture/wonhee.jpg");
    const auto* image = ui.GetImage(m_wonheeImage);
    if (!image) return false;
    m_wonheeHeight = 240.0f * image->height / image->width;
    return true;
}

void Scene_Game::OnDrawUI(UIManager& ui)
{
    //ui.DrawImage(m_wonheeImage, { 24, 24, 240, m_wonheeHeight });
    //// 4頂点を画面ピクセル座標で手打ちした四角形。
    //ui.DrawQuad({ DirectX::XMFLOAT2 { 300, 40 }, { 500, 40 },
    //    { 500, 180 }, { 300, 180 } }, { 0.1f, 0.7f, 1.0f, 0.75f });

    const float hpRatio = m_boss->GetMaxHP() > 0
        ? static_cast<float>(m_boss->GetHP()) / m_boss->GetMaxHP() : 0.0f;
    ui.DrawQuad({
        DirectX::XMFLOAT2 { BossHPBarX, BossHPBarY },
        DirectX::XMFLOAT2 { BossHPBarX + BossHPBarWidth, BossHPBarY },
        DirectX::XMFLOAT2 { BossHPBarX + BossHPBarWidth, BossHPBarY + BossHPBarHeight },
        DirectX::XMFLOAT2 { BossHPBarX, BossHPBarY + BossHPBarHeight }
    }, { 0.0f, 0.0f, 0.0f, 1.0f });

    const float fillX = BossHPBarX + BossHPBarPadding;
    const float fillY = BossHPBarY + BossHPBarPadding;
    const float fillWidth = (BossHPBarWidth - BossHPBarPadding * 2.0f) * hpRatio;
    const float fillHeight = BossHPBarHeight - BossHPBarPadding * 2.0f;
    if (fillWidth > 0.0f)
    {
        ui.DrawQuad({
            DirectX::XMFLOAT2 { fillX, fillY },
            DirectX::XMFLOAT2 { fillX + fillWidth, fillY },
            DirectX::XMFLOAT2 { fillX + fillWidth, fillY + fillHeight },
            DirectX::XMFLOAT2 { fillX, fillY + fillHeight }
        }, { 0.9f, 0.05f, 0.05f, 1.0f });
    }

    for (const MiniCopy* miniCopy : m_miniCopies)
    {
        if (miniCopy->IsDead()) continue;
        const auto headPosition = miniCopy->GetHPBarPosition();
        DirectX::XMFLOAT2 center;
        if (!WorldToScreen(headPosition, m_camera, center)) continue;

        const float x = center.x - MiniCopyHPBarWidth * 0.5f;
        const float y = center.y - MiniCopyHPBarHeight;
        ui.DrawQuad({
            DirectX::XMFLOAT2 { x, y },
            DirectX::XMFLOAT2 { x + MiniCopyHPBarWidth, y },
            DirectX::XMFLOAT2 { x + MiniCopyHPBarWidth, y + MiniCopyHPBarHeight },
            DirectX::XMFLOAT2 { x, y + MiniCopyHPBarHeight }
        }, { 0.0f, 0.0f, 0.0f, 0.9f });

        const float miniRatio = miniCopy->GetMaxHP() > 0
            ? static_cast<float>(miniCopy->GetHP()) / miniCopy->GetMaxHP() : 0.0f;
        const float miniFillWidth =
            (MiniCopyHPBarWidth - MiniCopyHPBarPadding * 2.0f) * miniRatio;
        if (miniFillWidth <= 0.0f) continue;
        const float miniFillX = x + MiniCopyHPBarPadding;
        const float miniFillY = y + MiniCopyHPBarPadding;
        const float miniFillHeight = MiniCopyHPBarHeight - MiniCopyHPBarPadding * 2.0f;
        ui.DrawQuad({
            DirectX::XMFLOAT2 { miniFillX, miniFillY },
            DirectX::XMFLOAT2 { miniFillX + miniFillWidth, miniFillY },
            DirectX::XMFLOAT2 { miniFillX + miniFillWidth, miniFillY + miniFillHeight },
            DirectX::XMFLOAT2 { miniFillX, miniFillY + miniFillHeight }
        }, { 0.9f, 0.1f, 0.1f, 1.0f });
    }

    const float playerHpRatio = m_player->GetMaxHP() > 0
        ? static_cast<float>(m_player->GetHP()) / m_player->GetMaxHP() : 0.0f;
    ui.DrawQuad({
        DirectX::XMFLOAT2 { PlayerHPBarX, PlayerHPBarY },
        DirectX::XMFLOAT2 { PlayerHPBarX + PlayerHPBarWidth, PlayerHPBarY },
        DirectX::XMFLOAT2 { PlayerHPBarX + PlayerHPBarWidth, PlayerHPBarY + PlayerHPBarHeight },
        DirectX::XMFLOAT2 { PlayerHPBarX, PlayerHPBarY + PlayerHPBarHeight }
    }, { 0.0f, 0.0f, 0.0f, 1.0f });

    const float playerFillX = PlayerHPBarX + PlayerHPBarPadding;
    const float playerFillY = PlayerHPBarY + PlayerHPBarPadding;
    const float playerFillWidth =
        (PlayerHPBarWidth - PlayerHPBarPadding * 2.0f) * playerHpRatio;
    const float playerFillHeight = PlayerHPBarHeight - PlayerHPBarPadding * 2.0f;
    if (playerFillWidth > 0.0f)
    {
        ui.DrawQuad({
            DirectX::XMFLOAT2 { playerFillX, playerFillY },
            DirectX::XMFLOAT2 { playerFillX + playerFillWidth, playerFillY },
            DirectX::XMFLOAT2 { playerFillX + playerFillWidth,
                playerFillY + playerFillHeight },
            DirectX::XMFLOAT2 { playerFillX, playerFillY + playerFillHeight }
        }, { 0.6f, 1.0f, 0.05f, 1.0f });
    }

    const float staminaRatio = m_player->GetMaxStamina() > 0.0f
        ? m_player->GetStamina() / m_player->GetMaxStamina() : 0.0f;
    ui.DrawQuad({
        DirectX::XMFLOAT2 { PlayerStaminaBarX, PlayerStaminaBarY },
        DirectX::XMFLOAT2 { PlayerStaminaBarX + PlayerStaminaBarWidth, PlayerStaminaBarY },
        DirectX::XMFLOAT2 { PlayerStaminaBarX + PlayerStaminaBarWidth,
            PlayerStaminaBarY + PlayerStaminaBarHeight },
        DirectX::XMFLOAT2 { PlayerStaminaBarX, PlayerStaminaBarY + PlayerStaminaBarHeight }
    }, { 0.0f, 0.0f, 0.0f, 1.0f });

    const float staminaFillX = PlayerStaminaBarX + PlayerStaminaBarPadding;
    const float staminaFillY = PlayerStaminaBarY + PlayerStaminaBarPadding;
    const float staminaFillWidth =
        (PlayerStaminaBarWidth - PlayerStaminaBarPadding * 2.0f) * staminaRatio;
    const float staminaFillHeight =
        PlayerStaminaBarHeight - PlayerStaminaBarPadding * 2.0f;
    if (staminaFillWidth > 0.0f)
    {
        ui.DrawQuad({
            DirectX::XMFLOAT2 { staminaFillX, staminaFillY },
            DirectX::XMFLOAT2 { staminaFillX + staminaFillWidth, staminaFillY },
            DirectX::XMFLOAT2 { staminaFillX + staminaFillWidth,
                staminaFillY + staminaFillHeight },
            DirectX::XMFLOAT2 { staminaFillX, staminaFillY + staminaFillHeight }
        }, { 1.0f, 0.82f, 0.05f, 1.0f });
    }
}

void Scene_Game::OnUpdate(float)
{
    if (m_input.ColliderVisualizationPressed())
        m_showColliders = !m_showColliders;
    m_camera.Rotate(m_input.LookX(), m_input.LookY());
    m_player->SetMovementYaw(-m_camera.GetYaw());
}

void Scene_Game::OnAfterUpdate(float deltaTime)
{
    m_player->ResolveTerrainContact();

    const int aliveCount = static_cast<int>(std::count_if(
        m_miniCopies.begin(), m_miniCopies.end(),
        [](const MiniCopy* miniCopy) { return !miniCopy->IsDead(); }));
    if (aliveCount < MaxMiniCopies && m_boss->ConsumeSpawnRequest())
    {
        constexpr float SpawnX[] = { -12.0f, -4.0f, 4.0f, 12.0f };
        const int slot = m_spawnSequence % MaxMiniCopies;
        const float x = SpawnX[slot];
        const float z = 27.0f + static_cast<float>(slot % 2) * 3.0f;
        const DirectX::XMFLOAT3 spawnPosition { x, Terrain::HeightAt(x, z), z };

        MiniCopy* miniCopy = nullptr;
        const auto reusable = std::find_if(m_miniCopies.begin(), m_miniCopies.end(),
            [](const MiniCopy* copy) { return copy->IsDead(); });
        if (reusable != m_miniCopies.end())
            miniCopy = *reusable;
        else
        {
            miniCopy = AddGameObject<MiniCopy>(*m_player, *m_boss);
            if (miniCopy) m_miniCopies.push_back(miniCopy);
        }
        if (miniCopy)
        {
            miniCopy->Respawn(spawnPosition);
            ++m_spawnSequence;
        }
    }

    for (MiniCopy* miniCopy : m_miniCopies)
    {
        if (miniCopy->IsDead()) continue;
        m_player->ResolveChargeAttackHit(*miniCopy);
        if (miniCopy->IsDead()) continue;
        m_player->ResolveAttackHit(*miniCopy);
        miniCopy->ResolveAttackHit(*m_player);
    }
    m_camera.SetTarget(m_player->GetPosition());
    for (Tree* tree : m_trees)
        tree->FaceCamera(m_camera.GetPosition());

    const auto updateWaterMovement = [deltaTime](const DirectX::XMFLOAT3& current,
        const DirectX::XMFLOAT3& previous, float previousStrength)
    {
        const float dx = current.x - previous.x;
        const float dz = current.z - previous.z;
        const float speed = deltaTime > 0.0f
            ? std::sqrt(dx * dx + dz * dz) / deltaTime : 0.0f;
        const float pondX = current.x - Terrain::PondCenterX;
        const float pondZ = current.z - Terrain::PondCenterZ;
        const bool inWater = pondX * pondX + pondZ * pondZ
            <= Terrain::PondWaterRadius * Terrain::PondWaterRadius;
        const float target = inWater ? std::clamp(speed / 3.0f, 0.0f, 1.0f) : 0.0f;
        const float response = 1.0f - std::exp(-8.0f * (std::max)(deltaTime, 0.0f));
        return previousStrength + (target - previousStrength) * response;
    };
    const auto playerPosition = m_player->GetPosition();
    m_playerWaterMovement = updateWaterMovement(
        playerPosition, m_previousPlayerPosition, m_playerWaterMovement);
    m_previousPlayerPosition = playerPosition;

    MiniCopy* environmentMiniCopy = nullptr;
    for (MiniCopy* miniCopy : m_miniCopies)
    {
        if (!miniCopy->IsDead())
        {
            environmentMiniCopy = miniCopy;
            break;
        }
    }
    if (environmentMiniCopy != m_environmentMiniCopy)
    {
        m_environmentMiniCopy = environmentMiniCopy;
        m_miniCopyWaterMovement = 0.0f;
        if (m_environmentMiniCopy)
            m_previousMiniCopyPosition = m_environmentMiniCopy->GetPosition();
    }
    if (m_environmentMiniCopy)
    {
        const auto miniCopyPosition = m_environmentMiniCopy->GetPosition();
        m_miniCopyWaterMovement = updateWaterMovement(miniCopyPosition,
            m_previousMiniCopyPosition, m_miniCopyWaterMovement);
        m_previousMiniCopyPosition = miniCopyPosition;
    }
}

void Scene_Game::OnDraw(Renderer& renderer)
{
    renderer.SetCamera(m_camera.GetPosition(), m_camera.GetTarget());
    const auto secondaryInteractor = m_environmentMiniCopy
        ? m_environmentMiniCopy->GetPosition() : m_boss->GetPosition();
    renderer.SetGrassInteractors(m_player->GetPosition(), secondaryInteractor,
        m_playerWaterMovement, m_miniCopyWaterMovement);
    if (!m_showColliders) return;

    for (const auto* playerCollider : m_player->GetComponents<Collider>())
    {
        if (!playerCollider->IsEnabled()) continue;
        const auto playerShape = playerCollider->GetShape();
        if (playerShape.hasSegment)
            renderer.DrawColliderSegment(playerShape.segmentStart, playerShape.segmentEnd,
                playerShape.radius, playerCollider->IsColliding());
        else
            renderer.DrawCollider(playerShape.center, playerShape.halfExtents,
                playerShape.radius, playerCollider->IsColliding());
    }
    for (MiniCopy* miniCopy : m_miniCopies)
    {
        for (const auto* collider : miniCopy->GetComponents<Collider>())
        {
            if (!collider->IsEnabled()) continue;
            const auto shape = collider->GetShape();
            if (shape.hasSegment)
                renderer.DrawColliderSegment(shape.segmentStart, shape.segmentEnd,
                    shape.radius, collider->IsColliding());
            else
                renderer.DrawCollider(shape.center, shape.halfExtents,
                    shape.radius, collider->IsColliding());
        }
    }
}
