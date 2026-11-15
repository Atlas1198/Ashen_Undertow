//-------------------------------------------------
//
//          Scene_Game.cpp
//
//  本編の配置、進行、戦闘結果、画面演出を統括する。
//-------------------------------------------------

#include "Scene_Game.h"

#include "Boss.h"
#include "MiniCopy.h"
#include "Player.h"
#include "Tree.h"
#include "Collider.h"
#include "Input.h"
#include "GameBalance.h"
#include "AudioManager.h"

#include "Renderer.h"
#include "Terrain.h"
#include <algorithm>
#include <cmath>

using namespace GameBalance::Scene;

Scene_Game::Scene_Game(ModelManager& modelManager, const Input& input,
    AudioManager& audio)
    : SceneManager(modelManager), m_input(input), m_audio(audio)
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
    m_pendingMiniCopySpawns.reserve(MaxMiniCopies);
    for (int i = 0; i < InitialMiniCopies; ++i)
    {
        const float angle = DirectX::XM_2PI * static_cast<float>(i) /
            static_cast<float>(InitialMiniCopies);
        QueueMiniCopySpawn(MakeMiniCopySpawnPosition(angle, 13.5f));
    }
    m_previousPlayerPosition = m_player->GetPosition();
    return true;
}

bool Scene_Game::InitializeUI(UIManager& ui)
{
    m_audio.PlayMusic(AudioId::BattleMusic, 0.36f);
    if (!m_hud.InitializeUI(ui)) return false;
    m_effectTexture = ui.LoadImageFile(
        "assets/texture/effects/spark_particle.png");
    if (!m_effectTexture.IsValid()) return false;
    m_effectManager.SetTextureIndex(m_effectTexture.index);
    m_youDiedTexture = ui.LoadImageFile("assets/texture/ui/you_died.png");
    m_titleTexture = ui.LoadImageFile("assets/texture/ui/title.png");
    m_retryTexture = ui.LoadImageFile("assets/texture/ui/retry.png");
    m_clearTexture = ui.LoadImageFile("assets/texture/ui/clear.png");
    m_gameStartTexture = ui.LoadImageFile("assets/texture/ui/game_start.png");
    m_specialPromptTexture = ui.LoadImageFile(
        "assets/texture/ui/use_special_attack.png");
    m_staminaShortageTexture = ui.LoadImageFile(
        "assets/texture/ui/charge_stamina_shortage.png");
    return m_youDiedTexture.IsValid() && m_titleTexture.IsValid() &&
        m_retryTexture.IsValid() && m_clearTexture.IsValid() &&
        m_gameStartTexture.IsValid() && m_specialPromptTexture.IsValid() &&
        m_staminaShortageTexture.IsValid();
}

