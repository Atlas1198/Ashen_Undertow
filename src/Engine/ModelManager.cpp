#include "ModelManager.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

ModelHandle ModelManager::Load(const std::string& path)
{
    if (path.empty()) return {};
    std::error_code error;
    const auto normalized = std::filesystem::weakly_canonical(path, error);
    if (error) return {};
    const auto key = normalized.generic_string();
    if (const auto found = m_handles.find(key); found != m_handles.end())
        return found->second;

    std::string extension = normalized.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    auto model = std::make_unique<GltfModel>();
    if ((extension != ".glb" && extension != ".gltf") ||
        !LoadGltf(key.c_str(), *model) || !model->Valid())
    {
        return {};
    }

    if (m_models.size() >= ModelHandle::InvalidValue) return {};
    const ModelHandle handle { static_cast<uint32_t>(m_models.size()) };
    m_models.emplace_back(std::move(model));
    m_handles.emplace(key, handle);
    return handle;
}

const GltfModel* ModelManager::GetModel(ModelHandle handle) const
{
    if (!handle.IsValid() || handle.value >= m_models.size())
        return nullptr;
    return m_models[handle.value].get();
}
