#include "Camera.h"
#include "AnimationComponent.h"
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

    const auto tree = models.Load("assets/model/tree.gltf");
    const auto* treeData = models.GetModel(tree);
    assert(treeData && treeData->Valid());
    assert(treeData->primitives[0].vertices.size() == 4);
    assert(treeData->images.size() == 1 && treeData->images[0].Valid());

    const auto playerModel = models.Load(
        "assets/model/SwordMen/sword_and_shield.glb");
    const auto* playerData = models.GetModel(playerModel);
    assert(playerData && playerData->Valid());
    assert(playerData->skins.size() == 1);
    assert(playerData->skins[0].joints.size() == 69);
    assert(playerData->animations.size() >= 6);
    bool hasIdle = false, hasWalk = false, hasRun = false;
    bool hasAttack = false, hasJump = false, hasEvasion = false;
    for (const auto& animation : playerData->animations)
    {
        assert(!animation.channels.empty());
        hasIdle = hasIdle || animation.name == "Idle";
        hasWalk = hasWalk || animation.name == "Walk";
        hasRun = hasRun || animation.name == "Run";
        hasAttack = hasAttack || animation.name == "Attack";
        hasJump = hasJump || animation.name == "Jump";
        hasEvasion = hasEvasion || animation.name == "Evasion";
    }
    assert(hasIdle && hasWalk && hasRun && hasAttack && hasJump && hasEvasion);

    const auto enemyModel = models.Load("assets/model/Mutant/mutant_idle.glb");
    const auto* enemyData = models.GetModel(enemyModel);
    assert(enemyData && enemyData->Valid());
    assert(enemyData->skins.size() == 1);
    assert(enemyData->animations.size() == 4);
    bool enemyIdle = false, enemyWalk = false, enemyRun = false, enemyAttack = false;
    for (const auto& animation : enemyData->animations)
    {
        assert(!animation.channels.empty());
        enemyIdle = enemyIdle || animation.name == "Idle";
        enemyWalk = enemyWalk || animation.name == "Walk";
        enemyRun = enemyRun || animation.name == "Run";
        enemyAttack = enemyAttack || animation.name == "Attack";
    }
    assert(enemyIdle && enemyWalk && enemyRun && enemyAttack);
    AnimationComponent enemyAnimation(models, "assets/model/Mutant/mutant_idle.glb");
    assert(enemyAnimation.Initialize());
    assert(enemyAnimation.Play("Walk"));
    assert(enemyAnimation.Play("Run", true, 1.0f, 0.2f));
    assert(enemyAnimation.Play("Attack", false, 2.0f, 0.2f));

    AnimationComponent idleAnimation(models,
        "assets/model/SwordMen/sword_and_shield.glb");
    assert(idleAnimation.Initialize());
    const auto initialPose = idleAnimation.GetSkinMatrices();
    idleAnimation.Update(0.5f);
    const auto& animatedPose = idleAnimation.GetSkinMatrices();
    assert(animatedPose.size() == 69);
    bool poseChanged = false;
    for (size_t joint = 0; joint < animatedPose.size(); ++joint)
    {
        const float* before = &initialPose[joint]._11;
        const float* after = &animatedPose[joint]._11;
        for (size_t element = 0; element < 16; ++element)
        {
            assert(std::isfinite(after[element]));
            poseChanged = poseChanged || !Near(before[element], after[element]);
        }
    }
    assert(poseChanged);
    const auto idlePose = animatedPose;
    assert(idleAnimation.Play("Walk"));
    assert(idleAnimation.IsPlaying("Walk"));
    assert(!idleAnimation.Play("Missing"));
    idleAnimation.Update(0.3f);
    const auto& walkPose = idleAnimation.GetSkinMatrices();
    bool clipChanged = false;
    for (size_t joint = 0; joint < walkPose.size(); ++joint)
    {
        const float* idle = &idlePose[joint]._11;
        const float* walk = &walkPose[joint]._11;
        for (size_t element = 0; element < 16; ++element)
            clipChanged = clipChanged || !Near(idle[element], walk[element]);
    }
    assert(clipChanged);
    const auto beforeRun = walkPose;
    assert(idleAnimation.Play("Run"));
    idleAnimation.Update(0.3f);
    bool runChanged = false;
    const auto& runPose = idleAnimation.GetSkinMatrices();
    for (size_t joint = 0; joint < runPose.size(); ++joint)
    {
        const float* before = &beforeRun[joint]._11;
        const float* after = &runPose[joint]._11;
        for (size_t element = 0; element < 16; ++element)
        {
            assert(std::isfinite(after[element]));
            runChanged = runChanged || !Near(before[element], after[element]);
        }
    }
    assert(runChanged);
    assert(idleAnimation.Play("Idle", true, 1.0f, 0.2f));
    assert(idleAnimation.IsPlaying("Idle"));
    idleAnimation.Update(0.1f);
    idleAnimation.Update(0.1f);
    assert(idleAnimation.Play("Attack", false));
    assert(!idleAnimation.IsFinished());
    idleAnimation.Update(10.0f);
    assert(idleAnimation.IsFinished());
    assert(idleAnimation.Play("Jump", false));
    idleAnimation.SetNormalizedTime(0.5f);
    assert(!idleAnimation.IsFinished());
    idleAnimation.SetNormalizedTime(1.0f);
    assert(idleAnimation.IsFinished());
    assert(idleAnimation.Play("Evasion", false));

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