void Scene_Game::OnDrawUI(UIManager& ui)
{
    if (m_gameStartTime < 2.2f)
    {
        const float alpha = m_gameStartTime < 0.55f
            ? m_gameStartTime / 0.55f
            : (m_gameStartTime < 1.45f ? 1.0f
                : std::clamp((2.2f - m_gameStartTime) / 0.75f, 0.0f, 1.0f));
        ui.DrawImage(m_gameStartTexture,
            { 260.0f, 240.0f, 1080.0f, 405.0f },
            { 1.0f, 1.0f, 1.0f, alpha });
        return;
    }

    m_hud.Draw(ui, *m_player, *m_boss, m_miniCopies, m_camera);
    const bool showingDeath = m_deathScreenTime >= 0.0f;
    const bool showingClear = m_clearScreenTime >= 0.0f;
    if (!showingDeath && !showingClear && m_boss->GetHP() == 1 &&
        !m_specialAttack.IsActive())
    {
        const float pulse = 0.82f + 0.18f *
            (0.5f + 0.5f * std::sin(
                m_bossAttackVisualTime * SpecialPromptPulseSpeed));
        ui.DrawImage(m_specialPromptTexture,
            { 450.0f, 105.0f, 700.0f, 120.0f },
            { 1.0f, 1.0f, 1.0f, pulse });
    }
    if (!showingDeath && !showingClear && m_staminaShortageNoticeTime > 0.0f)
    {
        const float alpha = std::clamp(
            m_staminaShortageNoticeTime / 0.3f, 0.0f, 1.0f);
        ui.DrawImage(m_staminaShortageTexture,
            { 350.0f, 650.0f, 900.0f, 110.0f },
            { 1.0f, 1.0f, 1.0f, alpha });
    }
    if (!showingDeath && !showingClear) return;

    constexpr float ScreenWidth = 1600.0f;
    constexpr float ScreenHeight = 900.0f;
    const float screenTime = showingDeath ? m_deathScreenTime : m_clearScreenTime;
    const float titleAlpha = std::clamp(screenTime / 1.15f, 0.0f, 1.0f);
    const auto rectangle = [](float x, float y, float width, float height)
    {
        return std::array<DirectX::XMFLOAT2, 4> {
            DirectX::XMFLOAT2 { x, y },
            DirectX::XMFLOAT2 { x + width, y },
            DirectX::XMFLOAT2 { x + width, y + height },
            DirectX::XMFLOAT2 { x, y + height }
        };
    };
    ui.DrawQuad(rectangle(0.0f, 0.0f, ScreenWidth, ScreenHeight),
        { 0.0f, 0.0f, 0.0f, 0.68f * titleAlpha });
    ui.DrawImage(showingDeath ? m_youDiedTexture : m_clearTexture,
        { 300.0f, 225.0f, 1000.0f, 333.0f },
        { 1.0f, 1.0f, 1.0f, titleAlpha });

    if (screenTime < 1.45f) return;
    const float menuAlpha = std::clamp(
        (screenTime - 1.45f) / 0.55f, 0.0f, 1.0f);
    const float pulse = 0.55f + 0.45f *
        (0.5f + 0.5f * std::sin(screenTime * 2.35f));
    const auto drawMenuItem = [&](float centerX, UIImageHandle image, bool selected)
    {
        constexpr float Y = 655.0f;
        const float width = selected ? 340.0f : 310.0f;
        const float height = width / 3.0f;
        const float x = centerX - width * 0.5f;
        if (selected)
        {
            constexpr float GlowSize = 12.0f;
            ui.DrawImage(image,
                { x - GlowSize * 0.5f, Y - GlowSize * 0.5f,
                    width + GlowSize, height + GlowSize },
                { 0.05f, 0.75f, 1.0f,
                    menuAlpha * (0.20f + 0.16f * pulse) });
        }
        const float alpha = selected ? 0.90f + 0.10f * pulse : 0.70f;
        ui.DrawImage(image, { x, Y, width, height },
            { selected ? 0.88f : 0.78f, selected ? 1.0f : 0.84f,
                1.0f, menuAlpha * alpha });
    };
    if (showingClear)
    {
        drawMenuItem(800.0f, m_titleTexture, true);
        return;
    }
    drawMenuItem(490.0f, m_titleTexture, m_deathMenuChoice == 0);
    drawMenuItem(1110.0f, m_retryTexture, m_deathMenuChoice == 1);
}

