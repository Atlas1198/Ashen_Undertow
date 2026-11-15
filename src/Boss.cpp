//-------------------------------------------------
//
//          Boss.cpp
//
//  ボスの HP 制御と全体攻撃フェーズを実装する。
//-------------------------------------------------

#include "Boss.h"

#include "AnimationComponent.h"
#include "GameAssets.h"
#include "GameBalance.h"
#include "ModelComponent.h"

#include <algorithm>

using namespace GameBalance::Boss;

bool Boss::Initialize(ModelManager& modelManager)
{
    InitializeHP(5000);
    m_scale = { VisualScale, VisualScale, VisualScale };

    m_model = AddComponent<ModelComponent>(modelManager, GameAssets::EnemyModel);
    if (!m_model) return false;
    m_model->SetLocalRotation(0.0f, DirectX::XM_PI, 0.0f);

    m_animation = AddComponent<AnimationComponent>(modelManager, GameAssets::EnemyModel);
    if (GlobalAttackTestAtStart)
    {
        m_globalAttackPhase = GlobalAttackPhase::Warning;
        m_globalAttackTimer = GlobalAttackWarningDuration;
        m_globalAttackUsed = true;
    }
    return m_animation != nullptr;
}

// 通常攻撃では HP を 1 残し、特殊攻撃でのみ決着できるようにする。
void Boss::TakeDamage(int amount)
{
    if (amount <= 0 || GetHP() <= 1) return;
    GameObject::TakeDamage(amount < GetHP() ? amount : GetHP() - 1);
}

void Boss::TakeSpecialDamage(int amount)
{
    GameObject::TakeDamage(amount);
    if (IsDead())
    {
        m_globalAttackPhase = GlobalAttackPhase::Cooldown;
        m_globalAttackTimer = 0.0f;
    }
}

// 頭ボーンが取れればそのワールド位置を使い、取れないモデルでは概算位置へ戻す。
DirectX::XMFLOAT3 Boss::GetSpecialAttackTarget() const
{
    DirectX::XMFLOAT4X4 headTransform;
    if (m_animation && m_model &&
        m_animation->GetNodeModelTransform("mixamorig:Head", headTransform))
    {
        const auto modelWorld = m_model->CreateModelWorldMatrix();
        DirectX::XMFLOAT3 position;
        DirectX::XMStoreFloat3(&position, DirectX::XMVector3TransformCoord(
            DirectX::XMVectorZero(), DirectX::XMLoadFloat4x4(&headTransform) *
            DirectX::XMLoadFloat4x4(&modelWorld)));
        return position;
    }
    return { m_position.x, m_position.y + m_scale.y * 0.78f, m_position.z };
}

DirectX::XMFLOAT3 Boss::GetMouthPosition() const
{
    auto position = GetSpecialAttackTarget();
    position.y -= GetVisualScale() * 0.055f;
    position.z -= GetVisualScale() * 0.025f;
    return position;
}

float Boss::GetGlobalAttackProgress() const
{
    if (m_globalAttackPhase == GlobalAttackPhase::Warning)
        return 1.0f - m_globalAttackTimer / GlobalAttackWarningDuration;
    if (m_globalAttackPhase == GlobalAttackPhase::Flame)
        return 1.0f - m_globalAttackTimer / GlobalAttackFlameDuration;
    return 0.0f;
}

// HP 閾値で一度だけ全体攻撃を始め、予兆と炎の残り時間を進める。
void Boss::Update(float deltaTime)
{
    if (IsDead()) return;
    if (m_globalAttackPhase == GlobalAttackPhase::Cooldown)
    {
        m_animation->Play("Idle", true, 1.0f, 0.2f);
        if (!m_globalAttackUsed && GetHP() <= static_cast<int>(
            GetMaxHP() * GlobalAttackTriggerHpRatio))
        {
            m_globalAttackPhase = GlobalAttackPhase::Warning;
            m_globalAttackTimer = GlobalAttackWarningDuration;
            m_globalAttackUsed = true;
        }
    }
    else if (m_globalAttackPhase == GlobalAttackPhase::Warning)
    {
        m_animation->Play("Idle", true, 1.0f, 0.2f);
        m_globalAttackTimer = std::max(0.0f, m_globalAttackTimer - deltaTime);
        if (m_globalAttackTimer <= 0.0f)
        {
            m_globalAttackPhase = GlobalAttackPhase::Flame;
            m_globalAttackTimer = GlobalAttackFlameDuration;
        }
    }
    else
    {
        m_animation->Play("Roar", false, 1.0f, 0.18f);
        m_globalAttackTimer = std::max(0.0f, m_globalAttackTimer - deltaTime);
        if (m_globalAttackTimer <= 0.0f)
            m_globalAttackPhase = GlobalAttackPhase::Cooldown;
    }
}
