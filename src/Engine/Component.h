//-------------------------------------------------
//
//          Component.h
//
//  GameObject に付く機能の共通インターフェースを定義する。
//-------------------------------------------------

#pragma once

class GameObject;
class ComponentManager;
class Renderer;

// GameObjectへ追加する機能の基底。所有者の設定後にInitializeが呼ばれる。
class Component
{
public:
    virtual ~Component() = default;
    virtual bool Initialize() { return true; }
    virtual void Update(float deltaTime) { (void)deltaTime; }
    virtual void Draw(Renderer& renderer) { (void)renderer; }

    GameObject& GetOwner() { return *m_owner; }
    const GameObject& GetOwner() const { return *m_owner; }
    bool IsEnabled() const { return m_enabled; }
    void SetEnabled(bool enabled) { m_enabled = enabled; }

private:
    friend class ComponentManager;
    GameObject* m_owner = nullptr;
    bool m_enabled = true;
};