// 開始・死亡・クリア画面を優先し、通常時だけ入力とカメラを処理する。
void Scene_Game::OnUpdate(float deltaTime)
{
    if (m_gameStartTime < 2.2f)
    {
        m_gameStartTime = (std::min)(2.2f, m_gameStartTime + deltaTime);
        return;
    }

    if (m_deathScreenTime >= 0.0f)
    {
        m_deathScreenTime += deltaTime;
        if (m_deathScreenTime >= 1.45f)
        {
            const float axis = m_input.MoveX();
            if (m_deathChoiceAxisReady && std::abs(axis) > 0.5f)
            {
                m_deathMenuChoice = axis < 0.0f ? 0 : 1;
                m_deathChoiceAxisReady = false;
            }
            else if (std::abs(axis) < 0.2f)
            {
                m_deathChoiceAxisReady = true;
            }
            if (m_input.MenuLeftPressed()) m_deathMenuChoice = 0;
            if (m_input.MenuRightPressed()) m_deathMenuChoice = 1;
            if (m_input.AttackPressed() || m_input.JumpPressed() ||
                m_input.ConfirmPressed())
            {
                RequestScene(m_deathMenuChoice == 0
                    ? SceneRequest::Title : SceneRequest::Retry);
            }
        }
        return;
    }

    if (m_clearScreenTime >= 0.0f)
    {
        m_clearScreenTime += deltaTime;
        if (m_clearScreenTime >= 1.45f &&
            (m_input.AttackPressed() || m_input.JumpPressed() ||
             m_input.ConfirmPressed()))
        {
            RequestScene(SceneRequest::Title);
        }
        return;
    }

    m_hitStopThisFrame = m_hitStopTimer > 0.0f;
    m_hitStopTimer = (std::max)(0.0f, m_hitStopTimer - deltaTime);
    if (m_input.ColliderVisualizationPressed())
        m_showColliders = !m_showColliders;
    m_camera.Rotate(m_input.LookX(), m_input.LookY());
    UpdateCameraGroundConstraint();
    m_player->SetMovementYaw(-m_camera.GetYaw());
    TryLaunchSpecialAttack();
}

void Scene_Game::UpdateCameraGroundConstraint()
{
    constexpr float CameraGroundClearance = 0.25f;
    const auto cameraPosition = m_camera.GetPosition();
    m_camera.SetMinimumHeight(Terrain::HeightAt(
        cameraPosition.x, cameraPosition.z) + CameraGroundClearance);
}

// オブジェクトと衝突の更新後に命中・撃破・演出を確定する。
void Scene_Game::OnAfterUpdate(float deltaTime)
{
    m_effectManager.Update(deltaTime);
    m_player->ResolveTerrainContact();
    if (m_player->ConsumeChargeStaminaShortageNotice())
        m_staminaShortageNoticeTime = StaminaShortageNoticeDuration;
    else
        m_staminaShortageNoticeTime = (std::max)(
            0.0f, m_staminaShortageNoticeTime - deltaTime);
    if (m_player->IsDead())
    {
        if (m_player->IsDeathAnimationFinished() && m_deathScreenTime < 0.0f)
        {
            m_deathScreenTime = 0.0f;
            m_audio.StopMusic();
            m_audio.PlaySound(AudioId::PlayerDeath, 0.68f, 0.78f);
        }
        m_hud.Update(*m_player, deltaTime);
        UpdateCameraAndTrees();
        return;
    }
    UpdateSpecialAttack(deltaTime);
    UpdatePlayerEffects(deltaTime);
    UpdateMiniCopySpawning(deltaTime);
    ResolveCombat();
    if (m_boss->IsDead() && !m_player->IsDead() && m_clearScreenTime < 0.0f)
    {
        if (m_player->IsAttacking())
        {
            m_clearSettleTime = 0.0f;
        }
        else
        {
            m_clearSettleTime += deltaTime;
            // 最後の攻撃モーションが終わってからクリア画面を出す。
            if (m_clearSettleTime >= ClearSettleDuration)
            {
                m_clearScreenTime = 0.0f;
                m_audio.StopMusic();
                m_audio.PlaySound(AudioId::Victory, 1.0f);
            }
        }
    }
    UpdateBossGlobalAttack();
    m_hud.Update(*m_player, deltaTime);
    UpdateCameraAndTrees();
    UpdateEnvironmentEffects(deltaTime);
    m_bossAttackVisualTime += deltaTime;
}

