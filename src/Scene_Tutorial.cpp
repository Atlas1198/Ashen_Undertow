#include "Scene_Tutorial.h"

#include "AnimationComponent.h"
#include "Boss.h"
#include "Collider.h"
#include "GameAssets.h"
#include "Input.h"
#include "ModelComponent.h"
#include "Player.h"
#include "Renderer.h"
#include "Terrain.h"

#include <algorithm>
#include <array>
#include <cmath>

class TutorialDummy final : public GameObject
{
public:
    bool Initialize(ModelManager& modelManager) override
    {
        InitializeHP(10000);
        auto* model = AddComponent<ModelComponent>(
            modelManager, GameAssets::EnemyModel);
        if (!model) return false;
        model->SetLocalRotation(0.0f, DirectX::XM_PI, 0.0f);
        m_animation = AddComponent<AnimationComponent>(
            modelManager, GameAssets::EnemyModel);
        if (!m_animation) return false;
        auto* hurtbox = AddComponent<SphereCollider>(0.9f);
        if (!hurtbox) return false;
        hurtbox->SetOffset(0.0f, 1.0f, 0.0f);
        hurtbox->SetTrigger(true);
        hurtbox->SetRole(ColliderRole::Hurtbox);
        m_animation->Play("Idle", true, 1.0f, 0.0f);
        return true;
    }

    void Update(float) override
    {
        m_animation->Play("Idle", true, 1.0f, 0.15f);
    }

private:
    AnimationComponent* m_animation = nullptr;
};

bool Scene_Tutorial::Initialize()
{
    m_player = AddGameObject<Player>(m_input);
    if (!m_player) return false;
    m_dummy = AddGameObject<TutorialDummy>();
    if (!m_dummy) return false;
    m_dummy->SetPosition(0.0f, Terrain::HeightAt(0.0f, 4.0f), 4.0f);
    m_boss = AddGameObject<Boss>();
    if (!m_boss) return false;
    m_boss->SetPosition(0.0f, -1000.0f, 0.0f);
    m_previousPlayerPosition = m_player->GetPosition();
    m_camera.SetTarget(m_player->GetPosition());
    return true;
}

bool Scene_Tutorial::InitializeUI(UIManager& ui)
{
    m_effectTexture = ui.LoadImageFile(
        "assets/texture/effects/spark_particle.png");
    m_stepsTexture = ui.LoadImageFile(
        "assets/texture/ui/tutorial_steps_ja.png");
    m_gameTexture = ui.LoadImageFile("assets/texture/ui/game.png");
    m_pressEnterTexture = ui.LoadImageFile(
        "assets/texture/ui/press_enter.png");
    if (!m_effectTexture.IsValid() || !m_stepsTexture.IsValid() ||
        !m_gameTexture.IsValid() || !m_pressEnterTexture.IsValid()) return false;
    m_effectManager.SetTextureIndex(m_effectTexture.index);
    return true;
}

void Scene_Tutorial::OnUpdate(float deltaTime)
{
    m_time += deltaTime;
    m_camera.Rotate(m_input.LookX(), m_input.LookY());
    const auto cameraPosition = m_camera.GetPosition();
    m_camera.SetMinimumHeight(Terrain::HeightAt(
        cameraPosition.x, cameraPosition.z) + 0.25f);
    m_player->SetMovementYaw(-m_camera.GetYaw());

    if (m_step == 7 && m_input.SpecialAttackPressed() &&
        m_player->IsSpecialGaugeFull() && !m_specialAttack.IsActive())
    {
        auto start = m_player->GetPosition();
        start.y += 1.0f;
        if (m_specialAttack.Launch(start, m_boss->GetSpecialAttackTarget()))
        {
            m_player->ConsumeSpecialGauge();
            m_specialEffectTimer = 0.0f;
        }
    }

    if (m_step >= TutorialStepCount && !m_leaving && m_input.ConfirmPressed())
        m_leaving = true;
    if (m_leaving)
    {
        m_fade = (std::min)(1.0f, m_fade + deltaTime);
        if (m_fade >= 1.0f) RequestScene(SceneRequest::Game);
    }
}

