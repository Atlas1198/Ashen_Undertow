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
    ModelManager& modelManager, ModelHandle model)
{
    m_width  = width;
    m_height = height;

    m_modelHandle = model;
    m_model = modelManager.GetModel(m_modelHandle);
    if (!m_model) return false;

    if (!CreateDevice())            return false;
    if (!CreateCommandObjects())    return false;
    if (!CreateSwapChain(hwnd))     return false;
    if (!CreateRenderTargets())     return false;
    if (!CreateFence())             return false;
    if (!CreateTrianglePipeline())  return false;
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