// 炎フェーズ中、池の内側を安全地帯としてプレイヤーへのダメージを判定する。
void Scene_Game::UpdateBossGlobalAttack()
{
    if (m_boss->IsDead() ||
        m_boss->GetGlobalAttackPhase() != Boss::GlobalAttackPhase::Flame)
    {
        m_bossFlameSoundActive = false;
        return;
    }
    if (!m_bossFlameSoundActive)
    {
        m_audio.PlaySound(AudioId::BossFlame, 1.25f, 0.72f);
        m_bossFlameSoundActive = true;
    }
    const auto& position = m_player->GetPosition();
    const float pondX = position.x - Terrain::PondCenterX;
    const float pondZ = position.z - Terrain::PondCenterZ;
    const bool inWater = pondX * pondX + pondZ * pondZ <=
        Terrain::PondWaterRadius * Terrain::PondWaterRadius;
    if (!inWater)
        m_player->TakeDamage(GameBalance::Boss::GlobalAttackDamage);
}

// 剣の過去姿勢から残像を作り、溜め時間に応じてオーラの発生間隔を縮める。
void Scene_Game::UpdatePlayerEffects(float deltaTime)
{
    DirectX::XMFLOAT3 swordStart {};
    DirectX::XMFLOAT3 swordEnd {};
    if (m_player->GetSwordTrailSegment(swordStart, swordEnd))
    {
        if (m_swordTrailHistoryCount < m_swordTrailHistory.size())
        {
            m_swordTrailHistory[m_swordTrailHistoryCount++] =
                { swordStart, swordEnd };
        }
        else
        {
            for (size_t i = 1; i < m_swordTrailHistory.size(); ++i)
                m_swordTrailHistory[i - 1] = m_swordTrailHistory[i];
            m_swordTrailHistory.back() = { swordStart, swordEnd };
        }
        if (m_swordTrailHistoryCount == m_swordTrailHistory.size())
            m_effectManager.SpawnSwordTrail(m_swordTrailHistory);
    }
    else
    {
        m_swordTrailHistoryCount = 0;
    }

    if (m_player->IsCharging())
    {
        m_chargeAuraSpawnTimer -= deltaTime;
        if (m_chargeAuraSpawnTimer <= 0.0f)
        {
            m_effectManager.SpawnChargeAura(
                m_player->GetFootPosition(),
                m_player->GetChargeDuration());
            m_chargeAuraSpawnTimer = (std::max)(0.025f,
                0.06f - m_player->GetChargeDuration() * 0.01f);
        }
    }
    else
    {
        m_chargeAuraSpawnTimer = 0.0f;
    }

}

// ゲージ満タン時だけ発射し、成功した発射でゲージを消費する。
void Scene_Game::TryLaunchSpecialAttack()
{
    if (m_player->IsDead() || m_boss->IsDead() ||
        !m_input.SpecialAttackPressed() || m_specialAttack.IsActive() ||
        !m_player->IsSpecialGaugeFull()) return;

    auto start = m_player->GetPosition();
    start.y += 1.0f;
    if (m_specialAttack.Launch(start, m_boss->GetSpecialAttackTarget()))
    {
        m_player->InterruptCombatForSpecialAttack();
        m_player->ConsumeSpecialGauge();
        m_specialAttackEffectTimer = 0.0f;
        m_audio.PlaySound(AudioId::SpecialAttack, 1.15f, 0.82f);
    }
}

// 飛行中は軌跡を間欠生成し、命中時にボスへのダメージと着弾演出を出す。
void Scene_Game::UpdateSpecialAttack(float deltaTime)
{
    if (!m_specialAttack.IsActive()) return;

    const auto target = m_boss->GetSpecialAttackTarget();
    const bool hit = m_specialAttack.Update(deltaTime, target);
    m_specialAttackEffectTimer -= deltaTime;
    if (m_specialAttackEffectTimer <= 0.0f)
    {
        m_effectManager.SpawnSpecialSlash(
            m_specialAttack.GetPosition(), m_specialAttack.GetDirection());
        m_specialAttackEffectTimer = 0.035f;
    }
    if (!hit) return;

    m_boss->TakeSpecialDamage(GameBalance::SpecialAttack::Damage);

    const auto impactDirection = m_specialAttack.GetDirection();
    const float impactSurfaceOffset = m_boss->GetVisualScale() * 0.14f;
    const DirectX::XMFLOAT3 impactPosition{
        target.x - impactDirection.x * impactSurfaceOffset,
        target.y - impactDirection.y * impactSurfaceOffset,
        target.z - impactDirection.z * impactSurfaceOffset
    };
    m_effectManager.SpawnSpecialImpact(impactPosition, impactDirection);
    m_hitStopTimer = (std::max)(m_hitStopTimer, MaxChargeHitStop);
}

