#include "Scene_Game.h"

#include "Enemy.h"
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
constexpr float EnemyHPBarX = 550.0f;
constexpr float EnemyHPBarY = 24.0f;
constexpr float EnemyHPBarWidth = 500.0f;
constexpr float EnemyHPBarHeight = 16.0f;
constexpr float EnemyHPBarPadding = 2.0f;
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
}

Scene_Game::Scene_Game(ModelManager& modelManager, const Input& input)
    : SceneManager(modelManager), m_input(input)
{
}

bool Scene_Game::Initialize()
{
    m_player = AddGameObject<Player>(m_input);
    if (!m_player) return false;

    m_enemy = AddGameObject<Enemy>(*m_player);
    if (!m_enemy) return false;
    m_enemy->SetPosition(3.0f, Terrain::HeightAt(3.0f, 3.0f), 3.0f);

    constexpr int TreeCount = 36;
    constexpr int TreesPerRing = TreeCount / 2;
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

        Tree* tree = AddGameObject<Tree>();
        if (!tree) return false;
        const float visualScale = 1.55f + scaleRandom * 0.35f;
        const float groundSink = visualScale * 0.3f;
        tree->SetPosition(x, Terrain::HeightAt(x, z) - groundSink, z);
        tree->SetVisualScale(visualScale);
        m_trees.push_back(tree);
    }
    m_previousPlayerPosition = m_player->GetPosition();
    m_previousEnemyPosition = m_enemy->GetPosition();
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

    const float hpRatio = m_enemy->GetMaxHP() > 0
        ? static_cast<float>(m_enemy->GetHP()) / m_enemy->GetMaxHP() : 0.0f;
    ui.DrawQuad({
        DirectX::XMFLOAT2 { EnemyHPBarX, EnemyHPBarY },
        DirectX::XMFLOAT2 { EnemyHPBarX + EnemyHPBarWidth, EnemyHPBarY },
        DirectX::XMFLOAT2 { EnemyHPBarX + EnemyHPBarWidth, EnemyHPBarY + EnemyHPBarHeight },
        DirectX::XMFLOAT2 { EnemyHPBarX, EnemyHPBarY + EnemyHPBarHeight }
    }, { 0.0f, 0.0f, 0.0f, 1.0f });

    const float fillX = EnemyHPBarX + EnemyHPBarPadding;
    const float fillY = EnemyHPBarY + EnemyHPBarPadding;
    const float fillWidth = (EnemyHPBarWidth - EnemyHPBarPadding * 2.0f) * hpRatio;
    const float fillHeight = EnemyHPBarHeight - EnemyHPBarPadding * 2.0f;
    if (fillWidth > 0.0f)
    {
        ui.DrawQuad({
            DirectX::XMFLOAT2 { fillX, fillY },
            DirectX::XMFLOAT2 { fillX + fillWidth, fillY },
            DirectX::XMFLOAT2 { fillX + fillWidth, fillY + fillHeight },
            DirectX::XMFLOAT2 { fillX, fillY + fillHeight }
        }, { 0.9f, 0.05f, 0.05f, 1.0f });
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
    m_player->ResolveAttackHit(*m_enemy);
    m_enemy->ResolveAttackHit(*m_player);
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
    const auto enemyPosition = m_enemy->GetPosition();
    m_playerWaterMovement = updateWaterMovement(
        playerPosition, m_previousPlayerPosition, m_playerWaterMovement);
    m_enemyWaterMovement = updateWaterMovement(
        enemyPosition, m_previousEnemyPosition, m_enemyWaterMovement);
    m_previousPlayerPosition = playerPosition;
    m_previousEnemyPosition = enemyPosition;
}

void Scene_Game::OnDraw(Renderer& renderer)
{
    renderer.SetCamera(m_camera.GetPosition(), m_camera.GetTarget());
    renderer.SetGrassInteractors(m_player->GetPosition(), m_enemy->GetPosition(),
        m_playerWaterMovement, m_enemyWaterMovement);
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
    for (const auto* enemyCollider : m_enemy->GetComponents<Collider>())
    {
        if (!enemyCollider->IsEnabled()) continue;
        const auto enemyShape = enemyCollider->GetShape();
        if (enemyShape.hasSegment)
            renderer.DrawColliderSegment(enemyShape.segmentStart, enemyShape.segmentEnd,
                enemyShape.radius, enemyCollider->IsColliding());
        else
            renderer.DrawCollider(enemyShape.center, enemyShape.halfExtents,
                enemyShape.radius, enemyCollider->IsColliding());
    }
}
