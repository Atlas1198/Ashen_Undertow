#include "RendererInternal.h"

#include <algorithm>

using namespace DirectX;
using namespace RendererDetail;

bool Renderer::CreateColliderDebugResources()
{
    D3D12_ROOT_PARAMETER rootParameter = {};
    rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParameter.Constants.ShaderRegister = 0;
    rootParameter.Constants.Num32BitValues = 16;
    rootParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
    rootDesc.NumParameters = 1;
    rootDesc.pParameters = &rootParameter;
    rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> signature, error;
    if (!Check(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        &signature, &error)))
    {
        if (error) OutputDebugStringA(static_cast<const char*>(error->GetBufferPointer()));
        return false;
    }
    if (!Check(m_device->CreateRootSignature(0, signature->GetBufferPointer(),
        signature->GetBufferSize(), IID_PPV_ARGS(&m_debugLineRootSignature))))
        return false;

    UINT compileFlags = 0;
#if defined(_DEBUG)
    compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    ComPtr<ID3DBlob> vs, ps;
    if (!Check(D3DCompileFromFile(L"shaders/ColliderDebug.hlsl", nullptr, nullptr,
        "VSMain", "vs_5_0", compileFlags, 0, &vs, &error)))
    {
        if (error) OutputDebugStringA(static_cast<const char*>(error->GetBufferPointer()));
        return false;
    }
    error.Reset();
    if (!Check(D3DCompileFromFile(L"shaders/ColliderDebug.hlsl", nullptr, nullptr,
        "PSMain", "ps_5_0", compileFlags, 0, &ps, &error)))
    {
        if (error) OutputDebugStringA(static_cast<const char*>(error->GetBufferPointer()));
        return false;
    }

    const D3D12_INPUT_ELEMENT_DESC inputElements[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = {};
    pso.pRootSignature = m_debugLineRootSignature.Get();
    pso.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    pso.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    pso.InputLayout = { inputElements, _countof(inputElements) };
    pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pso.RasterizerState.DepthClipEnable = TRUE;
    pso.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pso.DepthStencilState.DepthEnable = FALSE;
    pso.DepthStencilState.StencilEnable = FALSE;
    pso.SampleMask = UINT_MAX;
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    pso.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    pso.SampleDesc.Count = 1;
    if (!Check(m_device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_debugLinePSO))))
        return false;

    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC buffer = {};
    buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer.Width = sizeof(DebugLineVertex) * kMaxDebugLineVertices;
    buffer.Height = 1;
    buffer.DepthOrArraySize = 1;
    buffer.MipLevels = 1;
    buffer.SampleDesc.Count = 1;
    buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    for (uint32_t frame = 0; frame < kFrameCount; ++frame)
    {
        if (!Check(m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE,
            &buffer, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&m_debugLineVertexBuffers[frame]))))
            return false;
        if (!Check(m_debugLineVertexBuffers[frame]->Map(
            0, nullptr, &m_debugLineMapped[frame])))
            return false;
    }
    return true;
}

void Renderer::DrawCollider(const XMFLOAT3& center, const XMFLOAT3& halfExtents,
    float radius, bool colliding)
{
    m_debugColliders.push_back({ center, halfExtents, radius, colliding });
}

void Renderer::DrawColliderSegment(const XMFLOAT3& start, const XMFLOAT3& end,
    float radius, bool colliding)
{
    const XMFLOAT3 center { (start.x + end.x) * 0.5f,
        (start.y + end.y) * 0.5f, (start.z + end.z) * 0.5f };
    m_debugColliders.push_back(
        { center, {}, radius, colliding, start, end, true });
}