// 先に雷の予兆を出し、ボス HP が減るほど出現間隔・同時数を増やす。
void Scene_Game::UpdateMiniCopySpawning(float deltaTime)
{
    for (auto& pending : m_pendingMiniCopySpawns)
    {
        if (!pending.lightningStarted)
        {
            m_effectManager.SpawnEnemyArrivalLightning(pending.position);
            m_audio.PlaySound(AudioId::EnemyLightning, 0.55f,
                0.90f + static_cast<float>(m_spawnSequence & 3) * 0.05f);
            pending.lightningStarted = true;
        }
        else
            pending.delay -= deltaTime;
    }
    for (size_t i = m_pendingMiniCopySpawns.size(); i-- > 0;)
    {
        if (m_pendingMiniCopySpawns[i].delay > 0.0f) continue;
        SpawnMiniCopyAt(m_pendingMiniCopySpawns[i].position);
        m_pendingMiniCopySpawns.erase(m_pendingMiniCopySpawns.begin() + i);
    }

    if (m_boss->IsDead()) return;
    m_miniCopySpawnTimer -= deltaTime;

    const int aliveCount = static_cast<int>(std::count_if(
        m_miniCopies.begin(), m_miniCopies.end(),
        [](const MiniCopy* miniCopy) { return !miniCopy->IsDead(); }));
    const float hpRatio = static_cast<float>(m_boss->GetHP()) /
        static_cast<float>(m_boss->GetMaxHP());
    const int phaseLimit = hpRatio > 0.70f ? 8 : (hpRatio > 0.35f ? 10 : 12);
    if (m_miniCopySpawnTimer > 0.0f ||
        aliveCount + static_cast<int>(m_pendingMiniCopySpawns.size()) >= phaseLimit)
        return;

    const float interval = hpRatio > 0.70f ? 6.0f : (hpRatio > 0.35f ? 4.5f : 3.0f);
    int batchCount = hpRatio > 0.70f ? 1 :
        (hpRatio > 0.35f ? 1 + (m_spawnWaveSequence & 1) : 2);
    batchCount = (std::min)(batchCount, phaseLimit - aliveCount);
    const float baseAngle = NextSpawnRandom() * DirectX::XM_2PI;
    for (int i = 0; i < batchCount; ++i)
    {
        const float angle = baseAngle + DirectX::XM_2PI *
            static_cast<float>(i) / static_cast<float>(batchCount) +
            (NextSpawnRandom() - 0.5f) * 0.6f;
        const float radius = MiniCopySpawnMinRadius + NextSpawnRandom() *
            (MiniCopySpawnMaxRadius - MiniCopySpawnMinRadius);
        QueueMiniCopySpawn(MakeMiniCopySpawnPosition(angle, radius));
    }
    ++m_spawnWaveSequence;
    m_miniCopySpawnTimer = interval;
}

void Scene_Game::QueueMiniCopySpawn(const DirectX::XMFLOAT3& position)
{
    m_pendingMiniCopySpawns.push_back(
        { position, MiniCopySpawnWarning, false });
}

// 死亡済みの敵を再利用し、必要な場合だけ新しい個体を追加する。
void Scene_Game::SpawnMiniCopyAt(const DirectX::XMFLOAT3& position)
{
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
        miniCopy->Respawn(position);
        ++m_spawnSequence;
    }
}

