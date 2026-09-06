#include "ModelManager.h"

#include <algorithm>
#include <cctype>

ModelHandle ModelManager::Load(const std::string& path)
{
    if (const auto found = m_handles.find(path); found != m_handles.end())
        return found->second;

    std::string extension;
    if (const size_t dot = path.find_last_of('.'); dot != std::string::npos)
        extension = path.substr(dot);
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    auto model = std::make_unique<GltfModel>();
    if ((extension != ".glb" && extension != ".gltf") ||
        !LoadGltf(path.c_str(), *model) || !model->Valid())
    {
        return {};
    }

    const ModelHandle handle { static_cast<uint32_t>(m_models.size()) };
    m_models.emplace_back(std::move(model));
    m_handles.emplace(path, handle);
    return handle;
}

const GltfModel* ModelManager::GetModel(ModelHandle handle) const
{
    if (!handle.IsValid() || handle.value >= m_models.size())
        return nullptr;
    return m_models[handle.value].get();
}
