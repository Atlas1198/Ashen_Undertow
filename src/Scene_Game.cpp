#include "Scene_Game.h"

#include "Boss.h"
#include "MiniCopy.h"
#include "Player.h"
#include "Tree.h"
#include "Collider.h"
#include "Input.h"
#include "GameBalance.h"

#include "Renderer.h"
#include "Terrain.h"
#include <algorithm>
#include <cmath>

using namespace GameBalance::Scene;

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
    m_effectTexture = ui.LoadImageFile(
        "assets/texture/effects/spark_particle.png");
    if (!m_effectTexture.IsValid()) return false;
    m_effectManager.SetTextureIndex(m_effectTexture.index);
    m_youDiedTexture = ui.LoadImageFile("assets/texture/ui/you_died.png");
    m_titleTexture = ui.LoadImageFile("assets/texture/ui/title.png");
    m_retryTexture = ui.LoadImageFile("assets/texture/ui/retry.png");
    m_clearTexture = ui.LoadImageFile("assets/texture/ui/clear.png");
    return m_youDiedTexture.IsValid() && m_titleTexture.IsValid() &&
        m_retryTexture.IsValid() && m_clearTexture.IsValid();
}

void Scene_Game::OnDrawUI(UIManager& ui)
{
    m_hud.Draw(ui, *m_player, *m_boss, m_miniCopies, m_camera);
    const bool showingDeath = m_deathScreenTime >= 0.0f;
    const bool showingClear = m_clearScreenTime >= 0.0f;
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
    const auto drawButton = [&](float x, UIImageHandle image, bool selected)
    {
        constexpr float Y = 655.0f;
        constexpr float Width = 280.0f;
        constexpr float Height = 82.0f;
        const DirectX::XMFLOAT4 border = selected
            ? DirectX::XMFLOAT4 { 0.75f, 0.08f, 0.035f, menuAlpha }
            : DirectX::XMFLOAT4 { 0.32f, 0.29f, 0.25f, menuAlpha * 0.8f };
        ui.DrawQuad(rectangle(x - 4.0f, Y - 4.0f, Width + 8.0f, Height + 8.0f), border);
        ui.DrawQuad(rectangle(x, Y, Width, Height),
            { 0.025f, 0.02f, 0.018f, menuAlpha * 0.92f });
        ui.DrawImage(image, { x + 26.0f, Y + 8.0f, Width - 52.0f, Height - 16.0f },
            { 1.0f, 1.0f, 1.0f, menuAlpha * (selected ? 1.0f : 0.58f) });
    };
    if (showingClear)
    {
        drawButton(660.0f, m_titleTexture, true);
        return;
    }
    drawButton(350.0f, m_titleTexture, m_deathMenuChoice == 0);
    drawButton(970.0f, m_retryTexture, m_deathMenuChoice == 1);
}

void Scene_Game::OnUpdate(float deltaTime)
{
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
                RequestScene(m_deathMenuChoice == 0
                    ? SceneRequest::Title : SceneRequest::Retry);
        }
        return;
    }

    if (m_clearScreenTime >= 0.0f)
    {
        m_clearScreenTime += deltaTime;
        if (m_clearScreenTime >= 1.45f &&
            (m_input.AttackPressed() || m_input.JumpPressed() ||
             m_input.ConfirmPressed()))
            RequestScene(SceneRequest::Title);
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

void Scene_Game::OnAfterUpdate(float deltaTime)
{
    m_effectManager.Update(deltaTime);
    m_player->ResolveTerrainContact();
    if (m_player->IsDead())
    {
        if (m_player->IsDeathAnimationFinished())
            m_deathScreenTime = 0.0f;
        m_hud.Update(*m_player, deltaTime);
        UpdateCameraAndTrees();
        return;
    }
    UpdateSpecialAttack(deltaTime);
    UpdatePlayerEffects(deltaTime);
    UpdateMiniCopySpawning(deltaTime);
    ResolveCombat();
    if (m_boss->IsDead() && !m_player->IsDead() && m_clearScreenTime < 0.0f)
        m_clearScreenTime = 0.0f;
    UpdateBossGlobalAttack();
    m_hud.Update(*m_player, deltaTime);
    UpdateCameraAndTrees();
    UpdateEnvironmentEffects(deltaTime);
    m_bossAttackVisualTime += deltaTime;
}

void Scene_Game::UpdateBossGlobalAttack()
{
    if (m_boss->GetGlobalAttackPhase() != Boss::GlobalAttackPhase::Flame)
        return;
    const auto& position = m_player->GetPosition();
    const float pondX = position.x - Terrain::PondCenterX;
    const float pondZ = position.z - Terrain::PondCenterZ;
    const bool inWater = pondX * pondX + pondZ * pondZ <=
        Terrain::PondWaterRadius * Terrain::PondWaterRadius;
    if (!inWater)
        m_player->TakeDamage(GameBalance::Boss::GlobalAttackDamage);
}

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

void Scene_Game::TryLaunchSpecialAttack()
{
    if (!m_input.SpecialAttackPressed() || m_specialAttack.IsActive() ||
        !m_player->IsSpecialGaugeFull()) return;

    auto start = m_player->GetPosition();
    start.y += 1.0f;
    if (m_specialAttack.Launch(start, m_boss->GetSpecialAttackTarget()))
    {
        m_player->ConsumeSpecialGauge();
        m_specialAttackEffectTimer = 0.0f;
    }
}

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

    m_boss->TakeDamage(GameBalance::SpecialAttack::Damage);

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

void Scene_Game::UpdateMiniCopySpawning(float deltaTime)
{
    for (auto& pending : m_pendingMiniCopySpawns)
    {
        if (!pending.lightningStarted)
        {
            m_effectManager.SpawnEnemyArrivalLightning(pending.position);
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
        }
        if (miniCopy->IsDead())
        {
            miniCopy->EnterDefeatedState();
            continue;
        }
        DirectX::XMFLOAT3 blockPosition {};
        if (miniCopy->ResolveAttackHit(*m_player, blockPosition))
            m_effectManager.Spawn(EffectType::BlockSpark, blockPosition);
    }
}

void Scene_Game::UpdateCameraAndTrees()
{
    m_camera.SetTarget(m_player->GetPosition());
    UpdateCameraGroundConstraint();
    for (Tree* tree : m_trees)
        tree->FaceCamera(m_camera.GetPosition());

}

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
        const float dx = current.x - previous.x;
        const float dz = current.z - previous.z;
        const float distance = std::sqrt(dx * dx + dz * dz);
        if (isDead || distance > 1.0f || deltaTime <= 0.0f)
        {
            previous = current;
            timer = 0.0f;
            continue;
        }
        const float miniPondX = current.x - Terrain::PondCenterX;
        const float miniPondZ = current.z - Terrain::PondCenterZ;
        const bool inWater = miniPondX * miniPondX + miniPondZ * miniPondZ <=
            Terrain::PondWaterRadius * Terrain::PondWaterRadius;
        const float speed = distance / deltaTime;
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