// プレイヤー周辺から候補を取り、池と闘技場の境界を避けて地面へ置く。
DirectX::XMFLOAT3 Scene_Game::MakeMiniCopySpawnPosition(
    float angle, float radius) const
{
    const auto playerPosition = m_player->GetPosition();
    float x = playerPosition.x + std::cos(angle) * radius;
    float z = playerPosition.z + std::sin(angle) * radius;

    const float pondX = x - Terrain::PondCenterX;
    const float pondZ = z - Terrain::PondCenterZ;
    const float pondDistance = std::sqrt(pondX * pondX + pondZ * pondZ);
    constexpr float PondSafetyRadius = Terrain::PondRadius + 1.5f;
    if (pondDistance < PondSafetyRadius)
    {
        const float safeLength = (std::max)(pondDistance, 0.001f);
        x = Terrain::PondCenterX + pondX / safeLength * PondSafetyRadius;
        z = Terrain::PondCenterZ + pondZ / safeLength * PondSafetyRadius;
    }

    constexpr float SpawnArenaRadius = Terrain::WalkableRadius - 2.0f;
    const float arenaDistance = std::sqrt(x * x + z * z);
    if (arenaDistance > SpawnArenaRadius)
    {
        x *= SpawnArenaRadius / arenaDistance;
        z *= SpawnArenaRadius / arenaDistance;
    }
    return { x, Terrain::HeightAt(x, z), z };
}

float Scene_Game::NextSpawnRandom()
{
    m_spawnRandomState = m_spawnRandomState * 1664525u + 1013904223u;
    return static_cast<float>((m_spawnRandomState >> 8) & 0x00ffffffu) /
        static_cast<float>(0x01000000u);

}

// 攻撃種類ごとにダメージ、音、ヒットストップを確定する。
void Scene_Game::ResolveCombat()
{
    for (MiniCopy* miniCopy : m_miniCopies)
    {
        if (miniCopy->IsDead()) continue;
        DirectX::XMFLOAT3 chargeHitPosition {};
        float chargePower = 0.0f;
        if (m_player->ResolveChargeAttackHit(
            *miniCopy, chargeHitPosition, chargePower))
        {
            m_effectManager.SpawnChargeHit(chargeHitPosition, chargePower);
            m_audio.PlaySound(AudioId::SwordImpact,
                0.85f + chargePower * 0.3f, 0.68f + chargePower * 0.16f);
            const float hitStop = MinChargeHitStop +
                (MaxChargeHitStop - MinChargeHitStop) *
                std::clamp((chargePower - 0.4f) / 0.6f, 0.0f, 1.0f);
            m_hitStopTimer = (std::max)(m_hitStopTimer, hitStop);
        }
        if (miniCopy->IsDead())
        {
            miniCopy->EnterDefeatedState();
            continue;
        }
        DirectX::XMFLOAT3 hitPosition {};
        if (m_player->ResolveAttackHit(*miniCopy, hitPosition))
        {
            m_effectManager.Spawn(EffectType::HitSpark, hitPosition);
            m_audio.PlaySound(AudioId::SwordImpact, 0.82f, 1.08f);
        }
        if (miniCopy->IsDead())
        {
            miniCopy->EnterDefeatedState();
            continue;
        }
        DirectX::XMFLOAT3 blockPosition {};
        if (miniCopy->ResolveAttackHit(*m_player, blockPosition))
        {
            m_effectManager.Spawn(EffectType::BlockSpark, blockPosition);
            m_audio.PlaySound(AudioId::SwordImpact, 0.95f, 1.32f);
        }
    }
}

void Scene_Game::UpdateCameraAndTrees()
{
    m_camera.SetTarget(m_player->GetPosition());
    UpdateCameraGroundConstraint();
    for (Tree* tree : m_trees)
        tree->FaceCamera(m_camera.GetPosition());

}

