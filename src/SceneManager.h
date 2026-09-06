#pragma once

// 各シーンが共通して実装するインターフェース。
class SceneManager
{
public:
    virtual ~SceneManager() = default;

    virtual bool Initialize() = 0;
    virtual void Update() = 0;
};
