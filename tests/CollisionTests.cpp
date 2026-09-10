#include "ObjectManager.h"
#include "ModelManager.h"
#include "Collider.h"
#include "Rigidbody.h"
#include <cassert>
#include <limits>

struct TestObject : GameObject
{
    bool Initialize(ModelManager&) override { return true; }
    void Update(float dt) override { m_position.x += speed * dt; }
    void Draw(Renderer&) override {}
    float speed = 0;
};

Collider* AddShape(TestObject& object, int type)
{
    if (type == 0) return object.AddComponent<SphereCollider>();
    if (type == 1) return object.AddComponent<CapsuleCollider>();
    return object.AddComponent<BoxCollider>();
}

void Check(const Collider& a, const Collider& b, bool expected)
{
    assert(Collider::Intersects(a, b) == expected);
    assert(Collider::Intersects(b, a) == expected);
}

int main()
{
    // 6組み合わせ: 包含、水平接触、分離、上下端の接触。
    const float height[] = { 0.5f, 1.0f, 0.5f };
    for (int i = 0; i < 3; ++i)
    {
        for (int j = i; j < 3; ++j)
        {
            TestObject a, b;
            auto* ca = AddShape(a, i);
            auto* cb = AddShape(b, j);
            Check(*ca, *cb, true);
            b.SetPosition(1, 0, 0);
            Check(*ca, *cb, true);
            b.SetPosition(1.01f, 0, 0);
            Check(*ca, *cb, false);
            b.SetPosition(0, height[i] + height[j], 0);
            Check(*ca, *cb, true);
            b.SetPosition(0, height[i] + height[j] + 0.01f, 0);
            Check(*ca, *cb, false);
        }
    }

    TestObject box, sphere, capsule;
    auto* bc = box.AddComponent<BoxCollider>();
    auto* sc = sphere.AddComponent<SphereCollider>();
    auto* cc = capsule.AddComponent<CapsuleCollider>();
    // 拡張AABBだけでは誤検出する角のケース。
    sphere.SetPosition(0.9f, 0.9f, 0.9f);
    Check(*sc, *bc, false);
    sphere.SetPosition(0.75f, 0.75f, 0.75f);
    Check(*sc, *bc, true);
    capsule.SetPosition(0.9f, 1.4f, 0.9f);
    Check(*cc, *bc, false);
    capsule.SetPosition(0.75f, 1.25f, 0.75f);
    Check(*cc, *bc, true);
    capsule.SetPosition(0, 0, 0);
    sphere.SetPosition(0, 1.5f, 0);
    Check(*sc, *cc, true);
    sphere.SetPosition(0.1f, 1.5f, 0);
    Check(*sc, *cc, false);

    // 高さゼロのカプセルは球、サイズゼロの箱は点として扱える。
    TestObject point, zeroCapsule;
    auto* pc = point.AddComponent<BoxCollider>(0.0f, 0.0f, 0.0f);
    auto* zc = zeroCapsule.AddComponent<CapsuleCollider>(0.5f, 0.0f);
    point.SetPosition(0.5f, 0, 0);
    Check(*pc, *zc, true);
    point.SetPosition(0.51f, 0, 0);
    Check(*pc, *zc, false);
    assert(!point.AddComponent<SphereCollider>(-1.0f));
    assert(!point.AddComponent<CapsuleCollider>(1.0f, -1.0f));
    assert(!point.AddComponent<BoxCollider>(1.0f, -1.0f, 1.0f));
    assert(!point.AddComponent<SphereCollider>(std::numeric_limits<float>::infinity()));

    ModelManager models;
    ObjectManager objects(models);
    auto* a = objects.AddGameObject<TestObject>();
    auto* b = objects.AddGameObject<TestObject>();
    auto* ca = a->AddComponent<SphereCollider>();
    auto* extra = a->AddComponent<BoxCollider>();
    auto* cb = b->AddComponent<CapsuleCollider>();
    objects.Update(0);
    assert(ca->GetContacts().size() == 1 && ca->GetContacts()[0] == cb);
    assert(cb->GetContacts().size() == 2); // 同じ所有者の球・箱同士は判定されない
    b->speed = 10;
    objects.Update(1);
    assert(!ca->IsColliding() && !cb->IsColliding()); // 移動後の座標で判定
    cb->SetOffset(-10, 0, 0);
    objects.Update(0);
    assert(ca->IsColliding());
    cb->SetEnabled(false);
    objects.Update(0);
    assert(ca->GetContacts().empty() && cb->GetContacts().empty() && !extra->IsColliding());
    cb->SetEnabled(true);
    objects.Update(0);
    assert(ca->IsColliding());
    assert(a->AddComponent<SphereCollider>());
    objects.Update(0);
    assert(cb->GetContacts().size() == 3); // 後から追加した判定も自動収集

    // Rigidbody付きObjectは自動で押し出され、衝突面へ向かう速度も除去される。
    {
        ObjectManager resolution(models);
        auto* dynamic = resolution.AddGameObject<TestObject>();
        auto* fixed = resolution.AddGameObject<TestObject>();
        auto* dynamicCollider = dynamic->AddComponent<SphereCollider>();
        auto* fixedCollider = fixed->AddComponent<BoxCollider>();
        auto* body = dynamic->AddComponent<Rigidbody>();
        body->SetUseGravity(false);
        body->SetVelocity(1.0f, 0.0f, 0.0f);
        fixed->SetPosition(0.75f, 0.0f, 0.0f);
        resolution.Update(0.0f);
        assert(std::abs(dynamic->GetPosition().x + 0.25f) < 0.001f);
        assert(std::abs(body->GetVelocity().x) < 0.001f);
        assert(dynamicCollider->HasContactWith(*fixed));
        assert(fixedCollider->HasContactWith(*dynamic));
    }

    // Triggerは接触情報を残すが、物体を押し出さない。
    {
        ObjectManager triggers(models);
        auto* dynamic = triggers.AddGameObject<TestObject>();
        auto* sensor = triggers.AddGameObject<TestObject>();
        auto* dynamicCollider = dynamic->AddComponent<SphereCollider>();
        auto* trigger = sensor->AddComponent<BoxCollider>();
        auto* body = dynamic->AddComponent<Rigidbody>();
        body->SetUseGravity(false);
        trigger->SetTrigger(true);
        sensor->SetPosition(0.75f, 0.0f, 0.0f);
        triggers.Update(0.0f);
        assert(std::abs(dynamic->GetPosition().x) < 0.001f);
        assert(trigger->IsTrigger() && trigger->IsTriggering());
        assert(trigger->HasContactWith(*dynamic));
        assert(dynamicCollider->HasContactWith(*sensor));
    }
}
