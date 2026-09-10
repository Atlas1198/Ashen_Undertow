#include "Scene_Game.h"

#include "Enemy.h"
#include "Player.h"
#include "Tree.h"
#include "Collider.h"
#include "Input.h"

#include "Renderer.h"
#include "Terrain.h"

namespace
{
constexpr float EnemyHPBarX = 550.0f;
constexpr float EnemyHPBarY = 24.0f;
constexpr float EnemyHPBarWidth = 500.0f;
constexpr float EnemyHPBarHeight = 16.0f;
constexpr float EnemyHPBarPadding = 2.0f;
}

Scene_Game::Scene_Game(ModelManager& modelManager, const Input& input)
    : SceneManager(modelManager), m_input(input)
{
}

bool Scene_Game::Initialize()
{
    m_player = AddGameObject<Player>(m_input);
    if (!m_player) return false;

    m_enemy = AddGameObject<Enemy>();
    if (!m_enemy) return false;
    m_enemy->SetPosition(3.0f, Terrain::HeightAt(3.0f, 3.0f), 3.0f);

    m_tree = AddGameObject<Tree>();
    if (!m_tree) return false;
    m_tree->SetPosition(-3.0f, Terrain::HeightAt(-3.0f, 3.0f), 3.0f);
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
    ui.DrawImage(m_wonheeImage, { 24, 24, 240, m_wonheeHeight });
    // 4頂点を画面ピクセル座標で手打ちした四角形。
    ui.DrawQuad({ DirectX::XMFLOAT2 { 300, 40 }, { 500, 40 },
        { 500, 180 }, { 300, 180 } }, { 0.1f, 0.7f, 1.0f, 0.75f });

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
}

void Scene_Game::OnUpdate(float)
{
    if (m_input.ColliderVisualizationPressed())
        m_showColliders = !m_showColliders;
    m_camera.Rotate(m_input.LookX(), m_input.LookY());
    m_player->SetMovementYaw(-m_camera.GetYaw());
}

void Scene_Game::OnAfterUpdate(float)
{
    m_player->ResolveTerrainContact();
    m_player->ResolveAttackHit(*m_enemy);
    m_camera.SetTarget(m_player->GetPosition());
    m_tree->FaceCamera(m_camera.GetPosition());
}

void Scene_Game::OnDraw(Renderer& renderer)
{
    renderer.SetCamera(m_camera.GetPosition(), m_camera.GetTarget());
    if (!m_showColliders) return;

    const auto* playerCollider = m_player->GetComponent<CapsuleCollider>();
    const auto* enemyCollider = m_enemy->GetComponent<CapsuleCollider>();
    const auto playerShape = playerCollider->GetShape();
    const auto enemyShape = enemyCollider->GetShape();
    renderer.DrawCollider(playerShape.center, playerShape.halfExtents,
        playerShape.radius, playerCollider->IsColliding());
    renderer.DrawCollider(enemyShape.center, enemyShape.halfExtents,
        enemyShape.radius, enemyCollider->IsColliding());

    const auto* attackCollider = m_player->GetComponent<SphereCollider>();
    if (attackCollider && attackCollider->IsEnabled())
    {
        const auto attackShape = attackCollider->GetShape();
        renderer.DrawCollider(attackShape.center, attackShape.halfExtents,
            attackShape.radius, attackCollider->IsColliding());
    }
}
