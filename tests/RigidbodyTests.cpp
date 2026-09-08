#include "ObjectManager.h"
#include "ModelManager.h"
#include "Rigidbody.h"
#include <cassert>
#include <cmath>

namespace
{
bool Near(float actual, float expected)
{
    return std::abs(actual - expected) < 0.0001f;
}

struct PhysicsObject : GameObject
{
    bool Initialize(ModelManager&) override
    {
        body = AddComponent<Rigidbody>();
        return body != nullptr;
    }
    Rigidbody* body = nullptr;
};
}

int main()
{
    ModelManager models;
    ObjectManager objects(models);
    auto* object = objects.AddGameObject<PhysicsObject>();
    assert(object && object->body);

    auto* body = object->body;
    body->SetUseGravity(false);
    body->SetMass(2.0f);
    body->AddForce(4.0f, 0.0f, 0.0f);
    objects.Update(0.5f);
    assert(Near(body->GetVelocity().x, 1.0f));
    assert(Near(object->GetPosition().x, 0.5f));

    // 力は1回の更新後に消え、衝撃は質量に応じて即座に速度へ加算される。
    objects.Update(0.5f);
    assert(Near(body->GetVelocity().x, 1.0f));
    body->AddImpulse(2.0f, 0.0f, 0.0f);
    assert(Near(body->GetVelocity().x, 2.0f));

    body->SetLinearDrag(1.0f);
    objects.Update(1.0f);
    assert(Near(body->GetVelocity().x, 1.0f));

    body->SetLinearDrag(0.0f);
    body->SetVelocity(0.0f, 0.0f, 0.0f);
    body->SetUseGravity(true);
    objects.Update(1.0f);
    assert(Near(body->GetVelocity().y, -9.81f));

    const auto position = object->GetPosition();
    body->SetKinematic(true);
    body->AddForce(100.0f, 100.0f, 100.0f);
    body->AddImpulse(100.0f, 100.0f, 100.0f);
    objects.Update(1.0f);
    assert(Near(object->GetPosition().x, position.x));
    assert(Near(object->GetPosition().y, position.y));
    assert(Near(object->GetPosition().z, position.z));
}