void Scene_Tutorial::OnAfterUpdate(float deltaTime)
{
    m_effectManager.Update(deltaTime);
    m_player->ResolveTerrainContact();
    const auto current = m_player->GetPosition();
    const float dx = current.x - m_previousPlayerPosition.x;
    const float dz = current.z - m_previousPlayerPosition.z;
    const float traveled = std::sqrt(dx * dx + dz * dz);

    if (m_step == 0)
    {
        if (!m_input.DashHeld()) m_moveDistance += traveled;
        if (m_moveDistance >= 3.0f) AdvanceStep();
    }
    else if (m_step == 1)
    {
        if (!m_player->IsGrounded()) m_wasAirborne = true;
        if (m_wasAirborne && m_player->IsGrounded()) AdvanceStep();
    }
    else if (m_step == 2)
    {
        if (m_input.DashHeld() &&
            (std::abs(m_input.MoveX()) + std::abs(m_input.MoveZ()) > 0.1f))
            m_dashDistance += traveled;
        if (m_dashDistance >= 3.0f) AdvanceStep();
    }
    else if (m_step == 3)
    {
        if (m_player->IsEvading() && !m_wasEvading) AdvanceStep();
    }
    else if (m_step == 4)
    {
        DirectX::XMFLOAT3 hitPosition {};
        if (m_player->ResolveAttackHit(*m_dummy, hitPosition))
        {
            m_effectManager.Spawn(EffectType::HitSpark, hitPosition);
            ++m_comboHits;
            m_dummy->Heal(m_dummy->GetMaxHP());
            if (m_comboHits >= 3) AdvanceStep();
        }
        else if (m_comboHits > 0 && m_wasAttacking && !m_player->IsAttacking())
            m_comboHits = 0;
    }
    else if (m_step == 5)
    {
        DirectX::XMFLOAT3 hitPosition {};
        float chargePower = 0.0f;
        if (m_player->ResolveChargeAttackHit(
            *m_dummy, hitPosition, chargePower))
        {
            m_effectManager.SpawnChargeHit(hitPosition, chargePower);
            m_dummy->Heal(m_dummy->GetMaxHP());
            if (chargePower >= 0.85f) AdvanceStep();
        }
    }
    else if (m_step == 6)
    {
        m_guardDuration = m_player->IsBlocking()
            ? m_guardDuration + deltaTime : 0.0f;
        if (m_guardDuration >= 0.8f) AdvanceStep();
    }
    else if (m_step == 7 && m_specialAttack.IsActive())
    {
        const auto bossTarget = m_boss->GetSpecialAttackTarget();
        const bool hit = m_specialAttack.Update(deltaTime, bossTarget);
        m_specialEffectTimer -= deltaTime;
        if (m_specialEffectTimer <= 0.0f)
        {
            m_effectManager.SpawnSpecialSlash(
                m_specialAttack.GetPosition(), m_specialAttack.GetDirection());
            m_specialEffectTimer = 0.035f;
        }
        if (hit)
        {
            m_effectManager.SpawnSpecialImpact(
                bossTarget, m_specialAttack.GetDirection());
            AdvanceStep();
        }
        else if (!m_specialAttack.IsActive())
            m_player->AddSpecialGauge(m_player->GetMaxSpecialGauge());
    }

    m_previousPlayerPosition = current;
    m_wasEvading = m_player->IsEvading();
    m_wasAttacking = m_player->IsAttacking();
    m_camera.SetTarget(current);
}

void Scene_Tutorial::AdvanceStep()
{
    if (m_step >= TutorialStepCount) return;
    m_completed[m_step] = true;
    ++m_step;
    if (m_step == 4) PlaceDummyInFrontOfPlayer();
    if (m_step == 7)
    {
        PlaceBossInFrontOfPlayer();
        m_player->AddSpecialGauge(m_player->GetMaxSpecialGauge());
    }
}

void Scene_Tutorial::PlaceDummyInFrontOfPlayer()
{
    const auto player = m_player->GetPosition();
    const float yaw = m_camera.GetYaw();
    float x = player.x - std::sin(yaw) * 3.2f;
    float z = player.z + std::cos(yaw) * 3.2f;
    Terrain::ClampToArena(x, z);
    m_dummy->SetPosition(x, Terrain::HeightAt(x, z), z);
}

void Scene_Tutorial::PlaceBossInFrontOfPlayer()
{
    const auto player = m_player->GetPosition();
    const float yaw = m_camera.GetYaw();
    const float x = player.x - std::sin(yaw) * 42.0f;
    const float z = player.z + std::cos(yaw) * 42.0f;
    const float scale = m_boss->GetVisualScale();
    m_boss->SetPosition(x, Terrain::HeightAt(x, z) - scale * 0.9f, z);
    m_boss->SetRotation(0.0f,
        std::atan2(player.x - x, player.z - z), 0.0f);
    m_dummy->SetPosition(0.0f, -1000.0f, 0.0f);
}

