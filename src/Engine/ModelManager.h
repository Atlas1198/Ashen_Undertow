#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "GltfLoader.h"
#include "ModelHandle.h"

// モデルをパス単位で一度だけ読み込み、ハンドルで共有する。
class ModelManager
{
public:
    ModelHandle Load(const std::string& path);
    const GltfModel* GetModel(ModelHandle handle) const;

private:
    std::unordered_map<std::string, ModelHandle> m_handles;
    std::vector<std::unique_ptr<GltfModel>> m_models;
};
