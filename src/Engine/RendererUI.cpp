#include "RendererInternal.h"
#include "UIManager.h"

using namespace RendererDetail;

bool Renderer::CreateUIResources()
{
    D3D12_DESCRIPTOR_RANGE range = {};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 1;
    D3D12_ROOT_PARAMETER parameter = {};
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameter.DescriptorTable = { 1, &range };
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC root = {};
    root.NumParameters = 1;
    root.pParameters = &parameter;
    root.NumStaticSamplers = 1;
    root.pStaticSamplers = &sampler;
    root.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ComPtr<ID3DBlob> signature, error;
    if (!Check(D3D12SerializeRootSignature(&root, D3D_ROOT_SIGNATURE_VERSION_1,
        &signature, &error))) return false;
    if (!Check(m_device->CreateRootSignature(0, signature->GetBufferPointer(),
        signature->GetBufferSize(), IID_PPV_ARGS(&m_uiRootSignature)))) return false;

    ComPtr<ID3DBlob> vs, ps;
    const auto compile = [&](const char* entry, const char* target, ComPtr<ID3DBlob>& blob)
    {
        error.Reset();
        const auto hr = D3DCompileFromFile(L"shaders/UI.hlsl", nullptr, nullptr,
            entry, target, D3DCOMPILE_ENABLE_STRICTNESS, 0, &blob, &error);
        if (error) OutputDebugStringA(static_cast<const char*>(error->GetBufferPointer()));
        return Check(hr);
    };
    if (!compile("VSMain", "vs_5_0", vs) || !compile("PSMain", "ps_5_0", ps)) return false;
    const D3D12_INPUT_ELEMENT_DESC elements[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = {};
    pso.pRootSignature = m_uiRootSignature.Get();
    pso.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    pso.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    pso.InputLayout = { elements, _countof(elements) };
    pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pso.RasterizerState.DepthClipEnable = TRUE;
    auto& blend = pso.BlendState.RenderTarget[0];
    blend.BlendEnable = TRUE;
    blend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    blend.BlendOp = D3D12_BLEND_OP_ADD;
    blend.SrcBlendAlpha = D3D12_BLEND_ONE;
    blend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pso.SampleMask = UINT_MAX;
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso.SampleDesc.Count = 1;
    if (!Check(m_device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_uiPSO)))) return false;

    D3D12_DESCRIPTOR_HEAP_DESC descriptors = {};
    descriptors.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    descriptors.NumDescriptors = UIManager::MaxImages + 1; // 0は単色用の白。
    descriptors.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (!Check(m_device->CreateDescriptorHeap(&descriptors, IID_PPV_ARGS(&m_uiSrvHeap)))) return false;

    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC buffer = {};
    buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer.Width = sizeof(UIVertex) * UIManager::MaxQuads * 6;
    buffer.Height = 1;
    buffer.DepthOrArraySize = buffer.MipLevels = 1;
    buffer.SampleDesc.Count = 1;
    buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    for (uint32_t frame = 0; frame < kFrameCount; ++frame)
    {
        if (!Check(m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE,
            &buffer, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&m_uiVertexBuffers[frame])))) return false;
        D3D12_RANGE noRead { 0, 0 };
        if (!Check(m_uiVertexBuffers[frame]->Map(0, &noRead, &m_uiMapped[frame]))) return false;
    }
    return true;
}

bool Renderer::PrepareUIImages(const UIManager& ui)
{
    // Renderのコマンド記録開始前に転送。既存の画像は再転送しない。
    while (m_uiTextures.size() < ui.GetImages().size() + 1)
    {
        const size_t slot = m_uiTextures.size();
        const uint8_t white[] = { 255, 255, 255, 255 };
        const auto* image = slot ? &ui.GetImages()[slot - 1] : nullptr;
        ComPtr<ID3D12Resource> texture;
        // トーンマップ後のUNORM画面へ、画像の表示色をそのまま出す。
        if (!UploadTexture(image ? image->pixels.data() : white,
            image ? image->width : 1, image ? image->height : 1,
            DXGI_FORMAT_R8G8B8A8_UNORM, texture)) return false;
        D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
        srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MipLevels = texture->GetDesc().MipLevels;
        auto handle = m_uiSrvHeap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += slot * m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        m_device->CreateShaderResourceView(texture.Get(), &srv, handle);
        m_uiTextures.push_back(std::move(texture));
    }
    return true;
}

void Renderer::DrawUI(const UIManager& ui)
{
    const auto& commands = ui.GetCommands();
    if (commands.empty()) return;
    auto* vertices = static_cast<UIVertex*>(m_uiMapped[m_frameIndex]);
    constexpr int indices[] = { 0, 1, 2, 0, 2, 3 };
    const DirectX::XMFLOAT2 uvs[] = { {0,0}, {1,0}, {1,1}, {0,1} };
    size_t count = 0;
    for (const auto& command : commands)
        for (int corner : indices)
        {
            const auto& p = command.points[corner];
            vertices[count++] = { { 2.0f * p.x / m_width - 1.0f,
                1.0f - 2.0f * p.y / m_height }, uvs[corner], command.colors[corner] };
        }
    D3D12_VERTEX_BUFFER_VIEW view = {};
    view.BufferLocation = m_uiVertexBuffers[m_frameIndex]->GetGPUVirtualAddress();
    view.SizeInBytes = static_cast<UINT>(count * sizeof(UIVertex));
    view.StrideInBytes = sizeof(UIVertex);
    ID3D12DescriptorHeap* heaps[] = { m_uiSrvHeap.Get() };
    m_commandList->SetDescriptorHeaps(1, heaps);
    m_commandList->SetGraphicsRootSignature(m_uiRootSignature.Get());
    m_commandList->SetPipelineState(m_uiPSO.Get());
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_commandList->IASetVertexBuffers(0, 1, &view);
    m_commandList->IASetIndexBuffer(nullptr);
    const auto step = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    for (size_t i = 0; i < commands.size(); ++i)
    {
        auto srv = m_uiSrvHeap->GetGPUDescriptorHandleForHeapStart();
        srv.ptr += UINT64(commands[i].image.IsValid() ? commands[i].image.index + 1 : 0) * step;
        m_commandList->SetGraphicsRootDescriptorTable(0, srv);
        m_commandList->DrawInstanced(6, 1, static_cast<UINT>(i * 6), 0);
    }
}