DirectX::XMFLOAT3 Scene_Tutorial::GetDummyTarget() const
{
    auto target = m_dummy->GetPosition();
    target.y += 1.0f;
    return target;
}

void Scene_Tutorial::OnDraw(Renderer& renderer)
{
    renderer.SetCamera(m_camera.GetPosition(), m_camera.GetTarget());
    const auto secondary = m_step >= 7
        ? m_boss->GetPosition() : m_dummy->GetPosition();
    renderer.SetGrassInteractors(m_player->GetPosition(),
        secondary, 0.0f, 0.0f);
    m_effectManager.Draw(renderer);
}

void Scene_Tutorial::OnDrawUI(UIManager& ui)
{
    constexpr float ScreenWidth = 1600.0f;
    constexpr float ScreenHeight = 900.0f;
    const auto rectangle = [](float x, float y, float width, float height)
    {
        return std::array<DirectX::XMFLOAT2, 4> {
            DirectX::XMFLOAT2 { x, y }, DirectX::XMFLOAT2 { x + width, y },
            DirectX::XMFLOAT2 { x + width, y + height },
            DirectX::XMFLOAT2 { x, y + height }
        };
    };
    const auto segment = [&](DirectX::XMFLOAT2 a, DirectX::XMFLOAT2 b,
        float width, DirectX::XMFLOAT4 color)
    {
        const float dx = b.x - a.x, dy = b.y - a.y;
        const float length = std::sqrt(dx * dx + dy * dy);
        const float px = -dy / length * width, py = dx / length * width;
        ui.DrawQuad({ DirectX::XMFLOAT2 { a.x + px, a.y + py },
            DirectX::XMFLOAT2 { b.x + px, b.y + py },
            DirectX::XMFLOAT2 { b.x - px, b.y - py },
            DirectX::XMFLOAT2 { a.x - px, a.y - py } }, color);
    };

    ui.DrawImage(m_stepsTexture, { 20.0f, 10.0f, 520.0f, 715.0f },
        { 1.0f, 1.0f, 1.0f, 1.0f });
    constexpr float RowY[] = {
        54.0f, 142.0f, 230.0f, 318.0f,
        405.0f, 493.0f, 581.0f, 669.0f
    };
    if (m_step < TutorialStepCount)
    {
        const float glow = 0.65f + 0.35f *
            (0.5f + 0.5f * std::sin(m_time * 3.0f));
        const DirectX::XMFLOAT4 frameColor { 0.05f, 0.95f, 1.0f, glow };
        const float top = RowY[m_step] - 40.0f;
        ui.DrawQuad(rectangle(30.0f, top, 500.0f, 3.0f), frameColor);
        ui.DrawQuad(rectangle(30.0f, top + 80.0f, 500.0f, 3.0f), frameColor);
        ui.DrawQuad(rectangle(30.0f, top, 3.0f, 83.0f), frameColor);
        ui.DrawQuad(rectangle(527.0f, top, 3.0f, 83.0f), frameColor);
    }
    for (int i = 0; i < TutorialStepCount; ++i)
    {
        if (!m_completed[i]) continue;
        const DirectX::XMFLOAT4 checkColor { 0.15f, 1.0f, 0.75f, 1.0f };
        segment({ 48.0f, RowY[i] }, { 58.0f, RowY[i] + 11.0f },
            3.0f, checkColor);
        segment({ 58.0f, RowY[i] + 11.0f }, { 78.0f, RowY[i] - 13.0f },
            3.0f, checkColor);
    }

    if (m_step >= TutorialStepCount)
    {
        ui.DrawQuad(rectangle(0.0f, 0.0f, ScreenWidth, ScreenHeight),
            { 0.0f, 0.0f, 0.0f, 0.42f });
        const float pulse = 0.6f + 0.4f *
            (0.5f + 0.5f * std::sin(m_time * 2.4f));
        ui.DrawImage(m_gameTexture, { 570.0f, 300.0f, 460.0f, 154.0f },
            { 0.85f, 1.0f, 1.0f, pulse });
        ui.DrawImage(m_pressEnterTexture,
            { 625.0f, 505.0f, 350.0f, 117.0f },
            { 1.0f, 1.0f, 1.0f, pulse * (1.0f - m_fade) });
    }

    if (m_fade > 0.0f)
        ui.DrawQuad(rectangle(0.0f, 0.0f, ScreenWidth, ScreenHeight),
            { 0.0f, 0.0f, 0.0f, m_fade });
}
