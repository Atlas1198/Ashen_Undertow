#include "RendererInternal.h"
#include "UIManager.h"

#include <algorithm>
#include <cstring>

using namespace DirectX;
using namespace RendererDetail;

bool Renderer::CreateEffectResources()
{
    D3D12_DESCRIPTOR_RANGE range = {};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 1;
    range.BaseShaderRegister = 0;

    D3D12_ROOT_PARAMETER parameters[2] = {};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[0].Constants.ShaderRegister = 0;
    parameters[0].Constants.Num32BitValues = 20;
    parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[1].DescriptorTable = { 1, &range };
    parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW =
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC root = {};
    root.NumParameters = _countof(parameters);
    root.pParameters = parameters;
    root.NumStaticSamplers = 1;
    root.pStaticSamplers = &sampler;
    root.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ComPtr<ID3DBlob> signature, error;
    if (!Check(D3D12SerializeRootSignature(&root,
        D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error))) return false;
    if (!Check(m_device->CreateRootSignature(0, signature->GetBufferPointer(),
        signature->GetBufferSize(), IID_PPV_ARGS(&m_effectRootSignature))))
        return false;

    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    ComPtr<ID3DBlob> vs, ps;
    const auto compile = [&](const char* entry, const char* target,
        ComPtr<ID3DBlob>& blob)
    {
        error.Reset();
        const auto hr = D3DCompileFromFile(L"shaders/Effects.hlsl", nullptr,
            nullptr, entry, target, flags, 0, &blob, &error);
        if (error) OutputDebugStringA(
            static_cast<const char*>(error->GetBufferPointer()));
        return Check(hr);
    };
    if (!compile("VSMain", "vs_5_0", vs) ||
        !compile("PSMain", "ps_5_0", ps)) return false;

    const D3D12_INPUT_ELEMENT_DESC elements[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
          D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
        { "DIRECTION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12,
          D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24,
          D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
        { "SIZE", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 40,
          D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 }
    };
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = {};
    pso.pRootSignature = m_effectRootSignature.Get();
    pso.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    pso.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    pso.InputLayout = { elements, _countof(elements) };
    pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pso.RasterizerState.DepthClipEnable = TRUE;
    auto& blend = pso.BlendState.RenderTarget[0];
    blend.BlendEnable = TRUE;
    blend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    blend.DestBlend = D3D12_BLEND_ONE;
    blend.BlendOp = D3D12_BLEND_OP_ADD;
    blend.SrcBlendAlpha = D3D12_BLEND_ONE;
    blend.DestBlendAlpha = D3D12_BLEND_ONE;
    blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pso.DepthStencilState.DepthEnable = TRUE;
    pso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    pso.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    pso.SampleMask = UINT_MAX;
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    pso.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    pso.SampleDesc.Count = 1;
    if (!Check(m_device->CreateGraphicsPipelineState(
        &pso, IID_PPV_ARGS(&m_effectPSO)))) return false;
    blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    blend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    if (!Check(m_device->CreateGraphicsPipelineState(
        &pso, IID_PPV_ARGS(&m_effectAlphaPSO)))) return false;

    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC buffer = {};
    buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer.Width = sizeof(EffectParticleVertex) * kMaxEffectParticles;
    buffer.Height = 1;
    buffer.DepthOrArraySize = buffer.MipLevels = 1;
    buffer.SampleDesc.Count = 1;
    buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    for (uint32_t frame = 0; frame < kFrameCount; ++frame)
    {
        if (!Check(m_device->CreateCommittedResource(&heap,
            D3D12_HEAP_FLAG_NONE, &buffer,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&m_effectInstanceBuffers[frame])))) return false;
        D3D12_RANGE noRead { 0, 0 };
        if (!Check(m_effectInstanceBuffers[frame]->Map(
            0, &noRead, &m_effectInstanceMapped[frame]))) return false;
    }
    return true;
}

void Renderer::DrawEffectParticle(uint32_t textureIndex,
    const XMFLOAT3& position, const XMFLOAT3& direction,
    const XMFLOAT4& color, float width, float length, bool additive)
{
    if (m_effectParticles.size() + m_alphaEffectParticles.size() >=
        kMaxEffectParticles) return;
    if (m_effectParticles.empty() && m_alphaEffectParticles.empty())
        m_effectTextureIndex = textureIndex;
    if (textureIndex != m_effectTextureIndex) return;
    auto& particles = additive ? m_effectParticles : m_alphaEffectParticles;
    particles.push_back({ position, direction, color, { width, length } });
}

void Renderer::DrawWaterRipple(const XMFLOAT3& position, float age, float strength)
{
    if (m_waterRipples.size() >= kMaxWaterRipples) return;
    m_waterRipples.push_back({ position.x, position.z, age, strength });
}

void Renderer::DrawEffects(const XMMATRIX& viewProjection)
{
    const size_t totalCount =
        m_effectParticles.size() + m_alphaEffectParticles.size();
    if (totalCount == 0 || m_effectTextureIndex == UINT32_MAX) return;
    auto* mapped = static_cast<EffectParticleVertex*>(
        m_effectInstanceMapped[m_frameIndex]);
    std::copy(m_effectParticles.begin(), m_effectParticles.end(), mapped);
    std::copy(m_alphaEffectParticles.begin(), m_alphaEffectParticles.end(),
        mapped + m_effectParticles.size());

    D3D12_VERTEX_BUFFER_VIEW view = {};
    view.BufferLocation =
        m_effectInstanceBuffers[m_frameIndex]->GetGPUVirtualAddress();
    view.SizeInBytes = static_cast<UINT>(
        totalCount * sizeof(EffectParticleVertex));
    view.StrideInBytes = sizeof(EffectParticleVertex);

    struct Constants
    {
        XMFLOAT4X4 viewProjection;
        XMFLOAT4 cameraForward;
    } constants;
    XMStoreFloat4x4(&constants.viewProjection,
        XMMatrixTranspose(viewProjection));
    XMVECTOR forward = XMVector3Normalize(
        XMLoadFloat3(&m_cameraTarget) - XMLoadFloat3(&m_cameraPosition));
    XMStoreFloat4(&constants.cameraForward, forward);

    ID3D12DescriptorHeap* heaps[] = { m_uiSrvHeap.Get() };
    m_commandList->SetDescriptorHeaps(1, heaps);
    m_commandList->SetGraphicsRootSignature(m_effectRootSignature.Get());
    m_commandList->SetGraphicsRoot32BitConstants(0, 20, &constants, 0);
    auto srv = m_uiSrvHeap->GetGPUDescriptorHandleForHeapStart();
    const auto step = m_device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    srv.ptr += UINT64(m_effectTextureIndex + 1) * step;
    m_commandList->SetGraphicsRootDescriptorTable(1, srv);
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_commandList->IASetVertexBuffers(0, 1, &view);
    m_commandList->IASetIndexBuffer(nullptr);
    if (!m_effectParticles.empty())
    {
        m_commandList->SetPipelineState(m_effectPSO.Get());
        m_commandList->DrawInstanced(6,
            static_cast<UINT>(m_effectParticles.size()), 0, 0);
    }
    if (!m_alphaEffectParticles.empty())
    {
        m_commandList->SetPipelineState(m_effectAlphaPSO.Get());
        m_commandList->DrawInstanced(6,
            static_cast<UINT>(m_alphaEffectParticles.size()), 0,
            static_cast<UINT>(m_effectParticles.size()));
    }
}
