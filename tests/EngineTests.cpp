#include "Camera.h"
#include "ModelManager.h"
#include "ObjectManager.h"
#include <cassert>
#include <cmath>
#include <filesystem>

// This executable tests dispatch only; it does not link the DX12 renderer.
class Renderer {};

namespace
{
bool Near(float a, float b) { return std::abs(a - b) < 0.001f; }

struct DrawProbe : Component
{
    void Draw(Renderer&) override { ++draws; }
    int draws = 0;
};

struct Child : GameObject
{
    bool Initialize(ModelManager&) override { return AddComponent<DrawProbe>() != nullptr; }
    void Update(float) override { ++updates; }
    int updates = 0;
};

struct Spawner : GameObject
{
    explicit Spawner(ObjectManager& objects) : objects(objects) {}
    bool Initialize(ModelManager&) override { return true; }
    void Update(float) override
    {
        if (last) return;
        for (int i = 0; i < 128; ++i) last = objects.AddGameObject<Child>();
    }
    ObjectManager& objects;
    Child* last = nullptr;
};
}

int main()
{
    Camera camera;
    auto position = camera.GetPosition();
    assert(Near(position.x, 0) && Near(position.y, 2.5f) && Near(position.z, -6));
    camera.SetTarget({ 10, 2, 3 });
    position = camera.GetPosition();
    assert(Near(position.x, 10) && Near(position.y, 4.5f) && Near(position.z, -3));
    camera.Rotate(10, 0);
    assert(camera.GetPosition().x < camera.GetTarget().x); // Inverted horizontal look.
    for (float pitch : { 100000.0f, -200000.0f })
    {
        camera.Rotate(100000, pitch);
        position = camera.GetPosition();
        const auto target = camera.GetTarget();
        const float x = position.x - target.x, y = position.y - target.y, z = position.z - target.z;
        assert(std::isfinite(x) && std::isfinite(y) && std::isfinite(z));
        assert(Near(std::sqrt(x*x + y*y + z*z), 6.5f));
        assert(x*x + z*z > 0.01f); // Never parallel to the camera up vector.
    }

    ModelManager models;
    const auto model = models.Load("tests/fixtures/triangle.gltf");
    assert(model.IsValid());
    const auto* data = models.GetModel(model);
    assert(data && data->Valid());
    assert(models.Load("./tests/fixtures/../fixtures/triangle.gltf") == model);
    assert(models.Load(std::filesystem::absolute("tests/fixtures/triangle.gltf").string()) == model);
    assert(!models.Load("").IsValid());
    assert(!models.Load("tests/fixtures/missing.gltf").IsValid());
    assert(!models.Load("tests/fixtures/triangle.fbx").IsValid());
    assert(!models.GetModel({}));
    assert(models.GetModel(model) == data);

    ObjectManager objects(models);
    auto* spawner = objects.AddGameObject<Spawner>(objects);
    objects.Update(0.016f);
    assert(spawner->last && spawner->last->updates == 0);
    objects.Update(0.016f);
    assert(spawner->last->updates == 1);
    Renderer renderer;
    auto* probe = spawner->last->GetComponent<DrawProbe>();
    objects.Draw(renderer);
    assert(probe->draws == 1);
    probe->SetEnabled(false);
    objects.Draw(renderer);
    assert(probe->draws == 1);
}