// 前フレームからの移動量と地形を使い、足音・砂煙・水しぶきを発生させる。
void Scene_Game::UpdateEnvironmentEffects(float deltaTime)
{
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
    const float playerMoveX = playerPosition.x - m_previousPlayerPosition.x;
    const float playerMoveZ = playerPosition.z - m_previousPlayerPosition.z;
    const float playerMoveDistance = std::sqrt(
        playerMoveX * playerMoveX + playerMoveZ * playerMoveZ);
    const float playerSpeed = deltaTime > 0.0f
        ? playerMoveDistance / deltaTime : 0.0f;
    const float pondX = playerPosition.x - Terrain::PondCenterX;
    const float pondZ = playerPosition.z - Terrain::PondCenterZ;
    const bool playerInWater = pondX * pondX + pondZ * pondZ <=
        Terrain::PondWaterRadius * Terrain::PondWaterRadius;
    DirectX::XMFLOAT3 moveDirection {};
    if (playerMoveDistance > 0.0001f)
    {
        moveDirection.x = playerMoveX / playerMoveDistance;
        moveDirection.z = playerMoveZ / playerMoveDistance;
    }
    auto footPosition = m_player->GetFootPosition();
    footPosition.y = Terrain::HeightAt(footPosition.x, footPosition.z);
    const bool evasionStarted = m_player->IsEvading() && !m_wasPlayerEvading;
    if (evasionStarted && m_player->IsGrounded() && !playerInWater)
        m_effectManager.SpawnFootstepDust(
            footPosition, moveDirection, true);
    if (evasionStarted && m_player->IsGrounded() && playerInWater)
    {
        const DirectX::XMFLOAT3 waterPosition {
            playerPosition.x, Terrain::PondWaterHeight, playerPosition.z };
        m_effectManager.SpawnWaterSplash(
            waterPosition, moveDirection, 1.6f);
    }
    if (!m_player->IsEvading() && m_player->IsGrounded() &&
        !playerInWater && playerSpeed > 0.2f)
    {
        m_footstepDustTimer -= deltaTime;
        if (m_footstepDustTimer <= 0.0f)
        {
            m_effectManager.SpawnFootstepDust(
                footPosition, moveDirection, false);
            m_footstepDustTimer = playerSpeed >= 5.0f ? 0.13f : 0.22f;
        }
    }
    else
    {
        m_footstepDustTimer = 0.0f;
    }
    if (!m_player->IsEvading() && m_player->IsGrounded() &&
        !playerInWater && playerSpeed > 0.2f)
    {
        m_playerFootstepTimer -= deltaTime;
        if (m_playerFootstepTimer <= 0.0f)
        {
            const AudioId step = (m_playerFootstepSequence++ & 1)
                ? AudioId::Footstep2 : AudioId::Footstep1;
            m_audio.PlaySound(step, 0.72f, playerSpeed >= 5.0f ? 1.08f : 0.96f);
            m_playerFootstepTimer = playerSpeed >= 5.0f ? 0.28f : 0.42f;
        }
    }
    else
    {
        m_playerFootstepTimer = 0.0f;
    }
    if (!m_player->IsEvading() && m_player->IsGrounded() &&
        playerInWater && playerSpeed > 0.2f)
    {
        m_playerWaterSplashTimer -= deltaTime;
        if (m_playerWaterSplashTimer <= 0.0f)
        {
            const DirectX::XMFLOAT3 waterPosition {
                playerPosition.x, Terrain::PondWaterHeight, playerPosition.z };
            m_effectManager.SpawnWaterSplash(waterPosition, moveDirection,
                std::clamp(playerSpeed / 4.0f, 0.35f, 1.25f));
            m_playerWaterSplashTimer = playerSpeed >= 5.0f ? 0.1f : 0.18f;
        }
    }
    else
    {
        m_playerWaterSplashTimer = 0.0f;
    }
    m_wasPlayerEvading = m_player->IsEvading();
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

    m_previousMiniCopyEffectPositions.resize(m_miniCopies.size());
    m_miniCopyWaterSplashTimers.resize(m_miniCopies.size());
    m_miniCopyFootstepTimers.resize(m_miniCopies.size());
    m_miniCopyWasDead.resize(m_miniCopies.size(), false);
    for (size_t i = 0; i < m_miniCopies.size(); ++i)
    {
        MiniCopy* miniCopy = m_miniCopies[i];
        const auto current = miniCopy->GetPosition();
        const bool isDead = miniCopy->IsDead();
        if (isDead && !m_miniCopyWasDead[i])
        {
            m_effectManager.Spawn(EffectType::EnemyDefeat, current);
            m_player->AddSpecialGauge(
                GameBalance::SpecialAttack::GaugePerDefeat);
        }
        m_miniCopyWasDead[i] = isDead;
        auto& previous = m_previousMiniCopyEffectPositions[i];
        auto& timer = m_miniCopyWaterSplashTimers[i];
        auto& footstepTimer = m_miniCopyFootstepTimers[i];
        const float dx = current.x - previous.x;
        const float dz = current.z - previous.z;
        const float distance = std::sqrt(dx * dx + dz * dz);
        if (isDead || distance > 1.0f || deltaTime <= 0.0f)
        {
            previous = current;
            timer = 0.0f;
            footstepTimer = 0.0f;
            continue;
        }
        const float miniPondX = current.x - Terrain::PondCenterX;
        const float miniPondZ = current.z - Terrain::PondCenterZ;
        const bool inWater = miniPondX * miniPondX + miniPondZ * miniPondZ <=
            Terrain::PondWaterRadius * Terrain::PondWaterRadius;
        const float speed = distance / deltaTime;
        if (!inWater && speed > 0.35f)
        {
            footstepTimer -= deltaTime;
            if (footstepTimer <= 0.0f)
            {
                const float listenerX = current.x - playerPosition.x;
                const float listenerZ = current.z - playerPosition.z;
                const float listenerDistance = std::sqrt(
                    listenerX * listenerX + listenerZ * listenerZ);
                if (listenerDistance < 24.0f)
                {
                    const float volume = 0.18f *
                        (1.0f - listenerDistance / 24.0f);
                    const AudioId step = (m_enemyFootstepSequence++ & 1)
                        ? AudioId::Footstep4 : AudioId::Footstep3;
                    m_audio.PlaySound(step, volume, 0.78f);
                }
                footstepTimer = speed >= 2.5f ? 0.30f : 0.42f;
            }
        }
        else
        {
            footstepTimer = 0.0f;
        }
        if (inWater && speed > 0.2f)
        {
            timer -= deltaTime;
            if (timer <= 0.0f)
            {
                DirectX::XMFLOAT3 direction {};
                if (distance > 0.0001f)
                    direction = { dx / distance, 0.0f, dz / distance };
                const DirectX::XMFLOAT3 waterPosition {
                    current.x, Terrain::PondWaterHeight, current.z };
                m_effectManager.SpawnWaterSplash(waterPosition, direction,
                    std::clamp(speed / 3.0f, 0.35f, 1.0f));
                timer = speed >= 2.5f ? 0.13f : 0.22f;
            }
        }
        else
        {
            timer = 0.0f;
        }
        previous = current;
    }

}
void Scene_Game::OnDraw(Renderer& renderer)
{
    renderer.SetCamera(m_camera.GetPosition(), m_camera.GetTarget());
    const auto secondaryInteractor = m_environmentMiniCopy
        ? m_environmentMiniCopy->GetPosition() : m_boss->GetPosition();
    renderer.SetGrassInteractors(m_player->GetPosition(), secondaryInteractor,
        m_playerWaterMovement, m_miniCopyWaterMovement);
    m_effectManager.Draw(renderer);
    const auto phase = m_boss->GetGlobalAttackPhase();
    if (phase == Boss::GlobalAttackPhase::Warning)
    {
        renderer.DrawBossAttackTelegraph(m_boss->GetGlobalAttackProgress(),
            m_bossAttackVisualTime);
    }
    else if (phase == Boss::GlobalAttackPhase::Flame)
    {
        const auto mouth = m_boss->GetMouthPosition();
        DirectX::XMFLOAT3 direction { -mouth.x,
            Terrain::BaseHeight - mouth.y, -mouth.z };
        renderer.DrawBossFlame(mouth, direction,
            m_boss->GetGlobalAttackProgress(), m_bossAttackVisualTime);
    }
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