void Renderer::DrawColliderDebug(const XMMATRIX& viewProjection)
{
    if (m_debugColliders.empty()) return;

    std::vector<DebugLineVertex> vertices;
    vertices.reserve(std::min<size_t>(m_debugColliders.size() * 256,
        kMaxDebugLineVertices));
    const auto addLine = [&](const XMFLOAT3& a, const XMFLOAT3& b,
        const XMFLOAT4& color)
    {
        if (vertices.size() + 2 > kMaxDebugLineVertices) return;
        vertices.push_back({ a, color });
        vertices.push_back({ b, color });
    };
    const auto addRing = [&](const XMFLOAT3& center, const XMFLOAT3& u,
        const XMFLOAT3& v, float radius, const XMFLOAT4& color)
    {
        constexpr int segments = 24;
        for (int i = 0; i < segments; ++i)
        {
            const float a0 = XM_2PI * float(i) / segments;
            const float a1 = XM_2PI * float(i + 1) / segments;
            const XMFLOAT3 p0 { center.x + radius * (u.x * std::cos(a0) + v.x * std::sin(a0)),
                center.y + radius * (u.y * std::cos(a0) + v.y * std::sin(a0)),
                center.z + radius * (u.z * std::cos(a0) + v.z * std::sin(a0)) };
            const XMFLOAT3 p1 { center.x + radius * (u.x * std::cos(a1) + v.x * std::sin(a1)),
                center.y + radius * (u.y * std::cos(a1) + v.y * std::sin(a1)),
                center.z + radius * (u.z * std::cos(a1) + v.z * std::sin(a1)) };
            addLine(p0, p1, color);
        }
    };

    for (const auto& collider : m_debugColliders)
    {
        const XMFLOAT4 color = collider.colliding
            ? XMFLOAT4 { 1.0f, 0.02f, 0.01f, 1.0f }
            : XMFLOAT4 { 0.02f, 1.0f, 0.05f, 1.0f };
        if (collider.hasSegment)
        {
            const XMVECTOR startVector = XMLoadFloat3(&collider.segmentStart);
            const XMVECTOR endVector = XMLoadFloat3(&collider.segmentEnd);
            const XMVECTOR difference = endVector - startVector;
            const float length = XMVectorGetX(XMVector3Length(difference));
            if (length <= 0.0001f)
            {
                addRing(collider.center, { 1,0,0 }, { 0,1,0 }, collider.radius, color);
                addRing(collider.center, { 1,0,0 }, { 0,0,1 }, collider.radius, color);
                addRing(collider.center, { 0,1,0 }, { 0,0,1 }, collider.radius, color);
                continue;
            }
            const XMVECTOR axisVector = difference / length;
            const XMVECTOR reference = std::abs(XMVectorGetY(axisVector)) < 0.9f
                ? XMVectorSet(0, 1, 0, 0) : XMVectorSet(1, 0, 0, 0);
            XMFLOAT3 axis, u, v;
            XMStoreFloat3(&axis, axisVector);
            XMStoreFloat3(&u, XMVector3Normalize(XMVector3Cross(axisVector, reference)));
            XMStoreFloat3(&v, XMVector3Cross(axisVector, XMLoadFloat3(&u)));
            addRing(collider.segmentStart, u, v, collider.radius, color);
            addRing(collider.segmentEnd, u, v, collider.radius, color);
            constexpr int sides = 12;
            constexpr int arcSegments = 6;
            for (int side = 0; side < sides; ++side)
            {
                const float angle = XM_2PI * float(side) / sides;
                const XMFLOAT3 radial { u.x * std::cos(angle) + v.x * std::sin(angle),
                    u.y * std::cos(angle) + v.y * std::sin(angle),
                    u.z * std::cos(angle) + v.z * std::sin(angle) };
                addLine({ collider.segmentStart.x + radial.x * collider.radius,
                    collider.segmentStart.y + radial.y * collider.radius,
                    collider.segmentStart.z + radial.z * collider.radius },
                    { collider.segmentEnd.x + radial.x * collider.radius,
                    collider.segmentEnd.y + radial.y * collider.radius,
                    collider.segmentEnd.z + radial.z * collider.radius }, color);
                for (int arc = 0; arc < arcSegments; ++arc)
                {
                    const float a0 = XM_PIDIV2 * float(arc) / arcSegments;
                    const float a1 = XM_PIDIV2 * float(arc + 1) / arcSegments;
                    const auto capPoint = [&](const XMFLOAT3& center, float sign,
                        float angleValue)
                    {
                        return XMFLOAT3 {
                            center.x + collider.radius * (radial.x * std::cos(angleValue) + sign * axis.x * std::sin(angleValue)),
                            center.y + collider.radius * (radial.y * std::cos(angleValue) + sign * axis.y * std::sin(angleValue)),
                            center.z + collider.radius * (radial.z * std::cos(angleValue) + sign * axis.z * std::sin(angleValue)) };
                    };
                    addLine(capPoint(collider.segmentStart, -1.0f, a0),
                        capPoint(collider.segmentStart, -1.0f, a1), color);
                    addLine(capPoint(collider.segmentEnd, 1.0f, a0),
                        capPoint(collider.segmentEnd, 1.0f, a1), color);
                }
            }
        }
        else if (collider.radius <= 0.0f)
        {
            XMFLOAT3 corners[8];
            for (int i = 0; i < 8; ++i)
            {
                corners[i] = {
                    collider.center.x + ((i & 1) ? collider.halfExtents.x : -collider.halfExtents.x),
                    collider.center.y + ((i & 2) ? collider.halfExtents.y : -collider.halfExtents.y),
                    collider.center.z + ((i & 4) ? collider.halfExtents.z : -collider.halfExtents.z)
                };
            }
            const int edges[][2] = {
                {0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7}
            };
            for (const auto& edge : edges) addLine(corners[edge[0]], corners[edge[1]], color);
        }
        else if (collider.halfExtents.y <= 0.0f)
        {
            addRing(collider.center, { 1,0,0 }, { 0,1,0 }, collider.radius, color);
            addRing(collider.center, { 1,0,0 }, { 0,0,1 }, collider.radius, color);
            addRing(collider.center, { 0,1,0 }, { 0,0,1 }, collider.radius, color);
        }
        else
        {
            const float h = collider.halfExtents.y;
            const XMFLOAT3 top { collider.center.x, collider.center.y + h, collider.center.z };
            const XMFLOAT3 bottom { collider.center.x, collider.center.y - h, collider.center.z };
            addRing(top, { 1,0,0 }, { 0,0,1 }, collider.radius, color);
            addRing(bottom, { 1,0,0 }, { 0,0,1 }, collider.radius, color);
            constexpr int sides = 12;
            constexpr int arcSegments = 6;
            for (int side = 0; side < sides; ++side)
            {
                const float angle = XM_2PI * float(side) / sides;
                const float dx = std::cos(angle), dz = std::sin(angle);
                addLine({ bottom.x + dx * collider.radius, bottom.y, bottom.z + dz * collider.radius },
                    { top.x + dx * collider.radius, top.y, top.z + dz * collider.radius }, color);
                for (int arc = 0; arc < arcSegments; ++arc)
                {
                    const float a0 = XM_PIDIV2 * float(arc) / arcSegments;
                    const float a1 = XM_PIDIV2 * float(arc + 1) / arcSegments;
                    addLine(
                        { top.x + dx * collider.radius * std::cos(a0), top.y + collider.radius * std::sin(a0), top.z + dz * collider.radius * std::cos(a0) },
                        { top.x + dx * collider.radius * std::cos(a1), top.y + collider.radius * std::sin(a1), top.z + dz * collider.radius * std::cos(a1) }, color);
                    addLine(
                        { bottom.x + dx * collider.radius * std::cos(a0), bottom.y - collider.radius * std::sin(a0), bottom.z + dz * collider.radius * std::cos(a0) },
                        { bottom.x + dx * collider.radius * std::cos(a1), bottom.y - collider.radius * std::sin(a1), bottom.z + dz * collider.radius * std::cos(a1) }, color);
                }
            }
        }
    }

    if (vertices.empty()) return;
    memcpy(m_debugLineMapped[m_frameIndex], vertices.data(),
        vertices.size() * sizeof(DebugLineVertex));
    D3D12_VERTEX_BUFFER_VIEW view = {};
    view.BufferLocation = m_debugLineVertexBuffers[m_frameIndex]->GetGPUVirtualAddress();
    view.SizeInBytes = static_cast<UINT>(vertices.size() * sizeof(DebugLineVertex));
    view.StrideInBytes = sizeof(DebugLineVertex);

    XMFLOAT4X4 matrix;
    XMStoreFloat4x4(&matrix, XMMatrixTranspose(viewProjection));
    m_commandList->SetGraphicsRootSignature(m_debugLineRootSignature.Get());
    m_commandList->SetPipelineState(m_debugLinePSO.Get());
    m_commandList->SetGraphicsRoot32BitConstants(0, 16, &matrix, 0);
    m_commandList->IASetVertexBuffers(0, 1, &view);
    m_commandList->IASetIndexBuffer(nullptr);
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
    m_commandList->DrawInstanced(static_cast<UINT>(vertices.size()), 1, 0, 0);
}
