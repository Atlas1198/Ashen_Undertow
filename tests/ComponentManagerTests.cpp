#include "ObjectManager.h"
#include "ModelManager.h"
#include <cassert>

struct Probe : Component
{
    Probe(int& destroyed, bool success = true) : destroyed(destroyed), success(success) {}
    ~Probe() override { ++destroyed; }
    bool Initialize() override { ownerAtInit = &GetOwner(); return success; }
    void Update(float dt) override { ++updates; elapsed += dt; position = GetOwner().GetPosition().x; }
    int& destroyed;
    bool success;
    GameObject* ownerAtInit = nullptr;
    int updates = 0;
    float elapsed = 0;
    float position = 0;
};

struct SpawnDuringUpdate : Component
{
    explicit SpawnDuringUpdate(int& destroyed) : destroyed(destroyed) {}
    void Update(float) override
    {
        for (int i = 0; i < 32; ++i)
            last = GetOwner().AddComponent<Probe>(destroyed);
        SetEnabled(false);
    }
    int& destroyed;
    Probe* last = nullptr;
};

struct TestObject : GameObject
{
    explicit TestObject(int& destroyed) : destroyed(destroyed) {}
    bool Initialize(ModelManager&) override { return AddComponent<Probe>(destroyed) != nullptr; }
    void Update(float) override { SetPosition(5, 0, 0); }
    void Draw(Renderer&) override {}
    int& destroyed;
};

int main()
{
    int destroyed = 0;
    ModelManager models;
    {
        ObjectManager objects(models);
        auto* object = objects.AddGameObject<TestObject>(destroyed);
        assert(object);
        auto* probe = object->GetComponent<Probe>();
        assert(probe && probe->ownerAtInit == object);
        assert(static_cast<const TestObject*>(object)->GetComponent<Probe>() == probe);
        assert(!object->GetComponent<SpawnDuringUpdate>());
        assert(!object->AddComponent<Probe>(destroyed, false));
        assert(destroyed == 1);

        objects.Update(0.25f);
        assert(probe->updates == 1 && probe->elapsed == 0.25f && probe->position == 5);
        probe->SetEnabled(false);
        objects.Update(0.25f);
        assert(probe->updates == 1);
        probe->SetEnabled(true);
        auto* spawner = object->AddComponent<SpawnDuringUpdate>(destroyed);
        objects.Update(0.25f);
        assert(spawner->last && spawner->last->updates == 0);
        assert(object->GetComponent<Probe>() == probe);
        objects.Update(0.25f);
        assert(spawner->last->updates == 1 && probe->updates == 3);
    }
    assert(destroyed == 34); // 失敗1個 + 登録済み33個
}
