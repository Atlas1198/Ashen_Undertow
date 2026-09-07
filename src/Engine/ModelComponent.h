#pragma once

#include <string>
#include "Component.h"
#include "ModelHandle.h"

class ModelManager;

class ModelComponent final : public Component
{
public:
    ModelComponent(ModelManager& models, const std::string& path) : m_models(models), m_path(path) {}
    bool Initialize() override;
    void Draw(Renderer& renderer) override;
    ModelHandle GetModelHandle() const { return m_model; }

private:
    ModelManager& m_models;
    std::string m_path;
    ModelHandle m_model;
};
