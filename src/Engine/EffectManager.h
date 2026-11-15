//-------------------------------------------------
//
//          EffectManager.h
//
//  一時エフェクトの生成・更新・描画に使う型を定義する。
//-------------------------------------------------

#pragma once

#include <DirectXMath.h>
#include <array>
#include <cstdint>
#include <vector>

class Renderer;

// 剣の根元と先端。複数フレーム分をつないで軌跡にする。
struct SwordTrailPose
{
    DirectX::XMFLOAT3 start {};
    DirectX::XMFLOAT3 end {};
};

enum class EffectType
{
    Custom,
    HitSpark,
    ChargeHit,
    SwordTrail,
    BlockSpark,
    ChargeAura,
    FootstepDust,
    WaterSplash,
    EnemyDefeat,
    BossTelegraph,
    WaterRipple,
    SpecialSlash
};

// 生成時のパラメータ。速度はワールド単位/秒、寿命は秒。
struct EffectDesc
{
    EffectType type = EffectType::Custom;
    DirectX::XMFLOAT3 position {};
    DirectX::XMFLOAT3 velocity {};
    DirectX::XMFLOAT3 acceleration {};
    DirectX::XMFLOAT4 startColor { 1, 1, 1, 1 };
    DirectX::XMFLOAT4 endColor { 1, 1, 1, 0 };
    float startSize = 1.0f;
    float endSize = 0.0f;
    float lifetime = 1.0f;
    float drag = 0.0f;
    float strength = 1.0f;
    DirectX::XMFLOAT3 visualDirection {};
    float visualLength = 0.0f;
};

// 再生中の状態。age に応じて色と大きさを始点から終点へ補間する。
struct EffectInstance
{
    std::uint64_t id = 0;
    EffectType type = EffectType::Custom;
    DirectX::XMFLOAT3 position {};
    DirectX::XMFLOAT3 velocity {};
    DirectX::XMFLOAT3 acceleration {};
    DirectX::XMFLOAT4 color { 1, 1, 1, 1 };
    DirectX::XMFLOAT4 startColor { 1, 1, 1, 1 };
    DirectX::XMFLOAT4 endColor { 1, 1, 1, 0 };
    float size = 1.0f;
    float startSize = 1.0f;
    float endSize = 0.0f;
    float age = 0.0f;
    float lifetime = 1.0f;
    float drag = 0.0f;
    float strength = 1.0f;
    DirectX::XMFLOAT3 visualDirection {};
    float visualLength = 0.0f;
};

// 一時的なパーティクルを管理し、専用の生成関数で演出を組み立てる。
class EffectManager
{
public:
    std::uint64_t Spawn(const EffectDesc& desc);
    std::uint64_t Spawn(EffectType type, const DirectX::XMFLOAT3& position);
    void SpawnChargeAura(const DirectX::XMFLOAT3& position,
        float chargeSeconds);
    void SpawnChargeHit(const DirectX::XMFLOAT3& position, float chargePower);
    void SpawnSwordTrail(const std::array<SwordTrailPose, 4>& poses);
    void SpawnFootstepDust(const DirectX::XMFLOAT3& position,
        const DirectX::XMFLOAT3& moveDirection, bool evasion);
    void SpawnWaterSplash(const DirectX::XMFLOAT3& position,
        const DirectX::XMFLOAT3& moveDirection, float intensity);
    void SpawnWaterRipple(const DirectX::XMFLOAT3& position, float intensity);
    void SpawnSpecialSlash(const DirectX::XMFLOAT3& position,
        const DirectX::XMFLOAT3& forward);
    void SpawnSpecialImpact(const DirectX::XMFLOAT3& position,
        const DirectX::XMFLOAT3& forward);
    void SpawnEnemyArrivalLightning(const DirectX::XMFLOAT3& position);
    void Stop(std::uint64_t id);
    void Update(float deltaTime);
    void Draw(Renderer& renderer) const;
    void SetTextureIndex(std::uint32_t index) { m_textureIndex = index; }
    void Clear() { m_effects.clear(); }

    const std::vector<EffectInstance>& GetActiveEffects() const
    {
        return m_effects;
    }

private:
    float NextRandom();
    std::vector<EffectInstance> m_effects;
    // ID は発生したエフェクトを個別に停止するための識別子。
    std::uint64_t m_nextId = 1;
    std::uint32_t m_textureIndex = UINT32_MAX;
    std::uint32_t m_randomState = 0x9e3779b9u;
};
