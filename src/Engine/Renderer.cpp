//-------------------------------------------------
//
//          Renderer.cpp
//
//  DX12 レンダラを段階的に初期化し、GPU 同期を管理する。
//-------------------------------------------------

#include "RendererInternal.h"

using namespace DirectX;
using namespace RendererDetail;

Renderer::~Renderer()
{
    // ComPtr が解放される前に、GPU がリソースを使い終わるのを待つ。
    // これを忘れると「使用中のリソースを破棄した」ことになり得る。
    if (m_commandQueue && m_fence && m_fenceEvent)
    {
        WaitForGpu();
    }
    if (m_fenceEvent)
    {
        CloseHandle(m_fenceEvent);
    }
}

bool Renderer::Initialize(HWND hwnd, uint32_t width, uint32_t height,
    ModelManager& modelManager)
{
    m_width  = width;
    m_height = height;

    uint32_t materialStart = kSrvMaterialStart;
    for (size_t i = 0; i < modelManager.GetModelCount(); ++i)
    {
        const ModelHandle handle { static_cast<uint32_t>(i) };
        const auto* model = modelManager.GetModel(handle);
        if (!model || !model->Valid()) continue;

        GpuModel gpuModel;
        gpuModel.model = model;
        gpuModel.handle = handle;
        gpuModel.materialStart = materialStart;
        materialStart += static_cast<uint32_t>(model->materials.size()) *
            kTexturesPerMaterial;
        m_models.push_back(std::move(gpuModel));
    }
    if (m_models.empty()) return false;

    if (!CreateDevice())            return false;
    if (!CreateCommandObjects())    return false;
    if (!CreateSwapChain(hwnd))     return false;
    if (!CreateRenderTargets())     return false;
    if (!CreateFence())             return false;
    if (!CreateTrianglePipeline())  return false;
    if (!CreateColliderDebugResources()) return false;
    if (!CreateVertexBuffer())      return false;
    if (!CreateConstantBuffer())     return false;
	if (!CreateDepthBuffer())        return false;
    if (!CreateEnvironmentTexture()) return false;
    if (!CreateSkyboxPipeline())     return false;
    if (!CreateIrradianceMap())      return false;
    if (!CreatePrefilterMap())       return false;
    if (!CreateBrdfLut())            return false;
    if (!CreateMaterialTextures())  return false;
	if (!CreateFloor())             return false;
    if (!CreateShadowMap())         return false;
    if (!CreateShadowPipeline())    return false;
    if (!CreateHdrBuffer())         return false;
    if (!CreateTonemapPipeline())   return false;
    if (!CreateBloomResources())    return false;
    if (!CreateBloomPipelines())    return false;
    if (!CreateUIResources())       return false;
    if (!CreateEffectResources())   return false;
    return true;
}

void Renderer::WaitForGpu()
{
    const uint64_t fenceValue = m_fenceValues[m_frameIndex];
    m_commandQueue->Signal(m_fence.Get(), fenceValue);

    m_fence->SetEventOnCompletion(fenceValue, m_fenceEvent);
    WaitForSingleObject(m_fenceEvent, INFINITE);

    m_fenceValues[m_frameIndex] = fenceValue + 1;
}
